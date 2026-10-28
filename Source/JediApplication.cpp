// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDeskTop
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TMenu
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TRect
#define Uses_TFileDialog
#define Uses_MsgBox
#include "Version.h"
#include "JediApplication.h"
#include "PageDialog.h"
#include "WindowsDialog.h"
#include "JediStatusLine.h"
#include "JournalLayout.h"
#include "JournalWindow.h"
#include "TimelineWindow.h"
#include "Commands.h"
#include "JournalSource.h"

#include <time.h>
#include <string.h>

#include <algorithm>

#define DEFAULT_CAPACITY  100000
#define REPEAT_TIMEOUT    20      // Milliseconds to wait for events while a mouse button is held, Turbo Vision repeats it when the wait ends

// JediApplication

JediApplication::JediApplication(const JournalFilter& filter, int history, const std::string& layout) :
  TProgInit(&JediApplication::initStatusLine, &JediApplication::initMenuBar, &TProgram::initDeskTop)
{
  TRect bounds;
  std::string error;

  this->filter    = filter;
  this->history   = history;

  focused = nullptr;
  number  = 0;
  pressed = false;

  // Right corner of the menu bar, " HH:MM:SS "
  bounds     = getExtent();
  bounds.a.x = bounds.b.x - 10;
  bounds.b.y = bounds.a.y + 1;
  clock      = new JediClock(bounds);

  insert(clock);

  if ((layout.empty()) ||
      (!load(layout, error)))
  {
    // No layout to start with, a window of the command line filter
    open(filter);
  }
}

TStatusLine* JediApplication::initStatusLine(TRect bounds)
{
  bounds.a.y = bounds.b.y - 1;

  return new JediStatusLine(bounds,
    *new TStatusDef(0, 0xffff,
      new TStatusItem("~F1~ Help",      kbF1,    cmJournalHelp,
      new TStatusItem("~F2~ New",       kbF2,    cmJournalNew,
      new TStatusItem("~F3~ Levels",    kbF3,    cmJournalLevels,
      new TStatusItem("~F4~ Text",      kbF4,    cmJournalText,
      new TStatusItem("~F7~ Source",    kbF7,    cmJournalSource,
      new TStatusItem("~Ctrl-F~ Find",  kbCtrlF, cmJournalFind,
      new TStatusItem("~Space~ Pause",  kbNoKey, cmJournalPause,
      new TStatusItem("~Alt-X~ Exit",   kbAltX,  cmQuit,
      new TStatusItem(0,                kbCtrlC, cmQuit)))))))))));
}

TMenuBar* JediApplication::initMenuBar(TRect bounds)
{
  bounds.b.y = bounds.a.y + 1;

  // Window shortcuts live here, the menu bar handles them without a status line item
  return new TMenuBar(bounds,
    *new TSubMenu("~F~ile", kbAltF) +
      *new TMenuItem("~N~ew window",     cmJournalNew,    kbF2,      hcNoContext, "F2") +
      *new TMenuItem("~C~lone window",   cmJournalClone,  kbShiftF2, hcNoContext, "Shift-F2") +
      *new TMenuItem("~O~pen layout...", cmJournalOpen,   kbCtrlO,   hcNoContext, "Ctrl-O") +
      *new TMenuItem("~S~ave layout...", cmJournalSave,   kbCtrlS,   hcNoContext, "Ctrl-S") +
      newLine() +
      *new TMenuItem("~E~xport lines...", cmJournalExport, kbCtrlE, hcNoContext, "Ctrl-E") +
      newLine() +
      *new TMenuItem("E~x~it",           cmQuit,          kbAltX,    hcNoContext, "Alt-X") +
    *new TSubMenu("~J~ournal", kbAltJ) +
      *new TMenuItem("~L~evels...",     cmJournalLevels,       kbF3,      hcNoContext, "F3") +
      *new TMenuItem("~T~ext...",       cmJournalText,         kbF4,      hcNoContext, "F4") +
      *new TMenuItem("~S~ource...",     cmJournalSource,       kbF7,      hcNoContext, "F7") +
      *new TMenuItem("~J~ournals...",   cmJournalJournals,     kbShiftF7, hcNoContext, "Shift-F7") +
      *new TMenuItem("T~i~me...",       cmJournalTime,         kbCtrlT,   hcNoContext, "Ctrl-T") +
      *new TMenuItem("Col~u~mns...",    cmJournalColumns,      kbCtrlK,   hcNoContext, "Ctrl-K") +
      *new TMenuItem("~W~rap lines",    cmJournalWrap,         kbCtrlW,   hcNoContext, "Ctrl-W") +
      *new TMenuItem("~H~ighlight...",  cmJournalHighlight,    kbCtrlL,   hcNoContext, "Ctrl-L") +
      newLine() +
      *new TMenuItem("~F~ind...",       cmJournalFind,         kbCtrlF,   hcNoContext, "Ctrl-F") +
      *new TMenuItem("Find ~n~ext",     cmJournalFindNext,     kbCtrlN,   hcNoContext, "Ctrl-N") +
      *new TMenuItem("Find p~r~evious", cmJournalFindPrevious, kbCtrlP,   hcNoContext, "Ctrl-P") +
      *new TMenuItem("~D~etails...",    cmJournalDetails,      kbNoKey,   hcNoContext, "Enter") +
      newLine() +
      *new TMenuItem("~G~o to time...", cmJournalGoTo,         kbCtrlG,   hcNoContext, "Ctrl-G") +
      *new TMenuItem("S~y~nc windows",  cmJournalSync,         kbAltG,    hcNoContext, "Alt-G") +
      *new TMenuItem("Ti~m~eline",      cmJournalTimeline,     kbAltT,    hcNoContext, "Alt-T") +
      *new TMenuItem("~B~ack",          cmJournalBack,         kbAltLeft, hcNoContext, "Alt-Left") +
      newLine() +
      *new TMenuItem("~P~ause",         cmJournalPause,        kbNoKey,   hcNoContext, "Space") +
      *new TMenuItem("C~o~py",          cmJournalCopy,         kbCtrlIns, hcNoContext, "Ctrl-Ins") +
      *new TMenuItem("~C~lear",         cmJournalClear,        kbF8,      hcNoContext, "F8") +
    *new TSubMenu("~W~indow", kbAltW) +
      *new TMenuItem("~T~ile",        cmTile,           kbF9,      hcNoContext, "F9") +
      *new TMenuItem("C~a~scade",     cmCascade,        kbNoKey) +
      *new TMenuItem("~Z~oom",        cmZoom,           kbF5,      hcNoContext, "F5") +
      *new TMenuItem("~S~ize/Move",   cmResize,         kbCtrlF5,  hcNoContext, "Ctrl-F5") +
      *new TMenuItem("~N~ext",        cmNext,           kbF6,      hcNoContext, "F6") +
      *new TMenuItem("~P~revious",    cmPrev,           kbShiftF6, hcNoContext, "Shift-F6") +
      *new TMenuItem("~L~ist...",     cmJournalWindows, kbAlt0,    hcNoContext, "Alt-0") +
      *new TMenuItem("~C~lose",       cmClose,          kbAltF3,   hcNoContext, "Alt-F3") +
    *new TSubMenu("~H~elp", kbAltH) +
      *new TMenuItem("~K~eys",        cmJournalHelp,   kbF1,      hcNoContext, "F1") +
      *new TMenuItem("~A~bout",       cmJournalAbout,  kbNoKey));
}

void JediApplication::getEvent(TEvent& event)
{
  TApplication::getEvent(event);

  if (event.what & evMouseDown) pressed = true;
  if (event.what & evMouseUp)   pressed = false;

  if (event.what != evNothing)
  {
    // Whatever the event has changed, idle() tells the status line and the windows about it at once
    eventTimeoutMs = 0;
  }

  if ((event.what == evCommand) &&
      (event.message.command == cmQuit) &&
      (TopView() != this))
  {
    // Status line makes the command here and a dialog would ignore it: close the dialog, quit afterwards
    TopView()->endModal(cmCancel);
    putEvent(event);
    clearEvent(event);
  }
}

void JediApplication::handleEvent(TEvent& event)
{
  JournalWindow* current;

  TApplication::handleEvent(event);

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalOpenFilter))
  {
    // A window asks for another one with a filter of its making
    open(*static_cast<const JournalFilter*>(event.message.infoPtr));
    clearEvent(event);
    return;
  }

  if (event.what == evCommand)
  {
    // A timeline counts the entries of the focused window
    current = dynamic_cast<JournalWindow*>(deskTop->current);

    switch (event.message.command)
    {
      case cmJournalNew:       open(filter);                                                         break;
      case cmJournalClone:     clone();                                                              break;
      case cmJournalWindows:   chooseWindow();                                                       break;
      case cmJournalOpen:      chooseOpen();                                                         break;
      case cmJournalSave:      chooseSave();                                                         break;
      case cmJournalHelp:      showHelp();                                                           break;
      case cmJournalTimeline:  timeline(current);                                                    break;
      case cmJournalAbout:     showAbout();                                                          break;
      default:                                                                                       return;
    }

    clearEvent(event);
  }
}

void JediApplication::idle()
{
  JournalPoll poll;
  struct timespec now;

  TApplication::idle();
  clock->update();

  // Nothing else happens on its own, the wait for events lasts until the clock turns to the next second
  clock_gettime(CLOCK_REALTIME, &now);
  poll.timeout = 1000 - now.tv_nsec / 1000000;

  // Every window asks its own journal for appended entries, their changes end the wait as events do
  message(deskTop, evBroadcast, cmJournalPoll, &poll);

  eventTimeoutMs = pressed ? std::min(poll.timeout, REPEAT_TIMEOUT) : poll.timeout;

  if (focused != deskTop->current)
  {
    // Status line tells about the focused window
    focused = deskTop->current;
    statusLine->drawView();
  }
}

void JediApplication::open(const JournalFilter& filter)
{
  JournalWindow* window;

  number  = std::min<short>(number + 1, 10);
  window  = new JournalWindow(deskTop->getExtent(), (number < 10) ? number : wnNoNumber, filter, history, DEFAULT_CAPACITY);

  deskTop->insert(window);
  tile();
}

void JediApplication::clone()
{
  TWindow* window;
  JournalWindow* journal;
  TimelineWindow* timeline;

  journal  = dynamic_cast<JournalWindow*>(deskTop->current);
  timeline = dynamic_cast<TimelineWindow*>(deskTop->current);

  if (timeline != nullptr)
  {
    // Timelines go without numbers
    window = new TimelineWindow(timeline->getBounds(), *timeline->view);
  }
  else if (journal != nullptr)
  {
    number = std::min<short>(number + 1, 10);
    window = new JournalWindow(journal->getBounds(), (number < 10) ? number : wnNoNumber, *journal->view);
  }
  else
  {
    // Nothing focused to copy
    return;
  }

  deskTop->insert(window);
  tile();
}

static void CollectTitle(TView* view, void* argument)
{
  TWindow* window;

  if ((window = dynamic_cast<TWindow*>(view)) != nullptr)
  {
    // The desktop walks from the front, the focused window comes first
    static_cast<std::vector<TWindow*>*>(argument)->push_back(window);
  }
}

void JediApplication::chooseWindow()
{
  TWindow* window;
  WindowsDialog* dialog;
  std::vector<TWindow*> windows;

  deskTop->forEach(CollectTitle, &windows);

  dialog = new WindowsDialog(windows);

  if ((deskTop->execView(dialog) != cmCancel) &&
      ((window = dialog->selection()) != nullptr))
  {
    // Comes to the front along with the focus
    window->select();
  }

  TObject::destroy(dialog);
}

void JediApplication::timeline(JournalWindow* current)
{
  TimelineWindow* window;

  if (current == nullptr)
  {
    // Counts belong to the filter of a journal window
    return;
  }

  window = new TimelineWindow(deskTop->getExtent(), current->view->getFilter(), 0, false);

  deskTop->insert(window);
  tile();
}

static void CollectWindow(TView* view, void* argument)
{
  TRect bounds;
  JournalWindow* window;
  TimelineWindow* timeline;
  JournalLayout layout;

  window   = dynamic_cast<JournalWindow*>(view);
  timeline = dynamic_cast<TimelineWindow*>(view);

  if ((window != nullptr) ||
      (timeline != nullptr))
  {
    bounds             = view->getBounds();
    layout.bounds[0]   = bounds.a.x;
    layout.bounds[1]   = bounds.a.y;
    layout.bounds[2]   = bounds.b.x;
    layout.bounds[3]   = bounds.b.y;
    layout.filter      = (window != nullptr) ? window->view->getFilter() : timeline->view->getFilter();
    layout.timeline    = (timeline != nullptr);
    layout.step        = (timeline != nullptr) ? timeline->view->getStep() : 0;
    layout.logarithmic = (timeline != nullptr) && timeline->view->getLogarithmic();

    // The desktop walks from the front, the file keeps the order windows are opened in
    static_cast<std::vector<JournalLayout>*>(argument)->insert(static_cast<std::vector<JournalLayout>*>(argument)->begin(), layout);
  }
}

static void CloseWindow(TView* view, void* argument)
{
  if ((dynamic_cast<JournalWindow*>(view) != nullptr) ||
      (dynamic_cast<TimelineWindow*>(view) != nullptr))
  {
    // Closed after the walk, the desktop must not change under it
    static_cast<std::vector<TView*>*>(argument)->push_back(view);
  }
}

bool JediApplication::load(const std::string& path, std::string& error)
{
  TRect bounds;
  TRect extent;
  bool fitting;
  TWindow* window;
  std::vector<TView*> windows;
  std::vector<JournalLayout> layout;

  if (!LoadJournalLayout(path, layout, error))
  {
    // Windows there are stay as they are
    return false;
  }

  deskTop->forEach(CloseWindow, &windows);

  for (TView* view : windows)
  {
    // The layout replaces the windows there are
    TObject::destroy(view);
  }

  extent  = deskTop->getExtent();
  fitting = true;
  number  = 0;

  for (const JournalLayout& item : layout)
  {
    bounds  = TRect(item.bounds[0], item.bounds[1], item.bounds[2], item.bounds[3]);
    fitting = fitting && (bounds.b.x > bounds.a.x) && (bounds.b.y > bounds.a.y) && extent.contains(bounds.a) && (bounds.b.x <= extent.b.x) && (bounds.b.y <= extent.b.y);

    if (item.timeline)
    {
      // Timelines go without numbers
      window = new TimelineWindow(fitting ? bounds : extent, item.filter, item.step, item.logarithmic);
      deskTop->insert(window);
      continue;
    }

    number  = std::min<short>(number + 1, 10);
    window  = new JournalWindow(fitting ? bounds : extent, (number < 10) ? number : wnNoNumber, item.filter, history, DEFAULT_CAPACITY);

    deskTop->insert(window);
  }

  if (!fitting)
  {
    // Saved on a larger screen
    tile();
  }

  return true;
}

bool JediApplication::save(const std::string& path)
{
  std::vector<JournalLayout> layout;

  deskTop->forEach(CollectWindow, &layout);

  return SaveJournalLayout(path, layout);
}

void JediApplication::chooseOpen()
{
  char path[MAXPATH];
  std::string error;
  TFileDialog* dialog;

  dialog = new TFileDialog("*.jedi", "Open layout", "~N~ame", fdOpenButton, 1);

  if (deskTop->execView(dialog) != cmCancel)
  {
    dialog->getFileName(path);

    if (!load(path, error))
    {
      messageBox(mfError | mfOKButton, "Cannot read a layout from %s, %s", path, error.c_str());
    }
  }

  TObject::destroy(dialog);
}

void JediApplication::chooseSave()
{
  char path[MAXPATH];
  const char* name;
  TFileDialog* dialog;

  dialog = new TFileDialog("*.jedi", "Save layout", "~N~ame", fdOKButton, 1);

  if (deskTop->execView(dialog) != cmCancel)
  {
    dialog->getFileName(path);

    if (((name = strrchr(path, '/')) != nullptr) &&
        (strchr(name, '.') == nullptr))
    {
      // Layouts are told by their extension in the dialog
      strncat(path, ".jedi", sizeof(path) - strlen(path) - 1);
    }

    if (!save(path))
    {
      messageBox(mfError | mfOKButton, "Cannot write the layout to %s", path);
    }
  }

  TObject::destroy(dialog);
}

void JediApplication::showHelp()
{
  static const char* const keys[] =
  {
    "Windows",
    "  F2              New window with the filter of the command line, Shift-F2 copies one",
    "  Shift-F2        Clone of the focused window: its lines, filter, find, selection",
    "                  and place, or the counts, step, scale and cursor of a timeline;",
    "                  the journal goes on in both of them on their own",
    "  F6, Shift-F6    Next, previous window",
    "  Alt-0           List of the windows to go to, the filter narrows it down,",
    "                  the window used before is under the cursor",
    "  F5, Ctrl-F5     Zoom, size or move the window",
    "  F9              Tile the windows",
    "  Alt-F3          Close the window",
    "  Ctrl-O, Ctrl-S  Open, save the layout of all windows",
    "",
    "Filter of the window, the journal applies it when asked",
    "  F3              Levels",
    "  F4              Text: plain or regular expression, to show or to leave out",
    "  F7              Source: unit, identifier, command, transport, boot, user, machine, PID",
    "  Shift-F7        Journals: of this machine, of every machine or of a directory",
    "  Ctrl-T          Time range",
    "",
    "Lines",
    "  Ctrl-K          Columns, UTC and milliseconds",
    "  Ctrl-W          Wrap long messages",
    "  Ctrl-L          Highlight rules: built-in, off or a rule file of grc",
    "  Space           Pause or follow the tail",
    "  Up, PgUp, Home  Above the first line the older entries come in",
    "  Shift+arrows    Select lines, also with the mouse",
    "  Esc             Drop the selection",
    "  Enter           All fields of the entry, also double click, Enter on a field there",
    "                  opens a window with the filter of this one and the field",
    "  Ctrl-F          Find and highlight, Ctrl-N and Ctrl-P go to the next and previous one,",
    "                  the search goes on into the older entries above the first line",
    "  Ctrl-G          Go to a time",
    "  Alt-G           Every other window goes to the time of the selected line",
    "  Alt-Left        Back to where the window was before Ctrl-G, Alt-G or a find,",
    "                  also Alt-Backspace",
    "  Alt-T           Timeline: entries of the filter of the window by intervals of time,",
    "                  errors, warnings and the rest stacked in a column per interval",
    "  Ctrl-Ins        Copy the selected lines or the screen",
    "  Ctrl-E          Export the selected lines or all of them to a file",
    "  F8              Clear the window",
    "",
    "  Alt-X           Exit, the layout is kept for the next start",
    "",
    "Timeline",
    "  Left, Right     Interval under the cursor, also with the mouse, PgUp, PgDn, Home, End",
    "  Enter           Every journal window goes to the first entry of the interval,",
    "                  also double click",
    "  +, -            Finer or coarser intervals",
    "  L               Linear or logarithmic scale"
  };

  TRect bounds;
  PageDialog* dialog;

  bounds = deskTop->getExtent();
  bounds.grow(-2, -1);
  dialog = new PageDialog(bounds, "Keys", std::vector<std::string>(std::begin(keys), std::end(keys)));

  deskTop->execView(dialog);
  TObject::destroy(dialog);
}

void JediApplication::showAbout()
{
  messageBox("\003TurboJedi " VERSION "\n\003Turbo Journald Interface\n\003Artem Prilutskiy, 2026", mfInformation | mfOKButton);
}
