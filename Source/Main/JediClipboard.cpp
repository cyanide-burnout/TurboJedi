// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TClipboard
#include <tvision/tv.h>

#include "JediClipboard.h"

#include <pwd.h>
#include <grp.h>
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
#include <sys/stat.h>
#include <sys/wait.h>

#include <vector>
#include <algorithm>

#define COMMAND_TIMEOUT  3000  // Milliseconds the command has to take the text and finish, the screen waits meanwhile

extern char** environ;

static std::string command;

// The user the command runs as, prepared before, the child after fork() only calls what is safe there
static bool drop;
static uid_t uid;
static gid_t gid;
static std::string home;
static std::vector<gid_t> groups;
static std::vector<std::string> variables;
static std::vector<char*> environment;

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
  int null;
  struct sigaction ignore {};
  struct sigaction standard {};
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

  if (!drop)
  {
    result = posix_spawn(&process, "/bin/sh", &actions, &attributes, arguments, environ);
  }
  else if ((process = fork()) == 0)
  {
    // The same as the spawn above, then the user of sudo; whatever fails, the command does not run as root
    standard.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &standard, nullptr);
    sigprocmask(SIG_SETMASK, &signals, nullptr);
    setpgid(0, 0);
    dup2(handles[0], STDIN_FILENO);

    if ((null = open("/dev/null", O_WRONLY)) >= 0)
    {
      dup2(null, STDOUT_FILENO);
      dup2(null, STDERR_FILENO);
    }

    if ((setgroups(groups.size(), groups.data()) < 0) ||
        (setgid(gid) < 0) ||
        (setuid(uid) < 0) ||
        (setuid(0) == 0))
    {
      _exit(127);
    }

    if ((chdir(home.c_str()) < 0) &&
        (chdir("/") < 0))
    {
      // From wherever root has been, readable or not
      _exit(127);
    }

    execve("/bin/sh", arguments, environment.data());
    _exit(127);
  }
  else
  {
    result = (process < 0) ? errno : 0;
  }

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

void SetClipboardCommand(const std::string& value, uid_t user)
{
  int count;
  char buffer[4096];
  struct passwd entry;
  struct passwd* found;

  command = value;
  drop    = false;

  if ((user == 0) ||
      (command.empty()))
  {
    return;
  }

  if ((getpwuid_r(user, &entry, buffer, sizeof(buffer), &found) != 0) ||
      (found == nullptr))
  {
    // Not as root instead
    command.clear();
    return;
  }

  uid   = found->pw_uid;
  gid   = found->pw_gid;
  home  = found->pw_dir;
  count = 64;
  groups.resize(count);

  if (getgrouplist(found->pw_name, gid, groups.data(), &count) < 0)
  {
    groups.resize(count);

    if (getgrouplist(found->pw_name, gid, groups.data(), &count) < 0)
    {
      command.clear();
      return;
    }
  }

  groups.resize(count);
  variables.clear();
  environment.clear();

  for (char** item = environ; *item != nullptr; item ++)
  {
    // The ones of root's session that tell who and where give way to the user
    if ((strncmp(*item, "HOME=", 5) != 0) &&
        (strncmp(*item, "USER=", 5) != 0) &&
        (strncmp(*item, "LOGNAME=", 8) != 0))
    {
      variables.push_back(*item);
    }
  }

  variables.push_back("HOME=" + home);
  variables.push_back(std::string("USER=") + found->pw_name);
  variables.push_back(std::string("LOGNAME=") + found->pw_name);

  for (std::string& variable : variables)
  {
    environment.push_back(&variable[0]);
  }

  environment.push_back(nullptr);
  drop = true;
}

bool ReadClipboardCommand(const std::string& path, uid_t owner, std::string& value)
{
  int handle;
  FILE* file;
  char line[4096];
  struct stat status;
  std::string text;
  size_t equal;
  size_t first;
  size_t last;

  if ((path.empty()) ||
      ((handle = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) < 0))
  {
    return false;
  }

  if ((fstat(handle, &status) < 0) ||
      (!S_ISREG(status.st_mode)) ||
      (status.st_uid != owner) ||
      ((file = fdopen(handle, "r")) == nullptr))
  {
    // Of someone else, or not a file
    close(handle);
    return false;
  }

  while (fgets(line, sizeof(line), file) != nullptr)
  {
    text  = line;
    equal = text.find('=');
    first = text.find_first_not_of(" \t");

    if ((equal == std::string::npos) ||
        (first == std::string::npos) ||
        (text.substr(first, text.find_last_not_of(" \t", equal - 1) + 1 - first) != "copy"))
    {
      // Blank, a comment, or another key
      continue;
    }

    first = text.find_first_not_of(" \t", equal + 1);
    last  = text.find_last_not_of(" \t\r\n");
    value = ((first == std::string::npos) || (last < first)) ? std::string() : text.substr(first, last - first + 1);
    fclose(file);
    return true;
  }

  fclose(file);
  return false;
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
