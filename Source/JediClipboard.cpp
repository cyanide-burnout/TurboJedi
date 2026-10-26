// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TClipboard
#include <tvision/tv.h>

#include "JediClipboard.h"

#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <spawn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>

#define COMMAND_TIMEOUT  3000  // Milliseconds the command has to take the text and finish, the screen waits meanwhile

extern char** environ;

static std::string command;

// Helpers

static int64_t GetMilliseconds()
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static bool RunCommand(const std::string& text)
{
  int result;
  int status;
  int handles[2];
  pid_t process;
  size_t offset;
  ssize_t length;
  int64_t deadline;
  sigset_t signals;
  struct pollfd poller;
  struct sigaction ignore {};
  struct sigaction previous;
  posix_spawnattr_t attributes;
  posix_spawn_file_actions_t actions;
  char* const arguments[] = { (char*)"sh", (char*)"-c", (char*)command.c_str(), nullptr };

  if (pipe2(handles, O_CLOEXEC) < 0)
  {
    return false;
  }

  // The text comes on the standard input, whatever the command prints must not get on the screen;
  // a group of its own lets the timeout stop whatever it has started, and broken pipes are its own business
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, handles[0], STDIN_FILENO);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

  sigemptyset(&signals);
  sigaddset(&signals, SIGPIPE);
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setsigdefault(&attributes, &signals);
  sigemptyset(&signals);
  posix_spawnattr_setsigmask(&attributes, &signals);
  posix_spawnattr_setpgroup(&attributes, 0);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);

  result = posix_spawn(&process, "/bin/sh", &actions, &attributes, arguments, environ);

  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  close(handles[0]);

  if (result != 0)
  {
    close(handles[1]);
    return false;
  }

  // A command that leaves early must not take TurboJedi along by SIGPIPE
  ignore.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &ignore, &previous);
  fcntl(handles[1], F_SETFL, O_NONBLOCK);

  deadline = GetMilliseconds() + COMMAND_TIMEOUT;
  offset   = 0;

  while (offset < text.size())
  {
    poller = { handles[1], POLLOUT, 0 };
    result = poll(&poller, 1, std::max<int64_t>(deadline - GetMilliseconds(), 0));

    if ((result < 0) &&
        (errno == EINTR))
    {
      // Window resized meanwhile
      continue;
    }

    if (result <= 0)
    {
      // Timed out or the poll has failed
      break;
    }

    length = write(handles[1], text.data() + offset, text.size() - offset);

    if ((length < 0) &&
        ((errno == EAGAIN) || (errno == EINTR)))
    {
      // The pipe has filled up again since the poll, the next one waits for it
      continue;
    }

    if (length < 0)
    {
      // The command does not read anymore
      break;
    }

    offset += length;
  }

  sigaction(SIGPIPE, &previous, nullptr);

  // End of the text
  close(handles[1]);

  while (((result = waitpid(process, &status, WNOHANG)) == 0) &&
         (GetMilliseconds() < deadline))
  {
    // Polled, a child of its own may not have SIGCHLD handled
    poll(nullptr, 0, 10);
  }

  if (result == 0)
  {
    // Hangs, its whole group goes
    kill(-process, SIGKILL);
    waitpid(process, &status, 0);
    return false;
  }

  return (result == process) && (offset == text.size()) && WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}

// Interface

void SetClipboardCommand(const std::string& value)
{
  command = value;
}

void CopyClipboardText(const std::string& text)
{
  if (command.empty() ||
      (!RunCommand(text)))
  {
    // May block on an external clipboard tool of Turbo Vision itself
    TClipboard::setText(text);
  }
}
