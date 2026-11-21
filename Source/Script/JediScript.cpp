// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Plugins in Lua: the state, the files loaded, on(), the Actions menu and the Plugins menu, Invoke() of the bus

#include "ScriptPrivate.h"
#include "ScriptArchive.h"

#define ACTION_LIMIT       100  // Actions of a menu, the ones of its submenus among them
#define SUBMENU_DEPTH      3    // Submenus in a submenu

#define cmJournalAction    1200  // The first item of the menu, one per item

#define MENU_TIME          200     // Milliseconds the queries of all handlers of a menu may take together
#define MENU_SCAN          10000   // Entries they may look at together
#define MARKED_LIMIT       1000    // Entries of the selection a handler gets in context.marked
#define RESOURCE_LIMIT     (16 * 1024 * 1024)  // Bytes of a file resource() gives

#define HANDLERS  "TurboJedi.handlers"

#define NAMES     "TurboJedi.names"     // Of the plugin of every handler, by the same index

#define METHODS   "TurboJedi.methods"   // Tables of the functions of on('invoke') by the names of their plugins

#define MENU      "TurboJedi.menu"
#define MENUS     "TurboJedi.menus"     // Items of the Plugins menu by on('menu'): the title, the plugin, the function and its values

static const char* const SystemDirectories[] = { "/usr/share/turbojedi/plugins", "/usr/local/share/turbojedi/plugins" };

lua_State* state;
int phase;
std::string plugin;  // Name of the plugin being loaded
JournalView* origin; // Window the menu has been opened in, for open(), find(), seek() and query(); of the watch in its callbacks
const JournalFilter* basis;  // What open() and query() of an item of the Plugins menu start from when no window is open
int64_t deadline;    // Of the queries of the handlers making the menu, shared by all of them
size_t budget;       // Entries they may look at yet
int64_t invoked;     // Milliseconds of CLOCK_MONOTONIC a method of Invoke() has until, its queries stop there; zero outside one

static std::vector<std::string> directories;  // In the order a file of the same name takes the place: the user last
static std::map<std::string, std::string> folders;  // Of the plugins of a folder by their names, require() finds their modules there
static std::map<std::string, std::string> archives;  // Of the plugins of an archive by their names, for resource()
static std::map<std::pair<std::string, std::string>, PluginMethod> descriptions;  // Of on('invoke') by plugin and name
static const JournalFilter machine = { };     // The journal of this machine, of a method called without a window

int Fail(lua_State* state, const char* format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  lua_pushvfstring(state, format, arguments);
  va_end(arguments);

  return -1;
}

// Message handler of every call into Lua, the place in the plugin goes along
int Trace(lua_State* state)
{
  const char* message;

  message = lua_tostring(state, 1);

  if ((message == nullptr) &&
      (luaL_callmeta(state, 1, "__tostring")) &&
      (lua_type(state, -1) == LUA_TSTRING))
  {
    // An error object that tells itself
    return 1;
  }

  luaL_traceback(state, state, (message != nullptr) ? message : lua_pushfstring(state, "(error object is a %s value)", luaL_typename(state, 1)), 1);
  return 1;
}

std::string GetError(lua_State* state)
{
  const char* message;
  std::string text;

  message = lua_tostring(state, -1);
  text    = (message != nullptr) ? message : "(error object is not a string)";

  // Tabs of a traceback would be drawn as glyphs
  for (size_t place = text.find('\t'); place != std::string::npos; place = text.find('\t', place))
  {
    text.replace(place, 1, "  ");
  }

  return text;
}

bool GetString(lua_State* state, int index, std::string& value)
{
  size_t length;
  const char* text;

  if ((lua_type(state, index) != LUA_TSTRING) &&
      (lua_type(state, index) != LUA_TNUMBER))
  {
    return false;
  }

  text  = lua_tolstring(state, index, &length);
  value = std::string(text, length);
  return true;
}

// A list of strings: the command and its arguments, every one passed as it is
bool GetArguments(lua_State* state, int index, std::vector<std::string>& arguments)
{
  lua_Integer count;
  lua_Integer number;
  std::string value;

  if (lua_type(state, index) != LUA_TTABLE)
  {
    return false;
  }

  count = lua_rawlen(state, index);

  for (number = 1; number <= count; number ++)
  {
    lua_rawgeti(state, index, number);

    if ((!GetString(state, -1, value)) ||
        (value.find('\0') != std::string::npos))
    {
      lua_pop(state, 1);
      return false;
    }

    arguments.push_back(value);
    lua_pop(state, 1);
  }

  return count > 0;
}

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

// Interface of Lua

// on('actions', func): func(entry, menu, context) when the Actions menu is made
static int OnActions(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TFUNCTION)
  {
    return Fail(state, "on('actions') takes a function");
  }

  lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);
  lua_pushvalue(state, 1);
  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_getfield(state, LUA_REGISTRYINDEX, NAMES);
  lua_pushlstring(state, plugin.data(), plugin.size());
  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_pop(state, 2);

  return 0;
}

// on('menu', title, func, ...): an item of the Plugins menu of the menu bar, func(context, ...) when it is chosen; a
// title of a list, { 'Topics', 'New topic...' }, puts it into the submenus its first strings name
static int OnMenu(lua_State* state)
{
  int top;
  lua_Integer count;

  top = lua_gettop(state);

  if (((lua_type(state, 1) != LUA_TSTRING) &&
       (lua_type(state, 1) != LUA_TTABLE)) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('menu') takes a title and a function");
  }

  if (lua_type(state, 1) == LUA_TTABLE)
  {
    count = lua_rawlen(state, 1);

    if ((count < 1) ||
        (count > SUBMENU_DEPTH + 1))
    {
      return Fail(state, "the title of on('menu') is a list of 1 to %d strings, the submenus and the item", SUBMENU_DEPTH + 1);
    }

    for (lua_Integer number = 1; number <= count; number ++)
    {
      if ((lua_rawgeti(state, 1, number) != LUA_TSTRING) ||
          (lua_rawlen(state, -1) == 0))
      {
        return Fail(state, "the title of on('menu') is a list of strings, none of them empty");
      }

      lua_pop(state, 1);
    }
  }

  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  if (lua_rawlen(state, -1) >= PLUGIN_MENU_LIMIT)
  {
    return Fail(state, "the Plugins menu holds up to %d items", PLUGIN_MENU_LIMIT);
  }

  // The item keeps its values the way menu:add() does, nil in the middle included
  lua_createtable(state, top - 2, 5);

  if (lua_type(state, 1) == LUA_TTABLE)
  {
    // The path without the title, copied: the list of the plugin may change afterwards
    count = lua_rawlen(state, 1);
    lua_rawgeti(state, 1, count);
    lua_setfield(state, -2, "title");
    lua_createtable(state, count - 1, 0);

    for (lua_Integer number = 1; number < count; number ++)
    {
      lua_rawgeti(state, 1, number);
      lua_rawseti(state, -2, number);
    }

    lua_setfield(state, -2, "path");
  }
  else
  {
    lua_pushvalue(state, 1);
    lua_setfield(state, -2, "title");
  }

  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "action");
  lua_pushinteger(state, top - 2);
  lua_setfield(state, -2, "n");
  lua_pushlstring(state, plugin.data(), plugin.size());
  lua_setfield(state, -2, "plugin");

  for (int index = 3; index <= top; index ++)
  {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, index - 2);
  }

  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_pop(state, 1);

  return 0;
}

// on('restore', func): func(context, position) for every watch of the plugin a layout keeps
static int OnRestore(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TFUNCTION)
  {
    return Fail(state, "on('restore') takes a function");
  }

  lua_getfield(state, LUA_REGISTRYINDEX, RESTORERS);
  lua_pushvalue(state, 1);
  lua_setfield(state, -2, plugin.c_str());
  lua_pop(state, 1);

  return 0;
}

// print(...) of Lua: the values as print() writes them, a tab between them, a line of the console with the plugin
static int PrintConsole(lua_State* state)
{
  int count;
  size_t length;
  std::string text;
  const char* value;

  count = lua_gettop(state);

  for (int index = 1; index <= count; index ++)
  {
    value = luaL_tolstring(state, index, &length);
    text += ((index > 1) ? "\t" : "") + std::string(value, length);
    lua_pop(state, 1);
  }

  LogConsole(plugin.empty() ? std::string("lua") : plugin, text, false);
  return 0;
}

// A text field of the options of on('invoke'): false and the error for one of another type, true for none or a text
static bool GetMethodText(lua_State* state, int index, const char* name, std::string& value, std::string& error)
{
  bool done;

  lua_pushstring(state, name);
  lua_rawget(state, index);
  done = lua_isnil(state, -1) || GetString(state, -1, value);
  lua_pop(state, 1);

  if (!done)
  {
    error = std::string(name) + " is a text";
  }

  return done;
}

// An argument of the options of on('invoke'), at the top of the stack under its name: its type, what it is, whether
// it has to be given
static bool ReadMethodArgument(lua_State* state, PluginArgument& argument, std::string& error)
{
  int index;
  int type;

  index = lua_gettop(state);

  if (lua_type(state, index) != LUA_TTABLE)
  {
    error = "argument " + argument.name + " is a table of type, description and required";
    return false;
  }

  if ((!GetMethodText(state, index, "type", argument.type, error)) ||
      (!GetMethodText(state, index, "description", argument.description, error)))
  {
    error = "argument " + argument.name + ": " + error;
    return false;
  }

  if ((argument.type != "s") && (argument.type != "u") && (argument.type != "i") && (argument.type != "t") &&
      (argument.type != "x") && (argument.type != "b") && (argument.type != "d") && (argument.type != "as"))
  {
    error = "argument " + argument.name + " has type s, u, i, t, x, b, d or as, not '" + argument.type + "'";
    return false;
  }

  lua_pushliteral(state, "required");
  type              = lua_rawget(state, index);
  argument.required = lua_toboolean(state, -1);
  lua_pop(state, 1);

  if ((type != LUA_TNIL) &&
      (type != LUA_TBOOLEAN))
  {
    error = "required of argument " + argument.name + " is true or false";
    return false;
  }

  return true;
}

// The options of on('invoke') at the index: what the method does, its effect and its arguments. A method without them
// changes the system for all a caller knows
static bool ReadMethodOptions(lua_State* state, int index, PluginMethod& method, std::string& error)
{
  method.effect   = "system";
  method.declared = false;

  if (lua_isnone(state, index) ||
      lua_isnil(state, index))
  {
    return true;
  }

  if (lua_type(state, index) != LUA_TTABLE)
  {
    error = "the options are a table of description, effect and arguments";
    return false;
  }

  if ((!GetMethodText(state, index, "description", method.description, error)) ||
      (!GetMethodText(state, index, "effect", method.effect, error)))
  {
    return false;
  }

  if ((method.effect != "none") &&
      (method.effect != "screen") &&
      (method.effect != "system"))
  {
    error = "effect is 'none', 'screen' or 'system', not '" + method.effect + "'";
    return false;
  }

  lua_pushliteral(state, "arguments");

  if (lua_rawget(state, index) == LUA_TNIL)
  {
    lua_pop(state, 1);
    return true;
  }

  if (lua_type(state, -1) != LUA_TTABLE)
  {
    lua_pop(state, 1);
    error = "arguments is a table of the arguments by their names";
    return false;
  }

  // Its arguments are checked now, an empty table as well: such a method takes no values
  method.declared = true;

  for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1))
  {
    PluginArgument argument;

    if (lua_type(state, -2) != LUA_TSTRING)
    {
      lua_pop(state, 3);
      error = "the arguments are named by texts";
      return false;
    }

    argument.name     = lua_tostring(state, -2);
    argument.required = false;

    if (argument.name.empty() ||
        (argument.name[0] == '_'))
    {
      // The keys of TurboJedi, _window of the window a method runs in
      lua_pop(state, 3);
      error = "argument '" + argument.name + "': a name beginning with _ is of TurboJedi, an empty one of nothing";
      return false;
    }

    if (!ReadMethodArgument(state, argument, error))
    {
      lua_pop(state, 3);
      return false;
    }

    method.arguments.push_back(argument);
  }

  lua_pop(state, 1);

  // In the order of their names, a table of Lua has none of its own
  std::sort(method.arguments.begin(), method.arguments.end(), [] (const PluginArgument& one, const PluginArgument& other)
  {
    return one.name < other.name;
  });

  return true;
}

// Methods of a plugin that has failed to load: it offers nothing, as if it had none
static void ForgetPluginMethods(const std::string& name)
{
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);
  lua_pushnil(state);
  lua_setfield(state, -2, name.c_str());
  lua_pop(state, 1);

  for (std::map<std::pair<std::string, std::string>, PluginMethod>::iterator item = descriptions.begin(); item != descriptions.end(); )
  {
    item = (item->first.first == name) ? descriptions.erase(item) : std::next(item);
  }
}

// on('invoke', name, func, options): func(values, context) for Invoke() of the bus, the table it returns is the
// answer; the options tell what it does
static int OnInvoke(lua_State* state)
{
  std::string error;
  PluginMethod method;

  if ((lua_type(state, 1) != LUA_TSTRING) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('invoke') takes a name, a function and its options");
  }

  method.plugin = plugin;
  method.method = lua_tostring(state, 1);

  if (!ReadMethodOptions(state, 3, method, error))
  {
    return Fail(state, "on('invoke') of %s: %s", method.method.c_str(), error.c_str());
  }

  descriptions[{ plugin, method.method }] = method;
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);

  if (lua_getfield(state, -1, plugin.c_str()) != LUA_TTABLE)
  {
    // The first method of the plugin
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, -3, plugin.c_str());
  }

  lua_pushvalue(state, 2);
  lua_setfield(state, -2, lua_tostring(state, 1));
  lua_pop(state, 2);

  return 0;
}

// on(event, ...): what the plugin is called for, an event of the Actions menu, of the Plugins menu, of a layout or of the
// bus
static int On(lua_State* state)
{
  std::string event;

  if (GetString(state, 1, event))
  {
    lua_remove(state, 1);

    if (event == "actions") return OnActions(state);
    if (event == "menu")    return OnMenu(state);
    if (event == "restore") return OnRestore(state);
    if (event == "invoke")  return OnInvoke(state);
  }

  return Fail(state, "on() takes an event: actions, menu, restore or invoke");
}

static int AddItem(lua_State* state)
{
  int index;
  int top;

  top = lua_gettop(state);

  if (lua_type(state, 1) != LUA_TTABLE)  return Fail(state, "menu:add() is called with a colon");
  if (lua_type(state, 2) != LUA_TSTRING) return Fail(state, "menu:add() takes a title");
  if (lua_type(state, 3) != LUA_TFUNCTION) return Fail(state, "menu:add() takes a function after the title");

  // The item keeps its arguments the way table.pack() does, nil in the middle included
  lua_createtable(state, top - 3, 3);
  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "title");
  lua_pushvalue(state, 3);
  lua_setfield(state, -2, "action");
  lua_pushinteger(state, top - 3);
  lua_setfield(state, -2, "n");

  for (index = 4; index <= top; index ++)
  {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, index - 3);
  }

  lua_rawseti(state, 1, lua_rawlen(state, 1) + 1);
  return 0;
}

static int AddSubmenu(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TTABLE)  return Fail(state, "menu:sub() is called with a colon");
  if (lua_type(state, 2) != LUA_TSTRING) return Fail(state, "menu:sub() takes a title");

  // The item opens a menu of its own, items are added to it as to the menu; one with none is not shown
  lua_createtable(state, 0, 2);
  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "title");
  lua_newtable(state);
  luaL_setmetatable(state, MENU);
  lua_pushvalue(state, -1);
  lua_setfield(state, -3, "sub");
  lua_insert(state, -2);
  lua_rawseti(state, 1, lua_rawlen(state, 1) + 1);
  return 1;
}

// A field of an options table without its metamethods, false when it is not there or not a string or a number
bool GetOption(lua_State* state, int index, const char* name, std::string& value)
{
  bool found;

  if (lua_type(state, index) != LUA_TTABLE)
  {
    return false;
  }

  lua_pushstring(state, name);
  lua_rawget(state, index);
  found = GetString(state, -1, value);
  lua_pop(state, 1);
  return found;
}

bool GetFlag(lua_State* state, int index, const char* name)
{
  bool flag;

  if (lua_type(state, index) != LUA_TTABLE)
  {
    return false;
  }

  lua_pushstring(state, name);
  lua_rawget(state, index);
  flag = lua_toboolean(state, -1);
  lua_pop(state, 1);
  return flag;
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
static int GetResource(lua_State* state)
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

// A callback with the values of the plugin that go to it after the ones of its event, from the function at an index and
// the values from another one on: { func, value, ..., n = count } in the registry
int KeepCallback(lua_State* state, int function, int first)
{
  int top;
  int count;

  top   = lua_gettop(state);
  count = 1;

  lua_createtable(state, std::max(0, top - first + 1) + 1, 1);
  lua_pushvalue(state, function);
  lua_rawseti(state, -2, 1);

  for (int index = first; index <= top; index ++)
  {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, ++ count);
  }

  lua_pushinteger(state, count);
  lua_setfield(state, -2, "n");
  return luaL_ref(state, LUA_REGISTRYINDEX);
}

// A handle of a watch or a timer: a table with its number and the methods of its kind
void PushHandle(lua_State* state, int number, const char* kind)
{
  lua_createtable(state, 0, 1);
  lua_pushinteger(state, number);
  lua_setfield(state, -2, "id");
  luaL_setmetatable(state, kind);
}

int GetHandle(lua_State* state)
{
  int number;

  if (lua_type(state, 1) != LUA_TTABLE)
  {
    return 0;
  }

  lua_pushliteral(state, "id");
  lua_rawget(state, 1);
  number = lua_tointeger(state, -1);
  lua_pop(state, 1);
  return number;
}

// Milliseconds a method of Invoke() has left of the time of its call, nil outside one: queries take what is left on
// their own, a plugin weighs the rest of its work by it
static int GetLeft(lua_State* state)
{
  if (invoked == 0) lua_pushnil(state);
  else              lua_pushinteger(state, std::max<int64_t>(0, invoked - GetQueryMilliseconds()));

  return 1;
}

static const luaL_Reg functions[] =
{
  { "on",              Call<On,              PHASE_LOAD>   },
  { "log",             Call<LogText,         PHASE_ANY>    },
  { "append",          Call<AppendText,      PHASE_ANY>    },
  { "message",         Call<ShowMessage,     PHASE_DO>     },
  { "confirm",         Call<AskConfirm,      PHASE_DO>     },
  { "input",           Call<AskInput,        PHASE_DO>     },
  { "choose",          Call<AskChoice,       PHASE_DO>     },
  { "popup",           Call<AskPopup,        PHASE_DO>     },
  { "copy",            Call<Copy,            PHASE_DO>     },
  { "execute",         Call<Execute,         PHASE_DO>     },
  { "open",            Call<Open,            PHASE_ACTION> },
  { "find",            Call<Find,            PHASE_ACTION> },
  { "seek",            Call<Seek,            PHASE_ACTION> },
  { "query",           Call<Query,           PHASE_READ>   },
  { "unique",          Call<Unique,          PHASE_FIELD>  },
  { "mark",            Call<Mark,            PHASE_MARK>   },
  { "quote",           Call<Quote,           PHASE_ANY>    },
  { "regex",           Call<Match,           PHASE_ANY>    },
  { "matches",         Call<Matches,         PHASE_ANY>    },
  { "which",           Call<Which,           PHASE_ANY>    },
  { "left",            Call<GetLeft,         PHASE_ANY>    },
  { "utc",             Call<GetUniversal,    PHASE_ANY>    },
  { "sudo",            Call<GetSudoUser,     PHASE_ANY>    },
  { "settings",        Call<Settings,        PHASE_ANY>    },
  { "where",           Call<Where,           PHASE_ANY>    },
  { "resource",        Call<GetResource,     PHASE_ANY>    },
  { "date",            Call<FormatDate,      PHASE_ANY>    },
  { "poll",            Call<AddWatch,        PHASE_WATCH>  },
  { "timeout",         Call<AddTimer,        PHASE_WATCH>  },
  { "fetch",           Call<Fetch,           PHASE_WATCH>  },
  { "dbus",            Call<CallDBus,        PHASE_WATCH | PHASE_MENU> },
  { "services",        Call<ListServices,    PHASE_WATCH | PHASE_MENU> },
  { "watch",           Call<AddJournalWatch, PHASE_WATCH>  },
  { nullptr,           nullptr                             }
};

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

// Entry and context of a handler

void PushEntry(lua_State* state, const std::vector<std::pair<std::string, std::string>>& fields)
{
  int type;

  lua_createtable(state, 0, fields.size());

  for (const std::pair<std::string, std::string>& field : fields)
  {
    if ((field.first == "__REALTIME_TIMESTAMP") ||
        (field.first == "__MONOTONIC_TIMESTAMP"))
    {
      // Microseconds fit an integer of Lua 5.4
      lua_pushinteger(state, strtoull(field.second.c_str(), nullptr, 10));
      lua_setfield(state, -2, field.first.c_str());
      continue;
    }

    type = lua_getfield(state, -1, field.first.c_str());

    if (type == LUA_TNIL)
    {
      // The usual case, a field once
      lua_pop(state, 1);
      lua_pushlstring(state, field.second.data(), field.second.size());
      lua_setfield(state, -2, field.first.c_str());
      continue;
    }

    if (type == LUA_TSTRING)
    {
      // A repeated field becomes a list of its values in their order
      lua_createtable(state, 2, 0);
      lua_insert(state, -2);
      lua_rawseti(state, -2, 1);
      lua_pushvalue(state, -1);
      lua_setfield(state, -3, field.first.c_str());
    }

    lua_pushlstring(state, field.second.data(), field.second.size());
    lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
    lua_pop(state, 1);
  }
}

static void PushContext(lua_State* state, const JournalFilter& filter)
{
  lua_createtable(state, 0, 2);

  if (!filter.directory.empty())
  {
    lua_pushliteral(state, "directory");
    lua_setfield(state, -2, "source");
    lua_pushlstring(state, filter.directory.data(), filter.directory.size());
    lua_setfield(state, -2, "directory");
    return;
  }

  // This machine, or every machine the journal has
  lua_pushstring(state, filter.merge ? "all" : "local");
  lua_setfield(state, -2, "source");
}

// The context of a window: where its journal comes from, and the selection and the marks beside the entry of the cursor,
// the entries in context.marked, how many there are in context.selected, as the list may be cut
static void PushWindowContext(lua_State* state, JournalView* view)
{
  const JournalFilter& filter = view->getFilter();
  size_t total;
  std::vector<std::string> cursors;
  std::vector<std::vector<std::pair<std::string, std::string>>> records;

  PushContext(state, filter);
  lua_pushboolean(state, view->markedOnly());
  lua_setfield(state, -2, "marked_only");
  cursors = view->pickedCursors(MARKED_LIMIT, total);

  if (!cursors.empty())
  {
    records = ReadJournalRecords(filter, cursors);
    lua_createtable(state, records.size(), 0);

    for (size_t number = 0; number < records.size(); number ++)
    {
      PushEntry(state, records[number]);
      lua_rawseti(state, -2, number + 1);
    }

    lua_setfield(state, -2, "marked");
    lua_pushinteger(state, total);
    lua_setfield(state, -2, "selected");
  }
}

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

  LoadArchiveModules(state, archive, "?.lua;?/init.lua", name + ".", path);
  result = LoadArchiveFile(state, archive, index, path);

  // Only read, nothing to write back
  zip_discard(archive);
  return result;
}

// The title fits the screen, cut by whole characters
std::string Fit(std::string title, int width)
{
  size_t last;

  while ((strwidth(title) > width) &&
         (!title.empty()))
  {
    // The last character goes whole, from its lead byte on: the bytes before it are of the one before
    last = title.size() - 1;

    while ((last > 0) &&
           ((static_cast<unsigned char>(title[last]) & 0xc0) == 0x80))
    {
      last --;
    }

    title.erase(last);
  }

  return title;
}

void ShowErrors(const std::vector<std::string>& errors)
{
  std::string text;

  for (const std::string& error : errors)
  {
    text += (text.empty() ? "" : "\n\n") + error;
    LogConsole("plugins", error, true);
  }

  ShowActionText("Plugins", text);
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

void LoadPlugins(const std::string& directory, std::vector<std::string>& errors)
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

  OpenConsole();

  for (const char* system : SystemDirectories)
  {
    directories.push_back(system);
  }

  if (!directory.empty())
  {
    directories.push_back(directory);
  }

  if ((state = luaL_newstate()) == nullptr)
  {
    errors.push_back("Lua cannot start, there is no memory for it");
    return;
  }

  luaL_openlibs(state);

  // JSON of lua-cjson and SQLite of lsqlite3 built in, the modules a plugin requires are these whether the distribution
  // has them or not
  luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_pushcfunction(state, luaopen_cjson);
  lua_setfield(state, -2, "cjson");
  lua_pushcfunction(state, luaopen_cjson_safe);
  lua_setfield(state, -2, "cjson.safe");
  lua_pushcfunction(state, luaopen_lsqlite3);
  lua_setfield(state, -2, "lsqlite3");
  lua_pop(state, 1);

  // print() of the plugins goes to the console, the screen belongs to Turbo Vision
  lua_pushcfunction(state, PrintConsole);
  lua_setglobal(state, "print");

  // turbojedi.version as git tells it: v0.15, v0.15-3-gabcdef of a build after it, or unknown
  luaL_newlib(state, functions);
  lua_pushliteral(state, VERSION);
  lua_setfield(state, -2, "version");
  lua_setglobal(state, "turbojedi");

  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, HANDLERS);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, NAMES);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, RESTORERS);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, METHODS);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, ACTIONS);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, MENUS);

  // The menu a handler gets: menu:add(title, func, ...), menu:sub(title) gives a submenu of the same kind
  luaL_newmetatable(state, MENU);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<AddItem, PHASE_MENU>));
  lua_setfield(state, -2, "add");
  lua_pushcfunction(state, (Call<AddSubmenu, PHASE_MENU>));
  lua_setfield(state, -2, "sub");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  // watch:update(mode), watch:remove(), timer:cancel()
  luaL_newmetatable(state, POLLER);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<UpdateWatch, PHASE_ANY>));
  lua_setfield(state, -2, "update");
  lua_pushcfunction(state, (Call<RemoveWatch, PHASE_ANY>));
  lua_setfield(state, -2, "remove");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  luaL_newmetatable(state, TIMER);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<CancelTimer, PHASE_ANY>));
  lua_setfield(state, -2, "cancel");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  // watch:status(text), watch:add(text, entry, options), watch:delete(line), watch:clear(), watch:remove(), watch:follow() of watch()
  luaL_newmetatable(state, WATCHER);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<SetWatchStatus, PHASE_ANY>));
  lua_setfield(state, -2, "status");
  lua_pushcfunction(state, (Call<AddWatchLine, PHASE_ANY>));
  lua_setfield(state, -2, "add");
  lua_pushcfunction(state, (Call<DeleteWatchLine, PHASE_ANY>));
  lua_setfield(state, -2, "delete");
  lua_pushcfunction(state, (Call<ClearWatchLines, PHASE_ANY>));
  lua_setfield(state, -2, "clear");
  lua_pushcfunction(state, (Call<SetWatchContext, PHASE_ANY>));
  lua_setfield(state, -2, "context");
  lua_pushcfunction(state, (Call<RemoveJournalWatch, PHASE_ANY>));
  lua_setfield(state, -2, "remove");
  lua_pushcfunction(state, (Call<FollowWatch, PHASE_WATCH>));
  lua_setfield(state, -2, "follow");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  // request:cancel() of fetch()
  luaL_newmetatable(state, FETCH);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<CancelRequest, PHASE_ANY>));
  lua_setfield(state, -2, "cancel");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  // call:cancel() of dbus()
  luaL_newmetatable(state, DBUS);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<CancelInvocation, PHASE_ANY>));
  lua_setfield(state, -2, "cancel");
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

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
      folders.erase(plugin);
      archives.erase(plugin);
      continue;
    }

    lua_settop(state, 0);
    LogConsole(plugin, DescribeLoaded(path, handlers), false);
  }

  phase = PHASE_IDLE;
}

// The items the handlers make for an entry of a window: on the stack from the base on Trace, the entry, the context, the
// items of all handlers, the handlers and their names; the errors of the handlers that fail go to the errors
static void CollectActions(JournalView* view, const std::vector<std::pair<std::string, std::string>>& fields, std::vector<std::string>& errors)
{
  int base;
  int count;
  int index;
  std::string title;

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);                   // base + 1
  PushEntry(state, fields);                          // base + 2
  PushWindowContext(state, view);                    // base + 3
  lua_newtable(state);                               // base + 4, items of every handler
  lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);  // base + 5
  lua_getfield(state, LUA_REGISTRYINDEX, NAMES);     // base + 6

  phase    = PHASE_MENU;
  origin   = view;
  deadline = GetQueryMilliseconds() + MENU_TIME;
  budget   = MENU_SCAN;
  count    = lua_rawlen(state, base + 5);

  for (index = 1; index <= count; index ++)
  {
    // handler(entry, menu, context), the items of a handler that fails go along with it
    lua_rawgeti(state, base + 5, index);
    lua_newtable(state);
    luaL_setmetatable(state, MENU);
    lua_pushvalue(state, -1);
    lua_insert(state, -3);
    lua_pushvalue(state, base + 2);
    lua_insert(state, -2);
    lua_pushvalue(state, base + 3);

    if (lua_pcall(state, 3, 0, base + 1) != LUA_OK)
    {
      title = GetError(state);
      lua_rawgeti(state, base + 6, index);
      errors.push_back(std::string(lua_tostring(state, -1)) + ": " + title);
      lua_settop(state, base + 6);
      continue;
    }

    for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, -1); number ++)
    {
      // Every item tells its plugin, two plugins may offer items of the same title
      lua_rawgeti(state, -1, number);
      lua_rawgeti(state, base + 6, index);
      lua_setfield(state, -2, "plugin");
      lua_rawseti(state, base + 4, lua_rawlen(state, base + 4) + 1);
    }

    lua_settop(state, base + 6);
  }

  phase  = PHASE_IDLE;
  origin = nullptr;
}

// The menu with one line that tells why there is nothing to choose, instead of a dialog with OK: Esc, Enter, a click or
// the key that has opened it closes it, as any menu. The line is an item as any other, readable on the bar of the
// cursor, its command does nothing
static void ShowActionNote(TPoint where, const std::string& text)
{
  popupMenu(where, *new TMenuItem(Fit(text, TProgram::deskTop->size.x - 8).c_str(), cmJournalAction, kbNoKey, hcNoContext), nullptr);
}

// An item of the Actions menu as it is shown: an action, with its index in the flat list of the actions from 1, or a
// submenu with its items
struct ActionNode
{
  std::string title;
  std::string plugin;
  int leaf;
  std::vector<ActionNode> children;
};

// The items of the list at the index of the stack: the actions go into the flat list at the index flat, each with its
// plugin, up to ACTION_LIMIT of them; a submenu without an action in it, or deeper than SUBMENU_DEPTH, is left out
static void WalkActions(int list, int flat, const std::string& owner, int depth, std::vector<ActionNode>& nodes)
{
  int top;
  ActionNode node;

  for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, list); number ++)
  {
    if (lua_rawlen(state, flat) >= ACTION_LIMIT)
    {
      break;
    }

    top = lua_gettop(state);
    lua_rawgeti(state, list, number);
    lua_getfield(state, top + 1, "title");
    lua_getfield(state, top + 1, "plugin");

    node          = ActionNode {};
    node.title    = lua_tostring(state, top + 2);
    node.plugin   = owner.empty() ? lua_tostring(state, top + 3) : owner;
    node.leaf     = 0;

    if (lua_getfield(state, top + 1, "sub") == LUA_TTABLE)
    {
      if (depth < SUBMENU_DEPTH)
      {
        WalkActions(top + 4, flat, node.plugin, depth + 1, node.children);
      }

      if (!node.children.empty())
      {
        nodes.push_back(node);
      }
    }
    else
    {
      // Run as an action of its plugin, the one of the item on top
      lua_pushstring(state, node.plugin.c_str());
      lua_setfield(state, top + 1, "plugin");
      lua_pushvalue(state, top + 1);
      lua_rawseti(state, flat, lua_rawlen(state, flat) + 1);
      node.leaf = lua_rawlen(state, flat);
      nodes.push_back(node);
    }

    lua_settop(state, top);
  }
}

// The items of the menu, the command of an action the first one and its index, a line between the items of two plugins
// on top, the plugin standing where a menu has the key of an item; in a submenu the plugin is the one of the item it
// opens from. A box of Turbo Vision shows ► in the place of the plugin of an item with a submenu, so on top the plugin
// goes into its title, its end where the plugins of the other items end, and theirs leave room for ► after them
static TMenuItem* MakeActionItems(const std::vector<ActionNode>& nodes, int width, bool top, ushort command)
{
  int room;
  TMenuItem* first;
  TMenuItem* last;
  TMenuItem* item;
  std::string title;
  std::string previous;
  std::vector<std::string> titles;

  first = nullptr;
  last  = nullptr;
  room  = 10;  // The width of a box, as Turbo Vision makes it: the title of an item after 3 columns, its key or ► and 3 more

  for (const ActionNode& node : nodes)
  {
    titles.push_back(Fit(node.title, width - (top ? strwidth(node.plugin) + 5 : (node.leaf > 0) ? 0 : 3)));

    if (top)
    {
      // An item: its title, 2 columns, its plugin and 3 more, and 6 of the box; one with a submenu: its title, at
      // least 2 columns, its plugin and 9 of the box
      room = std::max<int>(room, cstrlen(titles.back()) + strwidth(node.plugin) + 11);
    }
  }

  for (size_t index = 0; index < nodes.size(); index ++)
  {
    const ActionNode& node = nodes[index];

    if ((top) &&
        (last != nullptr) &&
        (node.plugin != previous))
    {
      last->next = &newLine();
      last       = last->next;
    }

    if (node.leaf > 0)
    {
      // The plugin 3 columns short of where a menu ends a key, where ► stands
      title = top ? node.plugin + "   " : std::string();
      item  = new TMenuItem(titles[index].c_str(), command + node.leaf - 1, kbNoKey, hcNoContext, top ? title.c_str() : nullptr);
    }
    else
    {
      title = titles[index];

      if (top)
      {
        // Padded to end its plugin 9 columns short of the end of the box
        title += std::string(std::max<int>(2, room - 9 - cstrlen(titles[index]) - strwidth(node.plugin)), ' ') + node.plugin;
      }

      item = new TMenuItem(title.c_str(), kbNoKey, new TMenu(*MakeActionItems(node.children, width, false, command)), hcNoContext);
    }

    first    = (first == nullptr) ? item : first;
    previous = node.plugin;

    if (last != nullptr)
    {
      last->next = item;
    }

    last = item;
  }

  return first;
}

void ShowActions(JournalView* view, const std::string& cursor, TPoint where, bool keyboard)
{
  const JournalFilter& filter = view->getFilter();
  int base;
  int count;
  int index;
  int arguments;
  ushort command;
  std::string name;
  std::vector<ActionNode> nodes;
  std::vector<std::string> errors;
  std::vector<std::pair<std::string, std::string>> fields;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice: an action runs, its dialog keeps the windows going
    return;
  }

  if ((cursor.empty()) ||
      ((fields = ReadJournalFields(filter, cursor)).empty()))
  {
    // Rotated away meanwhile, or the journal cannot be opened
    ShowActionNote(where, DescribeJournalLoss(filter));
    return;
  }

  base = lua_gettop(state);
  CollectActions(view, fields, errors);
  lua_newtable(state);  // base + 7, the actions in the order of the menu, the ones of submenus among them
  WalkActions(base + 4, base + 7, "", 0, nodes);
  count = lua_rawlen(state, base + 7);

  if (!errors.empty())
  {
    ShowErrors(errors);
  }

  if (count == 0)
  {
    if (errors.empty())
    {
      ShowActionNote(where, "No actions for this entry");
    }

    lua_settop(state, base);
    return;
  }

  command = ShowActionMenu(where, *MakeActionItems(nodes, TProgram::deskTop->size.x - 8, true, cmJournalAction), "", keyboard);

  if ((command >= cmJournalAction) &&
      (command < cmJournalAction + count))
  {
    // action(...) with the arguments given to menu:add()
    lua_rawgeti(state, base + 7, command - cmJournalAction + 1);
    lua_getfield(state, -1, "plugin");
    name = lua_tostring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "action");
    lua_getfield(state, -2, "n");
    arguments = lua_tointeger(state, -1);
    lua_pop(state, 1);

    for (index = 1; index <= arguments; index ++)
    {
      lua_rawgeti(state, -1 - index, index);
    }

    phase  = PHASE_ACTION;
    origin = view;
    plugin = name;  // Watches and timers the action sets are of its plugin

    if (lua_pcall(state, arguments, 0, base + 1) != LUA_OK)
    {
      phase  = PHASE_IDLE;
      origin = nullptr;
      ShowErrors({ name + ": " + GetError(state) });
    }

    phase  = PHASE_IDLE;
    origin = nullptr;
  }

  lua_settop(state, base);
}

TMenuItem* MakePluginMenu(int width)
{
  int top;
  size_t place;
  std::string title;
  std::string name;
  std::vector<ActionNode> nodes;
  std::vector<ActionNode>* level;

  if (state == nullptr)
  {
    return nullptr;
  }

  top = lua_gettop(state);
  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  for (lua_Integer index = 1; index <= (lua_Integer)lua_rawlen(state, top + 1); index ++)
  {
    lua_rawgeti(state, top + 1, index);
    lua_getfield(state, top + 2, "plugin");
    name  = lua_tostring(state, -1);
    level = &nodes;

    if (lua_getfield(state, top + 2, "path") == LUA_TTABLE)
    {
      for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, top + 4); number ++)
      {
        // Into the submenu of that title of the plugin, made by its first item
        lua_rawgeti(state, top + 4, number);
        title = lua_tostring(state, -1);
        lua_pop(state, 1);

        for (place = 0; place < level->size(); place ++)
        {
          if (((*level)[place].leaf == 0) &&
              ((*level)[place].plugin == name) &&
              ((*level)[place].title == title))
          {
            break;
          }
        }

        if (place == level->size())
        {
          level->push_back(ActionNode { title, name, 0, { } });
        }

        level = &(*level)[place].children;
      }
    }

    lua_getfield(state, top + 2, "title");
    level->push_back(ActionNode { lua_tostring(state, -1), name, (int)index, { } });
    lua_settop(state, top + 1);
  }

  lua_settop(state, top);
  return nodes.empty() ? nullptr : MakeActionItems(nodes, width, true, cmJournalMenu);
}

void RunPluginMenu(int index, JournalView* view, const JournalFilter& filter)
{
  int base;
  int arguments;
  std::string name;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice: an action runs, its dialog keeps the windows going
    return;
  }

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);                // base + 1
  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);  // base + 2

  if (lua_rawgeti(state, base + 2, index + 1) != LUA_TTABLE)
  {
    lua_settop(state, base);
    return;
  }

  // func(context, ...) with the values given to on('menu') after it
  lua_getfield(state, base + 3, "plugin");
  name = lua_tostring(state, -1);
  lua_pop(state, 1);
  lua_getfield(state, base + 3, "action");

  if (view != nullptr) PushWindowContext(state, view);
  else                 PushContext(state, filter);

  lua_getfield(state, base + 3, "n");
  arguments = lua_tointeger(state, -1);
  lua_pop(state, 1);

  for (int number = 1; number <= arguments; number ++)
  {
    lua_rawgeti(state, base + 3, number);
  }

  phase  = PHASE_ACTION;
  origin = view;
  basis  = &filter;
  plugin = name;  // Watches and timers the action sets are of its plugin

  if (lua_pcall(state, arguments + 1, 0, base + 1) != LUA_OK)
  {
    phase  = PHASE_IDLE;
    origin = nullptr;
    ShowErrors({ name + ": " + GetError(state) });
  }

  phase  = PHASE_IDLE;
  origin = nullptr;
  basis  = nullptr;  // The filter is of the caller, gone once it returns
  lua_settop(state, base);
}

void ClosePlugins()
{
  journals.clear();
  watches.clear();
  timers.clear();
  requests.clear();
  invocations.clear();
  descriptions.clear();
  CloseFetches();
  CloseDBus();

  if (state != nullptr)
  {
    lua_close(state);
    state = nullptr;
  }

  expressions.clear();
}

bool IsPluginIdle()
{
  return (state != nullptr) && (phase == PHASE_IDLE);
}

std::vector<const PluginMethod*> ListPluginMethods()
{
  std::vector<const PluginMethod*> list;

  for (const std::pair<const std::pair<std::string, std::string>, PluginMethod>& item : descriptions)
  {
    list.push_back(&item.second);
  }

  return list;
}

const PluginMethod* FindPluginMethod(const std::string& name, const std::string& method)
{
  std::map<std::pair<std::string, std::string>, PluginMethod>::const_iterator item;

  item = descriptions.find({ name, method });
  return (item != descriptions.end()) ? &item->second : nullptr;
}

int InvokePluginMethod(const std::string& name, const std::string& method, sd_bus_message* call, sd_bus_message* reply, JournalView* view, int64_t deadline, std::string& error)
{
  int base;
  int result;
  bool done;
  int64_t late;

  if (!IsPluginIdle())
  {
    // The bus waits for it, this is not reached
    error = "Lua is busy";
    return PLUGIN_METHOD_FAILED;
  }

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);

  if ((lua_getfield(state, -1, name.c_str()) != LUA_TTABLE) ||
      (lua_getfield(state, -1, method.c_str()) != LUA_TFUNCTION))
  {
    lua_settop(state, base);
    return PLUGIN_METHOD_NONE;
  }

  // Trace, the function, the values of the caller as a table, the context of the window
  lua_replace(state, base + 2);
  lua_settop(state, base + 2);

  if ((result = PushDBusValue(state, call)) <= 0)
  {
    error = std::string("the values cannot be read: ") + strerror((result < 0) ? -result : EBADMSG);
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  for (lua_pushnil(state); lua_next(state, base + 3) != 0; )
  {
    // The keys of TurboJedi are not of the method, the bus has taken them
    lua_pop(state, 1);

    if ((lua_type(state, -1) == LUA_TSTRING) &&
        (lua_tostring(state, -1)[0] == '_'))
    {
      lua_pushvalue(state, -1);
      lua_pushnil(state);
      lua_rawset(state, base + 3);
    }
  }

  if (view != nullptr) PushWindowContext(state, view);
  else                 PushContext(state, machine);

  // As an action of the window: its journal is the one queries, marks and watches take
  phase   = PHASE_ACTION;
  plugin  = name;
  origin  = view;
  basis   = (view != nullptr) ? &view->getFilter() : &machine;
  invoked = deadline;
  done    = (lua_pcall(state, 2, 1, base + 1) == LUA_OK);
  phase   = PHASE_IDLE;
  origin  = nullptr;
  basis   = nullptr;
  invoked = 0;

  if ((late = GetQueryMilliseconds() - deadline) > 0)
  {
    // Past the 30 seconds of the call: a caller that keeps to them has given up, the answer may go nowhere
    LogConsole(name, "method " + method + " has answered " + std::to_string((late + 999) / 1000) + " s past the 30 s of its call", true);
  }

  if (!done)
  {
    error = GetError(state);
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  if (lua_isnil(state, -1))
  {
    // No answer is an empty one
    lua_pop(state, 1);
    lua_newtable(state);
  }

  if (!AppendValue(state, lua_gettop(state), "a{sv}", reply, error))
  {
    error = "the answer is not a{sv}: " + error;
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  lua_settop(state, base);
  return PLUGIN_METHOD_DONE;
}

// The actions of the nodes and of their submenus, each with the titles of the submenus above it
static void FlattenActions(const std::vector<ActionNode>& nodes, std::vector<std::string>& path, std::vector<PluginAction>& actions)
{
  for (const ActionNode& node : nodes)
  {
    if (node.leaf > 0)
    {
      actions.push_back({ node.title, node.plugin, path });
      continue;
    }

    path.push_back(node.title);
    FlattenActions(node.children, path, actions);
    path.pop_back();
  }
}

bool ListPluginActions(JournalView* view, const std::string& cursor, std::vector<PluginAction>& actions)
{
  int base;
  std::vector<ActionNode> nodes;
  std::vector<std::string> path;
  std::vector<std::string> errors;
  std::vector<std::pair<std::string, std::string>> fields;

  actions.clear();

  if ((!IsPluginIdle()) ||
      (cursor.empty()) ||
      ((fields = ReadJournalFields(view->getFilter(), cursor)).empty()))
  {
    // Lua runs something else, or the entry is not in the journal anymore
    return false;
  }

  // As the menu is made, nothing is run: a handler only adds items, the errors of a failing one stay with the user
  base = lua_gettop(state);
  CollectActions(view, fields, errors);
  lua_newtable(state);
  WalkActions(base + 4, base + 7, "", 0, nodes);
  FlattenActions(nodes, path, actions);
  lua_settop(state, base);
  return true;
}
