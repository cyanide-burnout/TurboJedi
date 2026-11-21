// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// The users the service answers besides the own one and root, by the files of allow, and the check of every call

#include "ServicePrivate.h"

static std::vector<Permission> permissions;

// Who calls: the real UID the bus tells, never the name. Its own user, root, and the user of sudo of a root instance for
// as long as it lives
// Of the groups the system database tells for the user of the UID, not the ones of the process the bus may not tell
static bool IsMember(uid_t uid, gid_t gid)
{
  int count;
  char buffer[4096];
  struct passwd entry;
  struct passwd* user;
  std::vector<gid_t> groups;

  if ((getpwuid_r(uid, &entry, buffer, sizeof(buffer), &user) != 0) ||
      (user == nullptr))
  {
    return false;
  }

  count = 64;
  groups.resize(count);

  if (getgrouplist(user->pw_name, user->pw_gid, groups.data(), &count) < 0)
  {
    // count tells how many there are
    groups.resize(count);

    if (getgrouplist(user->pw_name, user->pw_gid, groups.data(), &count) < 0)
    {
      return false;
    }
  }

  groups.resize(count);
  return std::find(groups.begin(), groups.end(), gid) != groups.end();
}

// A method a permission names: one of the bus, or Invoke:plugin and Invoke:plugin.method for Invoke() of those only
static bool Permits(const Permission& permission, const char* member, const std::string& plugin, const std::string& method)
{
  if ((!permission.limited) ||
      (permission.methods.count(member) != 0))
  {
    return true;
  }

  return (strcmp(member, "Invoke") == 0) &&
         (!plugin.empty()) &&
         ((permission.methods.count("Invoke:" + plugin) != 0) ||
          (permission.methods.count("Invoke:" + plugin + "." + method) != 0));
}

// Invoke() tells its plugin and method, the files of allow may name them; ListMethods() asks it for every method as if
// Invoke() were called, the member tells which one
int Allow(sd_bus_message* message, sd_bus_error* error, const std::string& plugin, const std::string& method, const char* member)
{
  int result;
  uid_t uid;
  sd_bus_creds* creds;

  creds = nullptr;

  if (((result = sd_bus_query_sender_creds(message, SD_BUS_CREDS_EUID, &creds)) < 0) ||
      ((result = sd_bus_creds_get_euid(creds, &uid)) < 0))
  {
    sd_bus_creds_unref(creds);
    return sd_bus_error_set(error, BUS_ERROR "Denied", "the bus does not tell who calls");
  }

  sd_bus_creds_unref(creds);

  if ((uid == 0) ||
      (uid == getuid()) ||
      ((delegated) && (uid == delegate)))
  {
    return 0;
  }

  for (const Permission& permission : permissions)
  {
    if ((permission.group ? IsMember(uid, permission.id) : (permission.id == uid)) &&
        (Permits(permission, (member != nullptr) ? member : sd_bus_message_get_member(message), plugin, method)))
    {
      // A service or another user the files let in, for the methods they name
      return 0;
    }
  }

  return sd_bus_error_setf(error, BUS_ERROR "Denied", "TurboJedi %d answers its own user and root only", (int)getpid());
}

// Interface

static std::string TrimText(const std::string& text)
{
  size_t first;
  size_t last;

  first = text.find_first_not_of(" \t");
  last  = text.find_last_not_of(" \t\r\n");
  return (first == std::string::npos) ? std::string() : text.substr(first, last - first + 1);
}

// Lines user=NAME or UID, then methods=Describe,Query,... for it; a problem tells the file and the line
void ReadPermissions(const std::string& path, std::vector<std::string>& problems)
{
  int number;
  bool known;
  bool numeric;
  bool fits;
  char* end;
  char line[1024];
  FILE* file;
  size_t equal;
  size_t start;
  size_t comma;
  unsigned long id;
  std::string key;
  std::string text;
  std::string value;
  std::string method;
  std::set<std::string> names;
  struct passwd* user;
  struct group* group;

  if ((path.empty()) ||
      ((file = fopen(path.c_str(), "r")) == nullptr))
  {
    // None is the default: the own user and root
    return;
  }

  for (const sd_bus_vtable* item = Table; item->type != _SD_BUS_VTABLE_END; item ++)
  {
    // Methods there are to name
    if (item->type == _SD_BUS_VTABLE_METHOD) names.insert(item->x.method.member);
  }

  known = false;

  for (number = 1; fgets(line, sizeof(line), file) != nullptr; number ++)
  {
    text = TrimText(line);

    if (text.empty() || (text[0] == '#') || (text[0] == ';'))
    {
      // Blank line or comment
      continue;
    }

    if ((equal = text.find('=')) == std::string::npos)
    {
      problems.push_back(path + ", line " + std::to_string(number) + ": key=value is expected");
      continue;
    }

    key   = TrimText(text.substr(0, equal));
    value = TrimText(text.substr(equal + 1));

    if ((key == "user") ||
        (key == "group"))
    {
      // Digits only are a UID or a GID, anything else a name the system has
      errno   = 0;
      id      = strtoul(value.c_str(), &end, 10);
      numeric = (!value.empty()) && (value.find_first_not_of("0123456789") == std::string::npos);
      user    = ((!numeric) && (key == "user"))  ? getpwnam(value.c_str()) : nullptr;
      group   = ((!numeric) && (key == "group")) ? getgrnam(value.c_str()) : nullptr;
      fits    = (key == "user") ?
        ((id < (unsigned long)(uid_t)-1) && (id == (unsigned long)(uid_t)id)) :
        ((id < (unsigned long)(gid_t)-1) && (id == (unsigned long)(gid_t)id));
      known   = numeric ? ((errno == 0) && (*end == '\0') && fits) : ((user != nullptr) || (group != nullptr));

      if (!known)
      {
        problems.push_back(path + ", line " + std::to_string(number) + ": no " + ((key == "user") ? (numeric ? "UID " : "user ") : (numeric ? "GID " : "group ")) + value + ", its lines are left out");
        continue;
      }

      permissions.push_back({ key == "group", (user != nullptr) ? (unsigned)user->pw_uid : (group != nullptr) ? (unsigned)group->gr_gid : (unsigned)id, false, { } });
      continue;
    }

    if (key == "methods")
    {
      if (!known)
      {
        // Of no user, or of one left out
        problems.push_back(path + ", line " + std::to_string(number) + ": methods go after user or group");
        continue;
      }

      permissions.back().limited = true;

      for (start = 0; start <= value.size(); start = comma + 1)
      {
        comma  = std::min(value.find(',', start), value.size());
        method = TrimText(value.substr(start, comma - start));

        if ((names.count(method) == 0) &&
            ((method.compare(0, 7, "Invoke:") != 0) || (method.size() == 7) || (method.find_first_of(" \t") != std::string::npos)))
        {
          problems.push_back(path + ", line " + std::to_string(number) + ": no method " + method);
          continue;
        }

        permissions.back().methods.insert(method);
      }

      continue;
    }

    problems.push_back(path + ", line " + std::to_string(number) + ": unknown key " + key);
  }

  fclose(file);
}
