// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Plugins of a file, a folder or an archive: found in the plugin directories and loaded, the modules they require and
// resource()

#include "ScriptPrivate.h"
#include "ScriptArchive.h"

#define RESOURCE_LIMIT     (16 * 1024 * 1024)  // Bytes of a file resource() gives
#define ARCHIVE_LIMIT      (64 * 1024 * 1024)  // Bytes of the files of an archive of a plugin, unpacked, its modules compiled at once

static const char* const SystemDirectories[] = { "/usr/share/turbojedi/plugins", "/usr/local/share/turbojedi/plugins" };

static std::vector<std::string> directories;  // In the order a file of the same name takes the place: the user last
static std::map<std::string, std::string> folders;  // Of the plugins of a folder by their names, require() finds their modules there
static std::map<std::string, std::string> archives;  // Of the plugins of an archive by their names, for resource()

// Root runs only what root alone can change: the file and every directory up to /, links resolved. The group may write
// when it is root or staff, which Debian gives /usr/local so that its members install there: root runs what they put
// to /usr/local/bin anyway
static bool CheckTrust(const std::string& path, std::string& reason)
{
  gid_t staff;
  size_t slash;
  std::string part;
  struct group* group;
  struct stat status;
  char resolved[PATH_MAX];

  if (geteuid() != 0)
  {
    // A user runs what is the user's anyway
    return true;
  }

  group = getgrnam("staff");
  staff = (group != nullptr) ? group->gr_gid : 0;

  if (realpath(path.c_str(), resolved) == nullptr)
  {
    reason = path + ": " + strerror(errno);
    return false;
  }

  part = resolved;

  for (;;)
  {
    if (stat(part.c_str(), &status) < 0)
    {
      reason = part + ": " + strerror(errno);
      return false;
    }

    if ((status.st_uid != 0) ||
        (status.st_mode & S_IWOTH) ||
        ((status.st_mode & S_IWGRP) && (status.st_gid != 0) && (status.st_gid != staff)))
    {
      reason = path + " is left out: root runs a plugin only when nobody but root, or the group root or staff, can change " + part;
      return false;
    }

    if (part == "/")
    {
      return true;
    }

    slash = part.rfind('/');
    part  = (slash == 0) ? std::string("/") : part.substr(0, slash);
  }
}

// A module of a file as a searcher gives it, the loader and the path, or why root does not take it; 0 and the file told
// as missing when it is not there
static int FindModule(lua_State* state, const std::string& path, std::string& missing)
{
  std::string reason;

  if (access(path.c_str(), R_OK) != 0)
  {
    missing += (missing.empty() ? "no file '" : "\n\tno file '") + path + "'";
    return 0;
  }

  if (!CheckTrust(path, reason))
  {
    lua_pushlstring(state, reason.data(), reason.size());
    return 1;
  }

  if (luaL_loadfile(state, path.c_str()) != LUA_OK)
  {
    return Fail(state, "error loading module '%s' from file '%s':\n\t%s", lua_tostring(state, 1), path.c_str(), lua_tostring(state, -1));
  }

  lua_pushlstring(state, path.data(), path.size());
  return 2;
}

// Modules of the plugins of a folder, bookmarks.base of 010-bookmarks/base.lua, then the ones of lib/ in the plugin
// directories, the user's first; under root only the ones root alone can change. The modules of the plugins of an archive
// are in package.preload already
static int Search(lua_State* state)
{
  int result;
  size_t dot;
  size_t index;
  std::string name;
  std::string file;
  std::string missing;
  std::map<std::string, std::string>::const_iterator folder;

  if (!GetString(state, 1, name))
  {
    lua_pushliteral(state, "no module name");
    return 1;
  }

  if (((dot = name.find('.')) != std::string::npos) &&
      ((folder = folders.find(name.substr(0, dot))) != folders.end()) &&
      ((file = name.substr(dot + 1)) != "init"))
  {
    // init.lua is the plugin itself, no module of it
    std::replace(file.begin(), file.end(), '.', '/');

    if (((result = FindModule(state, folder->second + "/" + file + ".lua", missing)) != 0) ||
        ((result = FindModule(state, folder->second + "/" + file + "/init.lua", missing)) != 0))
    {
      return result;
    }
  }

  std::replace(name.begin(), name.end(), '.', '/');

  for (index = directories.size(); index > 0; index --)
  {
    if ((result = FindModule(state, directories[index - 1] + "/lib/" + name + ".lua", missing)) != 0)
    {
      return result;
    }
  }

  lua_pushlstring(state, missing.data(), missing.size());
  return 1;
}

// A name of a file in a plugin: relative, its parts neither empty nor . or .., so that it stays inside
static bool CheckResourceName(const std::string& name)
{
  size_t start;
  size_t end;
  std::string part;

  if (name.empty() ||
      (name.size() > PATH_MAX) ||
      (name.find('\0') != std::string::npos))
  {
    return false;
  }

  for (start = 0; start <= name.size(); start = end + 1)
  {
    end  = std::min(name.find('/', start), name.size());
    part = name.substr(start, end - start);

    if (part.empty() ||
        (part == ".") ||
        (part == ".."))
    {
      return false;
    }
  }

  return true;
}

// A file of a folder whole, at most RESOURCE_LIMIT bytes
static bool ReadResourceFile(const std::string& path, std::string& text, std::string& reason)
{
  FILE* file;
  size_t length;
  struct stat status;
  char buffer[4096];

  if (((file = fopen(path.c_str(), "rb")) == nullptr) ||
      (fstat(fileno(file), &status) < 0) ||
      (!S_ISREG(status.st_mode)))
  {
    reason = path + ": " + ((file != nullptr) ? "not a file" : strerror(errno));
    if (file != nullptr) fclose(file);
    return false;
  }

  if (status.st_size > RESOURCE_LIMIT)
  {
    reason = path + " is larger than resource() gives, 16 MiB";
    fclose(file);
    return false;
  }

  // A file that grows while it is read stops at the limit as well
  while (((length = fread(buffer, 1, sizeof(buffer), file)) > 0) &&
         (text.size() + length <= RESOURCE_LIMIT))
  {
    text.append(buffer, length);
  }

  if ((length > 0) ||
      (ferror(file)))
  {
    // Larger than the limit by now, or a failure to read: nothing of it is given as if it were whole
    reason = path + ((length > 0) ? std::string(" is larger than resource() gives, 16 MiB") : (": " + std::string(strerror(errno))));
    text.clear();
    fclose(file);
    return false;
  }

  fclose(file);
  return true;
}

// A file of an archive whole, at most RESOURCE_LIMIT bytes
static bool ReadResourceArchive(const std::string& path, const std::string& name, std::string& text, std::string& reason)
{
  int code;
  zip_t* archive;
  zip_file_t* file;
  zip_error_t error;
  zip_int64_t index;
  zip_int64_t length;
  zip_stat_t status;
  char buffer[4096];

  if ((archive = zip_open(path.c_str(), ZIP_RDONLY, &code)) == nullptr)
  {
    zip_error_init_with_code(&error, code);
    reason = path + ": " + zip_error_strerror(&error);
    zip_error_fini(&error);
    return false;
  }

  if (((index = zip_name_locate(archive, name.c_str(), 0)) < 0) ||
      (zip_stat_index(archive, index, 0, &status) < 0))
  {
    reason = path + " has no " + name;
    zip_discard(archive);
    return false;
  }

  if ((status.valid & ZIP_STAT_SIZE) &&
      (status.size > RESOURCE_LIMIT))
  {
    reason = name + " of " + path + " is larger than resource() gives, 16 MiB";
    zip_discard(archive);
    return false;
  }

  if ((file = zip_fopen_index(archive, index, 0)) == nullptr)
  {
    reason = path + ": " + zip_strerror(archive);
    zip_discard(archive);
    return false;
  }

  while (((length = zip_fread(file, buffer, sizeof(buffer))) > 0) &&
         (text.size() + length <= RESOURCE_LIMIT))
  {
    text.append(buffer, length);
  }

  if (length != 0)
  {
    reason = name + " of " + path + ((length < 0) ? (std::string(": ") + zip_file_strerror(file)) : std::string(" is larger than resource() gives, 16 MiB"));
    text.clear();
  }

  zip_fclose(file);
  zip_discard(archive);
  return length == 0;
}

// resource(name): a file of the plugin whose code calls it, of its folder or its archive, as text or nil and why. The
// plugin is told by the file of the function that calls, not by the one whose action runs: a function of Arrakis that
// the list of the bookmarks calls reads the files of Arrakis
int GetResource(lua_State* state)
{
  lua_Debug caller;
  std::string name;
  std::string text;
  std::string place;
  std::string reason;
  std::string source;

  if ((!GetString(state, 1, name)) ||
      (!CheckResourceName(name)))
  {
    return Fail(state, "resource() takes a name of a file of the plugin, relative and without . or .. in it, prompts/system.txt for one");
  }

  if ((lua_getstack(state, 1, &caller) == 0) ||
      (lua_getinfo(state, "S", &caller) == 0) ||
      (caller.source == nullptr) ||
      (caller.source[0] != '@'))
  {
    lua_pushnil(state);
    lua_pushliteral(state, "resource() is called by code that is not of a file of a plugin");
    return 2;
  }

  source = caller.source + 1;

  for (const std::pair<const std::string, std::string>& folder : folders)
  {
    if (source.compare(0, folder.second.size() + 1, folder.second + "/") == 0)
    {
      place = folder.second + "/" + name;

      if ((!CheckTrust(place, reason)) ||
          (!ReadResourceFile(place, text, reason)))
      {
        lua_pushnil(state);
        lua_pushlstring(state, reason.data(), reason.size());
        return 2;
      }

      lua_pushlstring(state, text.data(), text.size());
      return 1;
    }
  }

  for (const std::pair<const std::string, std::string>& archive : archives)
  {
    if (source.compare(0, archive.second.size() + 1, archive.second + ":") == 0)
    {
      if ((!CheckTrust(archive.second, reason)) ||
          (!ReadResourceArchive(archive.second, name, text, reason)))
      {
        lua_pushnil(state);
        lua_pushlstring(state, reason.data(), reason.size());
        return 2;
      }

      lua_pushlstring(state, text.data(), text.size());
      return 1;
    }
  }

  lua_pushnil(state);
  lua_pushfstring(state, "%s is of no plugin of a folder or an archive, a plugin of one file has no resources", source.c_str());
  return 2;
}

// Paths relative to the current directory are dropped, the modules of the plugins are looked for first
static const char Restriction[] =
  "local search = ...\n"
  "local function strip(paths)\n"
  "  local kept = { }\n"
  "  for path in paths:gmatch('[^;]+') do\n"
  "    if not path:match('^%./') then kept[#kept + 1] = path end\n"
  "  end\n"
  "  return table.concat(kept, ';')\n"
  "end\n"
  "package.path  = strip(package.path)\n"
  "package.cpath = strip(package.cpath)\n"
  "table.insert(package.searchers, 2, search)\n";

static bool HasSuffix(const std::string& name, const char* suffix)
{
  size_t length;

  length = strlen(suffix);
  return (name.size() > length) && (name.compare(name.size() - length, length, suffix) == 0);
}

// Name of a plugin by its file or folder: 100-fail2ban.lua is fail2ban, 010-bookmarks/ and 010-bookmarks.zip are bookmarks
static std::string GetPluginName(const std::string& file)
{
  size_t end;
  size_t start;

  end   = file.size() - ((HasSuffix(file, ".lua") || HasSuffix(file, ".zip")) ? 4 : 0);
  start = file.find_first_not_of("0123456789");
  start = ((start != std::string::npos) && (start > 0) && (start < end) && ((file[start] == '-') || (file[start] == '_'))) ? (start + 1) : 0;
  return file.substr(start, end - start);
}

// A plugin of an archive: its modules into package.preload, and init.lua at the root of it compiled onto the stack, or
// the error; nothing of the archive is kept
static int LoadPluginArchive(const std::string& path, const std::string& name)
{
  int code;
  int result;
  zip_t* archive;
  zip_error_t error;
  zip_int64_t index;
  zip_int64_t count;
  zip_int64_t number;
  zip_int64_t left;
  zip_uint64_t total;
  zip_stat_t status;

  if ((archive = zip_open(path.c_str(), ZIP_RDONLY, &code)) == nullptr)
  {
    zip_error_init_with_code(&error, code);
    lua_pushfstring(state, "%s: %s", path.c_str(), zip_error_strerror(&error));
    zip_error_fini(&error);
    return LUA_ERRFILE;
  }

  if ((index = zip_name_locate(archive, "init.lua", 0)) < 0)
  {
    lua_pushfstring(state, "%s: an archive of a plugin has init.lua at its root, this one has not, the plugin is left out", path.c_str());
    zip_discard(archive);
    return LUA_ERRFILE;
  }

  // The modules are compiled at once: an archive that unpacks to far more than any plugin is, damaged or made so, is not
  // read whole into memory
  count = zip_get_num_entries(archive, 0);
  total = 0;

  for (number = 0; number < count; number ++)
  {
    if ((zip_stat_index(archive, number, 0, &status) == 0) &&
        (status.valid & ZIP_STAT_SIZE))
    {
      total += status.size;
    }
  }

  if (total > ARCHIVE_LIMIT)
  {
    lua_pushfstring(state, "%s: an archive of a plugin unpacks to %d MiB, more than the 64 MiB taken, the plugin is left out", path.c_str(), (int)(total >> 20));
    zip_discard(archive);
    return LUA_ERRFILE;
  }

  // The sizes an archive tells may be wrong: the bytes read are counted as well, past the limit the plugin is left out
  left = ARCHIVE_LIMIT;
  LoadArchiveModules(state, archive, "?.lua;?/init.lua", name + ".", path, &left);

  if (left < 0)
  {
    lua_pushfstring(state, "%s: an archive of a plugin unpacks to more than the 64 MiB taken, the plugin is left out", path.c_str());
    zip_discard(archive);
    return LUA_ERRFILE;
  }

  result = LoadArchiveFile(state, archive, index, path, &left);

  // Only read, nothing to write back
  zip_discard(archive);
  return result;
}

// The modules of a plugin that has failed to load, compiled from its archive or loaded by its init.lua so far: neither
// another plugin nor a later require() gets them
static void ForgetPluginModules(const std::string& name)
{
  std::string prefix;
  std::vector<std::string> keys;

  prefix = name + ".";
  lua_getglobal(state, "package");

  for (const char* table : { "preload", "loaded" })
  {
    keys.clear();
    lua_getfield(state, -1, table);

    for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1))
    {
      if ((lua_type(state, -2) == LUA_TSTRING) &&
          (strncmp(lua_tostring(state, -2), prefix.c_str(), prefix.size()) == 0))
      {
        keys.push_back(lua_tostring(state, -2));
      }
    }

    for (const std::string& key : keys)
    {
      lua_pushnil(state);
      lua_setfield(state, -2, key.c_str());
    }

    lua_pop(state, 1);
  }

  lua_pop(state, 1);
}

// What a plugin has set while it has been loaded, for the console: nothing set means it has nothing to offer here
static std::string DescribeLoaded(const std::string& path, size_t before)
{
  size_t methods;
  size_t watching;
  size_t handlers;
  std::string text;

  lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);
  handlers = lua_rawlen(state, -1) - before;
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);
  methods = 0;

  if (lua_getfield(state, -1, plugin.c_str()) == LUA_TTABLE)
  {
    for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1)) methods ++;
  }

  lua_settop(state, 0);
  watching = std::count_if(journals.begin(), journals.end(), [] (const std::pair<const int, std::unique_ptr<JournalWatch>>& watch)
  {
    return watch.second->plugin == plugin;
  });

  if (handlers > 0) text += ", " + std::to_string(handlers) + ((handlers == 1) ? " handler of the menu" : " handlers of the menu");
  if (watching > 0) text += ", " + std::to_string(watching) + ((watching == 1) ? " watch" : " watches");
  if (methods > 0)  text += ", " + std::to_string(methods) + ((methods == 1) ? " method" : " methods") + " for Invoke()";

  return "loaded from " + path + (text.empty() ? ", it has set nothing: nothing to offer here" : text);
}

// The plugins of the system directories and of the one of the user, a later one taking the place of a plugin of the same
// name, loaded in the order of the names of their files
void LoadPluginFiles(const std::string& directory, std::vector<std::string>& errors)
{
  DIR* folder;
  int result;
  size_t handlers;
  std::string main;
  std::string path;
  std::string reason;
  struct dirent* item;
  struct stat status;
  std::vector<std::string> names;
  std::map<std::string, std::string> here;
  std::map<std::string, std::string> files;
  std::map<std::string, std::pair<std::string, std::string>> chosen;

  for (const char* system : SystemDirectories)
  {
    directories.push_back(system);
  }

  if (!directory.empty())
  {
    directories.push_back(directory);
  }

  if ((luaL_loadstring(state, Restriction) != LUA_OK) ||
      (lua_pushcfunction(state, (Call<Search, PHASE_ANY>)), lua_pcall(state, 1, 0, 0) != LUA_OK))
  {
    errors.push_back(GetError(state));
    lua_settop(state, 0);
  }

  for (const std::string& place : directories)
  {
    if ((folder = opendir(place.c_str())) == nullptr)
    {
      // None there
      continue;
    }

    names.clear();
    here.clear();

    while ((item = readdir(folder)) != nullptr)
    {
      // A file, an archive or a folder of a plugin; lib/ holds the modules shared by all of them
      if ((item->d_name[0] != '.') &&
          ((HasSuffix(item->d_name, ".lua")) ||
           (HasSuffix(item->d_name, ".zip")) ||
           ((strcmp(item->d_name, "lib") != 0) &&
            (stat((place + "/" + item->d_name).c_str(), &status) == 0) &&
            (S_ISDIR(status.st_mode)))))
      {
        names.push_back(item->d_name);
      }
    }

    closedir(folder);
    std::sort(names.begin(), names.end());

    for (const std::string& name : names)
    {
      plugin = GetPluginName(name);

      if (here.count(plugin) != 0)
      {
        // Two numbers of one plugin side by side, an install over an older one that has left the old name: the later
        // one by name is taken
        errors.push_back(place + ": " + here[plugin] + " and " + name + " are both the plugin " + plugin + ", " + name + " is taken");
      }

      // A later directory takes the place of the plugin of that name, whatever its number: the own version of a user or
      // an empty file that leaves it out holds when the package numbers its plugins anew
      here[plugin]   = name;
      chosen[plugin] = { name, place + "/" + name };
    }
  }

  // In the order of the names of their files, the numbers tell it
  for (const std::pair<const std::string, std::pair<std::string, std::string>>& taken : chosen)
  {
    files[taken.second.first] = taken.second.second;
  }

  phase = PHASE_LOAD;

  for (const std::pair<const std::string, std::string>& file : files)
  {
    path = file.second;
    main = path;

    if (stat(path.c_str(), &status) < 0)
    {
      // A link to a plugin gone, such as one of a package removed
      errors.push_back(path + ": " + strerror(errno));
      continue;
    }

    if (S_ISDIR(status.st_mode) &&
        (!HasSuffix(file.first, ".lua")) &&
        (!HasSuffix(file.first, ".zip")))
    {
      // A plugin of a folder runs its init.lua, its modules lie beside it
      main = path + "/init.lua";

      if (stat(main.c_str(), &status) < 0)
      {
        errors.push_back(path + ": a folder of a plugin has init.lua, this one has not, the plugin is left out");
        continue;
      }
    }
    else if ((!S_ISREG(status.st_mode)) ||
             (status.st_size == 0))
    {
      // An empty file or archive, or a link to /dev/null, leaves out the plugin of that name
      LogConsole(GetPluginName(file.first), "left out by " + path + ", empty", false);
      continue;
    }

    if (!CheckTrust(main, reason))
    {
      errors.push_back(reason);
      continue;
    }

    plugin = GetPluginName(file.first);

    if (plugin == "turbojedi")
    {
      // The name of the console and of TurboJedi itself
      errors.push_back(path + ": turbojedi is the name of TurboJedi itself, the plugin is left out");
      continue;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);
    handlers = lua_rawlen(state, -1);
    lua_settop(state, 0);
    lua_pushcfunction(state, Trace);

    if (HasSuffix(file.first, ".zip"))
    {
      archives[plugin] = path;
      result           = LoadPluginArchive(path, plugin);
    }
    else
    {
      // The modules of a folder are found from now on, init.lua may require them already
      if (main != path) folders[plugin] = path;
      result = luaL_loadfile(state, main.c_str());
    }

    // The name of the plugin goes to its file as ..., the way require() gives a module its name: init.lua requires its
    // modules by it, a folder renamed keeps working
    if (result == LUA_OK)
    {
      lua_pushlstring(state, plugin.data(), plugin.size());
    }

    if ((result != LUA_OK) ||
        (lua_pcall(state, 1, 0, 1) != LUA_OK))
    {
      errors.push_back(GetError(state));
      lua_settop(state, 0);
      ForgetPluginMethods(plugin);
      ForgetPluginModules(plugin);
      folders.erase(plugin);
      archives.erase(plugin);
      continue;
    }

    lua_settop(state, 0);
    LogConsole(plugin, DescribeLoaded(path, handlers), false);
  }

  phase = PHASE_IDLE;
}
