// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JediSettings.h"

#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <vector>

// The user the files are written as, prepared before, the child after fork() only calls what is safe there
static bool drop;
static bool lost;
static uid_t uid;
static gid_t gid;
static std::vector<gid_t> groups;

void SetSettingsUser(uid_t user)
{
  int count;
  char buffer[4096];
  struct passwd entry;
  struct passwd* found;

  drop = (user != 0);
  lost = false;

  if (!drop)
  {
    return;
  }

  if ((getpwuid_r(user, &entry, buffer, sizeof(buffer), &found) != 0) ||
      (found == nullptr))
  {
    // Not as root instead
    lost = true;
    return;
  }

  uid   = found->pw_uid;
  gid   = found->pw_gid;
  count = 64;
  groups.resize(count);

  if (getgrouplist(found->pw_name, gid, groups.data(), &count) < 0)
  {
    groups.resize(count);

    if (getgrouplist(found->pw_name, gid, groups.data(), &count) < 0)
    {
      lost = true;
      return;
    }
  }

  groups.resize(count);
}

FILE* OpenSettings(const std::string& path)
{
  int handle;
  FILE* file;
  struct stat status;

  if ((path.empty()) ||
      (lost))
  {
    return nullptr;
  }

  if (!drop)
  {
    // Of the process itself, as it has always been
    return fopen(path.c_str(), "r");
  }

  // Root reads a file of the user: a link may point to a dotfile of theirs, not to a file of someone else, and a pipe
  // or a device does not hold the start
  if ((handle = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
  {
    return nullptr;
  }

  if ((fstat(handle, &status) < 0) ||
      (!S_ISREG(status.st_mode)) ||
      (status.st_uid != uid) ||
      ((file = fdopen(handle, "r")) == nullptr))
  {
    close(handle);
    return nullptr;
  }

  return file;
}

// Allocates nothing, a child after fork() calls it
static bool WriteFile(const std::string& path, const std::string& text, const std::vector<std::string>& folders)
{
  int handle;
  size_t offset;
  ssize_t length;

  for (const std::string& folder : folders)
  {
    // Like mkdir -p, existing ones fail harmlessly
    mkdir(folder.c_str(), 0755);
  }

  if ((handle = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)) < 0)
  {
    return false;
  }

  for (offset = 0; offset < text.size(); offset += length)
  {
    while (((length = write(handle, text.data() + offset, text.size() - offset)) < 0) && (errno == EINTR));

    if (length <= 0)
    {
      close(handle);
      return false;
    }
  }

  return close(handle) == 0;
}

bool SaveSettings(const std::string& path, const std::string& text)
{
  int status = -1;
  pid_t process;
  std::vector<std::string> folders;
  std::string::size_type position;

  if ((path.empty()) ||
      (lost))
  {
    // Kept for this run only
    return false;
  }

  if (!drop)
  {
    return WriteFile(path, text, folders);
  }

  for (position = path.find('/', 1); position != std::string::npos; position = path.find('/', position + 1))
  {
    // The directories of the user may not be there yet, named before fork()
    folders.push_back(path.substr(0, position));
  }

  if ((process = fork()) == 0)
  {
    // The user of sudo writes into the home of their own, root writes nothing there
    if ((setgroups(groups.size(), groups.data()) < 0) ||
        (setgid(gid) < 0) ||
        (setuid(uid) < 0) ||
        (setuid(0) == 0))
    {
      _exit(1);
    }

    _exit(WriteFile(path, text, folders) ? 0 : 1);
  }

  if (process < 0)
  {
    return false;
  }

  while ((waitpid(process, &status, 0) < 0) && (errno == EINTR));

  return (status >= 0) && WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}
