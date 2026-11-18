// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Plugins in Lua: the state, the files loaded, on(), the Actions menu and the Plugins menu, Invoke() of the bus

#include "ScriptPrivate.h"

#define ACTION_LIMIT       100  // Items of a menu

#define cmJournalAction    1200  // The first item of the menu, one per item

#define MENU_TIME          200     // Milliseconds the queries of all handlers of a menu may take together
#define MENU_SCAN          10000   // Entries they may look at together
#define MARKED_LIMIT       1000    // Entries of the selection a handler gets in context.marked

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

static std::vector<std::string> directories;  // In the order a file of the same name takes the place: the user last

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

// on('menu', title, func, ...): an item of the Plugins menu of the menu bar, func(context, ...) when it is chosen
static int OnMenu(lua_State* state)
{
  int top;

  top = lua_gettop(state);

  if ((lua_type(state, 1) != LUA_TSTRING) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('menu') takes a title and a function");
  }

  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  if (lua_rawlen(state, -1) >= PLUGIN_MENU_LIMIT)
  {
    return Fail(state, "the Plugins menu holds up to %d items", PLUGIN_MENU_LIMIT);
  }

  // The item keeps its values the way menu:add() does, nil in the middle included
  lua_createtable(state, top - 2, 4);
  lua_pushvalue(state, 1);
  lua_setfield(state, -2, "title");
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

// on('invoke', name, func): func(values) for Invoke() of the bus, the table it returns is the answer
static int OnInvoke(lua_State* state)
{
  if ((lua_type(state, 1) != LUA_TSTRING) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('invoke') takes a name and a function");
  }

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

// Modules of lib/ in the plugin directories, the user's first, under root only the ones root alone can change
static int Search(lua_State* state)
{
  size_t index;
  std::string name;
  std::string path;
  std::string missing;
  std::string reason;

  if (!GetString(state, 1, name))
  {
    lua_pushliteral(state, "no module name");
    return 1;
  }

  std::replace(name.begin(), name.end(), '.', '/');

  for (index = directories.size(); index > 0; index --)
  {
    path = directories[index - 1] + "/lib/" + name + ".lua";

    if (access(path.c_str(), R_OK) != 0)
    {
      missing += (missing.empty() ? "no file '" : "\n\tno file '") + path + "'";
      continue;
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

  lua_pushlstring(state, missing.data(), missing.size());
  return 1;
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

static const luaL_Reg functions[] =
{
  { "on",              Call<On,              PHASE_LOAD>   },
  { "log",             Call<LogText,         PHASE_ANY>    },
  { "append",          Call<AppendText,      PHASE_ANY>    },
  { "message",         Call<ShowMessage,     PHASE_DO>     },
  { "confirm",         Call<AskConfirm,      PHASE_DO>     },
  { "input",           Call<AskInput,        PHASE_DO>     },
  { "choose",          Call<AskChoice,       PHASE_DO>     },
  { "copy",            Call<Copy,            PHASE_DO>     },
  { "execute",         Call<Execute,         PHASE_DO>     },
  { "open",            Call<Open,            PHASE_ACTION> },
  { "find",            Call<Find,            PHASE_ACTION> },
  { "seek",            Call<Seek,            PHASE_ACTION> },
  { "query",           Call<Query,           PHASE_READ>   },
  { "mark",            Call<Mark,            PHASE_MARK>   },
  { "quote",           Call<Quote,           PHASE_ANY>    },
  { "regex",           Call<Match,           PHASE_ANY>    },
  { "matches",         Call<Matches,         PHASE_ANY>    },
  { "which",           Call<Which,           PHASE_ANY>    },
  { "utc",             Call<GetUniversal,    PHASE_ANY>    },
  { "sudo",            Call<GetSudoUser,     PHASE_ANY>    },
  { "settings",        Call<Settings,        PHASE_ANY>    },
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

// Name of a plugin by its file: 100-fail2ban.lua is fail2ban
static std::string GetPluginName(const std::string& file)
{
  size_t start;

  start = file.find_first_not_of("0123456789");
  start = ((start != std::string::npos) && (start > 0) && ((file[start] == '-') || (file[start] == '_'))) ? (start + 1) : 0;
  return file.substr(start, file.size() - 4 - start);
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
  size_t handlers;
  size_t length;
  std::string path;
  std::string reason;
  struct dirent* item;
  struct stat status;
  std::map<std::string, std::string> files;

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

  // The menu a handler gets: menu:add(title, func, ...)
  luaL_newmetatable(state, MENU);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<AddItem, PHASE_MENU>));
  lua_setfield(state, -2, "add");
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

    while ((item = readdir(folder)) != nullptr)
    {
      length = strlen(item->d_name);

      if ((length > 4) &&
          (strcmp(item->d_name + length - 4, ".lua") == 0) &&
          (item->d_name[0] != '.'))
      {
        // A later directory takes the place of the name
        files[item->d_name] = place + "/" + item->d_name;
      }
    }

    closedir(folder);
  }

  phase = PHASE_LOAD;

  for (const std::pair<const std::string, std::string>& file : files)
  {
    path = file.second;

    if (stat(path.c_str(), &status) < 0)
    {
      // A link to a plugin gone, such as one of a package removed
      errors.push_back(path + ": " + strerror(errno));
      continue;
    }

    if ((!S_ISREG(status.st_mode)) ||
        (status.st_size == 0))
    {
      // An empty file or a link to /dev/null leaves out the plugin of that name
      LogConsole(GetPluginName(file.first), "left out by " + path + ", empty", false);
      continue;
    }

    if (!CheckTrust(path, reason))
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

    if ((luaL_loadfile(state, path.c_str()) != LUA_OK) ||
        (lua_pcall(state, 0, 0, 1) != LUA_OK))
    {
      errors.push_back(GetError(state));
      lua_settop(state, 0);
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

void ShowActions(JournalView* view, const std::string& cursor, TPoint where, bool keyboard)
{
  const JournalFilter& filter = view->getFilter();
  int base;
  int count;
  int width;
  int index;
  int arguments;
  ushort command;
  TMenuItem* first;
  TMenuItem* last;
  TMenuItem* item;
  std::string title;
  std::string name;
  std::string previous;
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
  count = std::min<int>(lua_rawlen(state, base + 4), ACTION_LIMIT);

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

  first = nullptr;
  last  = nullptr;
  width = TProgram::deskTop->size.x - 8;

  for (index = 1; index <= count; index ++)
  {
    lua_rawgeti(state, base + 4, index);
    lua_getfield(state, -1, "title");
    lua_getfield(state, -2, "plugin");
    name  = lua_tostring(state, -1);
    title = Fit(lua_tostring(state, -2), width - strwidth(name) - 2);
    lua_pop(state, 3);

    if ((last != nullptr) &&
        (name != previous))
    {
      // A line between the items of two plugins
      last->next = &newLine();
      last       = last->next;
    }

    // The plugin stands where a menu has the key of an item
    item     = new TMenuItem(title.c_str(), cmJournalAction + index - 1, kbNoKey, hcNoContext, name.c_str());
    first    = (first == nullptr) ? item : first;
    previous = name;

    if (last != nullptr)
    {
      last->next = item;
    }

    last = item;
  }

  if (keyboard)
  {
    // A popup of Turbo Vision opens on no item, the first step down takes the first one
    TEvent event;

    event                 = TEvent {};
    event.what            = evKeyDown;
    event.keyDown.keyCode = kbDown;
    TProgram::application->putEvent(event);
  }

  command = popupMenu(where, *first, nullptr);

  if ((command >= cmJournalAction) &&
      (command < cmJournalAction + count))
  {
    // action(...) with the arguments given to menu:add()
    lua_rawgeti(state, base + 4, command - cmJournalAction + 1);
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

std::vector<std::pair<std::string, std::string>> ListPluginMenu(int width)
{
  std::vector<std::pair<std::string, std::string>> items;

  if (state == nullptr)
  {
    return items;
  }

  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  for (lua_Integer index = 1; index <= (lua_Integer)lua_rawlen(state, -1); index ++)
  {
    lua_rawgeti(state, -1, index);
    lua_getfield(state, -1, "title");
    lua_getfield(state, -2, "plugin");
    // A title is cut to fit the width beside its plugin
    items.emplace_back(Fit(lua_tostring(state, -2), width - strwidth(lua_tostring(state, -1)) - 2), lua_tostring(state, -1));
    lua_pop(state, 3);
  }

  lua_pop(state, 1);
  return items;
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
  lua_settop(state, base);
}

void ClosePlugins()
{
  journals.clear();
  watches.clear();
  timers.clear();
  requests.clear();
  invocations.clear();
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

int InvokePluginMethod(const std::string& name, const std::string& method, sd_bus_message* call, sd_bus_message* reply, std::string& error)
{
  int base;
  int result;
  bool done;

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

  // Trace, the function, the values of the caller as a table
  lua_replace(state, base + 2);
  lua_settop(state, base + 2);

  if ((result = PushDBusValue(state, call)) <= 0)
  {
    error = std::string("the values cannot be read: ") + strerror((result < 0) ? -result : EBADMSG);
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  phase  = PHASE_EVENT;
  plugin = name;
  done   = (lua_pcall(state, 1, 1, base + 1) == LUA_OK);
  phase  = PHASE_IDLE;

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

bool ListPluginActions(JournalView* view, const std::string& cursor, std::vector<std::pair<std::string, std::string>>& actions)
{
  int base;
  int count;
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
  count = std::min<int>(lua_rawlen(state, base + 4), ACTION_LIMIT);

  for (int index = 1; index <= count; index ++)
  {
    lua_rawgeti(state, base + 4, index);
    lua_getfield(state, -1, "title");
    lua_getfield(state, -2, "plugin");
    actions.emplace_back(lua_tostring(state, -2), lua_tostring(state, -1));
    lua_pop(state, 3);
  }

  lua_settop(state, base);
  return true;
}
