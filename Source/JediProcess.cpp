// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "JediProcess.h"
#include "JediClipboard.h"
#include "JournalSource.h"
#include "JournalEscapes.h"
#include "PageView.h"
#include "Commands.h"

#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <spawn.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>

#define CAPTURE_QUIET   250                // Milliseconds a captured command may take before its dialog is shown
#define CAPTURE_LIMIT   (16 * 1024 * 1024) // Bytes of output kept, the rest is read and dropped so that the command does not stall
#define OUTPUT_LIMIT    100000             // Lines a window of output keeps, the oldest go
#define POLL_RUNNING    20                 // Milliseconds between looks at a running command
#define POLL_BACKGROUND 200

#define STREAM_INHERIT  -1  // Of TurboJedi, for a command on the terminal
#define STREAM_NULL     -2  // /dev/null

extern char** environ;

// Helpers

static int64_t GetMilliseconds()
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static void AddStream(posix_spawn_file_actions_t* actions, int stream, int target)
{
  if (stream == STREAM_NULL)
  {
    posix_spawn_file_actions_addopen(actions, target, "/dev/null", (target == STDIN_FILENO) ? O_RDONLY : O_WRONLY, 0);
    return;
  }

  if (stream >= 0)
  {
    // The pipe is closed on exec, its copy is not
    posix_spawn_file_actions_adddup2(actions, stream, target);
  }
}

static int StartProcess(pid_t* process, const std::vector<std::string>& arguments, int input, int output, int error, bool group)
{
  int result;
  sigset_t signals;
  std::vector<char*> list;
  posix_spawnattr_t attributes;
  posix_spawn_file_actions_t actions;

  if (arguments.empty())
  {
    // Nothing to run
    return EINVAL;
  }

  for (const std::string& argument : arguments)
  {
    list.push_back(const_cast<char*>(argument.c_str()));
  }

  list.push_back(nullptr);

  posix_spawn_file_actions_init(&actions);
  AddStream(&actions, input,  STDIN_FILENO);
  AddStream(&actions, output, STDOUT_FILENO);
  AddStream(&actions, error,  STDERR_FILENO);

  // Signals TurboJedi ignores or handles are the defaults of the command
  sigemptyset(&signals);
  sigaddset(&signals, SIGPIPE);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGQUIT);
  sigaddset(&signals, SIGTSTP);
  sigaddset(&signals, SIGTTIN);
  sigaddset(&signals, SIGTTOU);
  sigaddset(&signals, SIGCHLD);
  sigaddset(&signals, SIGWINCH);
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setsigdefault(&attributes, &signals);
  sigemptyset(&signals);
  posix_spawnattr_setsigmask(&attributes, &signals);
  posix_spawnattr_setpgroup(&attributes, 0);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | (group ? POSIX_SPAWN_SETPGROUP : 0));

  // glibc tells a command that cannot be executed by the result, not by an exit of the child
  result = posix_spawnp(process, list[0], &actions, &attributes, list.data(), environ);

  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);

  return result;
}

static bool OpenPipe(int handles[2])
{
  if (pipe2(handles, O_CLOEXEC) < 0)
  {
    return false;
  }

  // TurboJedi reads its end between events
  fcntl(handles[0], F_SETFL, O_NONBLOCK);
  return true;
}

// Reads what has come, false at the end of the stream
static bool Drain(int handle, std::string& text, size_t limit)
{
  ssize_t length;
  char buffer[65536];

  for (;;)
  {
    length = read(handle, buffer, sizeof(buffer));

    if ((length < 0) &&
        (errno == EINTR))
    {
      continue;
    }

    if (length < 0)
    {
      // Nothing more for now
      return errno == EAGAIN;
    }

    if (length == 0)
    {
      return false;
    }

    if (text.size() < limit)
    {
      // Beyond the limit it is read to keep the command going, not kept
      text.append(buffer, std::min<size_t>(length, limit - text.size()));
    }
  }
}

static JediOutcome Tell(int status)
{
  if (WIFSIGNALED(status))
  {
    return { "signal", WTERMSIG(status), "" };
  }

  return { "exit", WEXITSTATUS(status), "" };
}

static std::string Describe(const JediOutcome& outcome)
{
  if (outcome.how == "exit")    return "exit " + std::to_string(outcome.code);
  if (outcome.how == "signal")  return "killed by signal " + std::to_string(outcome.code) + " (" + strsignal(outcome.code) + ")";
  if (outcome.how == "timeout") return "stopped after " + std::to_string(outcome.code) + " s";
  if (outcome.how == "cancel")  return "cancelled";
  return outcome.text;
}

static void Stop(pid_t process)
{
  int status;

  // Its whole group goes, whatever it has started included
  kill(-process, SIGKILL);
  while ((waitpid(process, &status, 0) < 0) && (errno == EINTR));
}

std::string QuoteArguments(const std::vector<std::string>& arguments)
{
  std::string line;

  for (const std::string& argument : arguments)
  {
    if (!line.empty())
    {
      line += ' ';
    }

    if ((!argument.empty()) &&
        (argument.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@%+=:,./_-") == std::string::npos))
    {
      // Nothing sh would take apart
      line += argument;
      continue;
    }

    // Single quotes keep everything, a quote of its own ends them, goes escaped and opens them again
    line += '\'';

    for (char character : argument)
    {
      if (character == '\'') line += "'\\''";
      else                   line += character;
    }

    line += '\'';
  }

  return line;
}

// Background

struct BackgroundCommand
{
  pid_t process;
  int error;  // Standard error, -1 once it has ended
  std::string line;
  std::string errors;
};

static std::vector<BackgroundCommand> commands;

bool RunBackground(const std::vector<std::string>& arguments, std::string& failure)
{
  int result;
  int handles[2];
  BackgroundCommand command;

  if (!OpenPipe(handles))
  {
    failure = strerror(errno);
    return false;
  }

  result = StartProcess(&command.process, arguments, STREAM_NULL, STREAM_NULL, handles[1], true);
  close(handles[1]);

  if (result != 0)
  {
    close(handles[0]);
    failure = (arguments.empty() ? std::string("Empty command") : arguments[0]) + ": " + strerror(result);
    return false;
  }

  command.error = handles[0];
  command.line  = QuoteArguments(arguments);
  commands.push_back(command);
  return true;
}

bool PollBackground(std::vector<std::string>& reports)
{
  int status;
  pid_t result;
  JediOutcome outcome;
  std::vector<BackgroundCommand>::iterator command;

  command = commands.begin();

  while (command != commands.end())
  {
    if ((command->error >= 0) &&
        (!Drain(command->error, command->errors, CAPTURE_LIMIT)))
    {
      // Nothing more comes from it
      close(command->error);
      command->error = -1;
    }

    while (((result = waitpid(command->process, &status, WNOHANG)) < 0) && (errno == EINTR));

    if (result == 0)
    {
      // Still running
      ++ command;
      continue;
    }

    if (command->error >= 0)
    {
      // What it has written last, a process it has left behind may keep the pipe open
      Drain(command->error, command->errors, CAPTURE_LIMIT);
      close(command->error);
    }

    outcome = Tell(status);

    if ((result != command->process) ||
        (outcome.how != "exit") ||
        (outcome.code != 0))
    {
      // Success goes without a word
      reports.push_back(command->line + "\n\n" + Describe(outcome) + (command->errors.empty() ? "" : "\n\n" + command->errors));
    }

    command = commands.erase(command);
  }

  return !commands.empty();
}

// Capture

class Capture
{
  public:

    pid_t process;
    int handles[2];  // Standard output and error, -1 at the end
    int status;
    bool ended;
    std::string output;
    std::string errors;

    // Waits up to the milliseconds for anything to read or the end, true once it has ended
    bool step(int wait);
};

bool Capture::step(int wait)
{
  int index;
  int count;
  pid_t result;
  struct pollfd pollers[2];

  count = 0;

  for (index = 0; index < 2; index ++)
  {
    if (handles[index] >= 0)
    {
      pollers[count ++] = { handles[index], POLLIN, 0 };
    }
  }

  // A command that has closed both its streams is looked at in short steps
  poll(pollers, count, (count > 0) ? wait : std::min(wait, POLL_RUNNING));

  for (index = 0; index < 2; index ++)
  {
    if ((handles[index] >= 0) &&
        (!Drain(handles[index], (index == 0) ? output : errors, CAPTURE_LIMIT)))
    {
      close(handles[index]);
      handles[index] = -1;
    }
  }

  while (((result = waitpid(process, &status, WNOHANG)) < 0) && (errno == EINTR));

  if (result != 0)
  {
    ended = true;

    for (index = 0; index < 2; index ++)
    {
      if (handles[index] >= 0)
      {
        // What it has written last, a process it has left behind may keep the pipe open
        Drain(handles[index], (index == 0) ? output : errors, CAPTURE_LIMIT);
        close(handles[index]);
        handles[index] = -1;
      }
    }
  }

  return ended;
}

#define cmCaptureDone     1300
#define cmCaptureTimeout  1301

class CaptureDialog : public TDialog
{
  public:

    CaptureDialog(Capture& capture, const std::string& line, int64_t deadline);

    void handleEvent(TEvent& event) override;

  private:

    Capture& capture;
    int64_t deadline;
};

CaptureDialog::CaptureDialog(Capture& capture, const std::string& line, int64_t deadline) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 60, 9), "Running"),
  capture(capture)
{
  std::string text;

  this->deadline = deadline;

  options |= ofCentered;
  text     = (line.size() > 160) ? (line.substr(0, 157) + "...") : line;

  insert(new TStaticText(TRect(2, 2, 58, 5), text));
  insert(new TButton(TRect(24, 6, 36, 8), "Cancel", cmCancel, bfDefault));
}

void CaptureDialog::handleEvent(TEvent& event)
{
  JournalPoll* poll;

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalPoll))
  {
    // Between the events: the windows go on, the command is read
    poll          = static_cast<JournalPoll*>(event.message.infoPtr);
    poll->timeout = std::min(poll->timeout, POLL_RUNNING);

    if (capture.step(0))
    {
      endModal(cmCaptureDone);
    }
    else if ((deadline > 0) &&
             (GetMilliseconds() >= deadline))
    {
      endModal(cmCaptureTimeout);
    }

    // Every window gets it as well
    return;
  }

  TDialog::handleEvent(event);
}

JediOutcome CaptureCommand(const std::vector<std::string>& arguments, int timeout, std::string& output, std::string& errors)
{
  int result;
  int streams[2][2];
  int64_t quiet;
  int64_t deadline;
  ushort answer;
  Capture capture;
  CaptureDialog* dialog;

  if (!OpenPipe(streams[0]))
  {
    return { "error", 0, strerror(errno) };
  }

  if (!OpenPipe(streams[1]))
  {
    close(streams[0][0]);
    close(streams[0][1]);
    return { "error", 0, strerror(errno) };
  }

  result = StartProcess(&capture.process, arguments, STREAM_NULL, streams[0][1], streams[1][1], true);

  close(streams[0][1]);
  close(streams[1][1]);

  if (result != 0)
  {
    close(streams[0][0]);
    close(streams[1][0]);
    return { "error", 0, (arguments.empty() ? std::string("Empty command") : arguments[0]) + ": " + strerror(result) };
  }

  capture.handles[0] = streams[0][0];
  capture.handles[1] = streams[1][0];
  capture.ended      = false;
  capture.status     = 0;
  quiet              = GetMilliseconds() + CAPTURE_QUIET;
  deadline           = (timeout > 0) ? (GetMilliseconds() + timeout * 1000LL) : 0;
  answer             = cmCaptureDone;

  while ((!capture.ended) &&
         (GetMilliseconds() < quiet))
  {
    // A short command ends before anything is shown
    capture.step(POLL_RUNNING);
  }

  if (!capture.ended)
  {
    dialog = new CaptureDialog(capture, QuoteArguments(arguments), deadline);
    answer = TProgram::deskTop->execView(dialog);
    TObject::destroy(dialog);
  }

  if (!capture.ended)
  {
    // Cancelled or out of time, what it has written so far is kept
    kill(-capture.process, SIGKILL);

    while (!capture.step(POLL_RUNNING));
  }

  output = std::move(capture.output);
  errors = std::move(capture.errors);

  if (answer == cmCaptureTimeout) return { "timeout", timeout, "" };
  if (answer != cmCaptureDone)    return { "cancel", 0, "" };

  return Tell(capture.status);
}

// Output

class OutputView : public PageView
{
  public:

    OutputView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical);

    void draw() override;
    void add(const std::string& text);    // Bytes as they come, lines are made of them
    void finish(const std::string& end);  // Of what has come without a line break, then the outcome

  private:

    std::string rest;
    JournalPaint paint;
    std::vector<std::vector<JournalTint>> tints;

    void put(const std::string& line);
};

OutputView::OutputView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical) :
  PageView(bounds, horizontal, vertical, std::vector<std::string>(), false)
{
  paint = JournalPaint {};
}

void OutputView::put(const std::string& line)
{
  bool bottom;
  int width;

  // The view follows the output while it is at its end
  bottom = (delta.y + size.y >= (int)lines.size());

  lines.emplace_back();
  tints.emplace_back();
  TakeJournalEscapes(line, 0, line.size(), lines.back(), tints.back(), paint);

  if (lines.size() > OUTPUT_LIMIT)
  {
    lines.erase(lines.begin());
    tints.erase(tints.begin());
  }

  width = std::max<int>(limit.x, strwidth(lines.back()));
  setLimit(width, lines.size());

  if (bottom)
  {
    scrollTo(delta.x, std::max<int>(0, (int)lines.size() - size.y));
  }

  drawView();
}

void OutputView::add(const std::string& text)
{
  size_t start;
  size_t end;

  rest += text;
  start = 0;

  while ((end = rest.find('\n', start)) != std::string::npos)
  {
    put(rest.substr(start, end - start));
    start = end + 1;
  }

  rest.erase(0, start);
}

void OutputView::finish(const std::string& end)
{
  if (!rest.empty())
  {
    put(rest);
    rest.clear();
  }

  put("[" + end + "]");
}

void OutputView::draw()
{
  int x;
  int y;
  int from;
  size_t index;
  TDrawBuffer buffer;
  TColorAttr color;

  PageView::draw();
  color = mapColor(1);

  for (y = 0; y < size.y; y ++)
  {
    index = delta.y + y;

    if ((index >= lines.size()) ||
        (tints[index].empty()))
    {
      continue;
    }

    buffer.moveChar(0, ' ', color, size.x);
    buffer.moveStr(0, lines[index], color, size.x, delta.x);

    for (const JournalTint& tint : tints[index])
    {
      // Columns of the text in front of the tint, the scroll takes delta.x of them
      from = strwidth(TStringView(lines[index].data(), tint.start));
      x    = from - delta.x;

      if (x >= size.x)
      {
        continue;
      }

      if (x >= 0) buffer.moveStr(x, TStringView(lines[index].data() + tint.start, tint.end - tint.start), tint.paint.apply(color), size.x - x);
      else        buffer.moveStr(0, TStringView(lines[index].data() + tint.start, tint.end - tint.start), tint.paint.apply(color), size.x, -x);
    }

    writeLine(0, y, size.x, 1, buffer);
  }
}

class OutputWindow : public TDialog
{
  public:

    OutputWindow(const TRect& bounds, const std::string& line, pid_t process, int handle);
    ~OutputWindow();

    void handleEvent(TEvent& event) override;
    const char* getTitle(short size) override;

  private:

    pid_t process;  // Zero once it has ended
    int handle;
    std::string caption;
    OutputView* view;

    void read();
    void copy();
};

OutputWindow::OutputWindow(const TRect& bounds, const std::string& line, pid_t process, int handle) :
  TWindowInit(&TDialog::initFrame),
  TDialog(bounds, nullptr)
{
  TRect extent;
  TScrollBar* horizontal;
  TScrollBar* vertical;

  this->process = process;
  this->handle  = handle;

  caption    = line;
  flags     |= wfGrow | wfZoom;
  growMode   = gfGrowAll | gfGrowRel;
  horizontal = standardScrollBar(sbHorizontal | sbHandleKeyboard);
  vertical   = standardScrollBar(sbVertical   | sbHandleKeyboard);

  extent = getExtent();
  extent.grow(-1, -1);
  extent.b.y -= 3;
  view        = new OutputView(extent, horizontal, vertical);

  insert(view);
  insert(new TButton(TRect(extent.b.x / 2 - 13, extent.b.y + 1, extent.b.x / 2 - 1, extent.b.y + 3), "~C~opy",  cmPageCopy, bfNormal));
  insert(new TButton(TRect(extent.b.x / 2 + 1,  extent.b.y + 1, extent.b.x / 2 + 13, extent.b.y + 3), "C~l~ose", cmClose,    bfDefault));

  view->select();
}

OutputWindow::~OutputWindow()
{
  if (process != 0)
  {
    // Closed while it runs
    Stop(process);
  }

  if (handle >= 0)
  {
    ::close(handle);
  }
}

const char* OutputWindow::getTitle(short size)
{
  static std::string title;

  // The command, with its end cut off to fit
  title = ((short)caption.size() > size) && (size > 3) ? (caption.substr(0, size - 3) + "...") : caption;
  return title.c_str();
}

void OutputWindow::read()
{
  int status;
  pid_t result;
  std::string text;
  JediOutcome outcome;

  if ((handle >= 0) &&
      (!Drain(handle, text, CAPTURE_LIMIT)))
  {
    ::close(handle);
    handle = -1;
  }

  if (!text.empty())
  {
    view->add(text);
  }

  if (process == 0)
  {
    return;
  }

  while (((result = waitpid(process, &status, WNOHANG)) < 0) && (errno == EINTR));

  if (result == 0)
  {
    // Still running
    return;
  }

  if (handle >= 0)
  {
    // What it has written last, a process it has left behind may keep the pipe open
    text.clear();
    Drain(handle, text, CAPTURE_LIMIT);
    ::close(handle);
    handle = -1;
    view->add(text);
  }

  process = 0;
  outcome = Tell(status);
  view->finish(Describe(outcome));
}

void OutputWindow::copy()
{
  std::string text;

  for (const std::string& line : view->lines)
  {
    text += line;
    text += '\n';
  }

  CopyClipboardText(text);
}

void OutputWindow::handleEvent(TEvent& event)
{
  JournalPoll* poll;

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalPoll))
  {
    // Read between the events like the journals, in short steps while it runs
    poll = static_cast<JournalPoll*>(event.message.infoPtr);
    read();

    if (process != 0)
    {
      poll->timeout = std::min(poll->timeout, POLL_RUNNING);
    }

    return;
  }

  if (((event.what == evCommand) && (event.message.command == cmPageCopy)) ||
      ((event.what == evKeyDown) && (event.keyDown.keyCode == kbCtrlIns)))
  {
    copy();
    clearEvent(event);
    return;
  }

  if (((event.what == evCommand) && ((event.message.command == cmCancel) || (event.message.command == cmOK))) ||
      ((event.what == evKeyDown) && (event.keyDown.keyCode == kbEsc)))
  {
    // Not modal, it closes like a window
    clearEvent(event);
    close();
    return;
  }

  TDialog::handleEvent(event);
}

bool OpenOutput(const std::vector<std::string>& arguments, std::string& failure)
{
  int result;
  int handles[2];
  pid_t process;
  TRect bounds;

  if (!OpenPipe(handles))
  {
    failure = strerror(errno);
    return false;
  }

  // Standard output and error go into the window together, in the order they come
  result = StartProcess(&process, arguments, STREAM_NULL, handles[1], handles[1], true);
  close(handles[1]);

  if (result != 0)
  {
    close(handles[0]);
    failure = (arguments.empty() ? std::string("Empty command") : arguments[0]) + ": " + strerror(result);
    return false;
  }

  bounds = TProgram::deskTop->getExtent();
  bounds.grow(-2, -1);
  TProgram::deskTop->insert(new OutputWindow(bounds, QuoteArguments(arguments), process, handles[0]));
  return true;
}

// Terminal

JediOutcome RunTerminal(const std::vector<std::string>& arguments)
{
  int result;
  int status;
  char character;
  pid_t process;
  ssize_t length;
  JediOutcome outcome;
  struct sigaction ignore {};
  struct sigaction interrupt;
  struct sigaction quit;

  TProgram::application->suspend();

  printf("\n$ %s\n", QuoteArguments(arguments).c_str());
  fflush(stdout);

  // As system() does: Ctrl-C and Ctrl-\ are for the command, it runs in the group of the terminal
  ignore.sa_handler = SIG_IGN;
  sigaction(SIGINT,  &ignore, &interrupt);
  sigaction(SIGQUIT, &ignore, &quit);

  result = StartProcess(&process, arguments, STREAM_INHERIT, STREAM_INHERIT, STREAM_INHERIT, false);

  if (result == 0)
  {
    while ((waitpid(process, &status, 0) < 0) && (errno == EINTR));

    outcome = Tell(status);
  }
  else
  {
    outcome = { "error", 0, (arguments.empty() ? std::string("Empty command") : arguments[0]) + ": " + strerror(result) };
  }

  sigaction(SIGINT,  &interrupt, nullptr);
  sigaction(SIGQUIT, &quit,      nullptr);

  printf("\n[%s] Press Enter to return to TurboJedi", Describe(outcome).c_str());
  fflush(stdout);

  // The terminal is in its line mode meanwhile, the line is read up to its end
  do
  {
    length = read(STDIN_FILENO, &character, 1);
  }
  while (((length < 0) && (errno == EINTR)) ||
         ((length > 0) && (character != '\n')));

  TProgram::application->resume();
  TProgram::application->redraw();

  return outcome;
}
