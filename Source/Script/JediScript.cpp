// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Plugins in Lua: the state and the interface of turbojedi, on(), the entry and the context of a call, the errors

#include "ScriptPrivate.h"

#define MARKED_LIMIT       1000    // Entries of the selection a handler gets in context.marked

lua_State* state;
int phase;
std::string plugin;  // Name of the plugin being loaded
JournalView* origin; // Window the menu has been opened in, for open(), find(), seek() and query(); of the watch in its callbacks
const JournalFilter* basis;  // What open() and query() of an item of the Plugins menu start from when no window is open
int64_t deadline;    // Of the queries of the handlers making the menu, shared by all of them
size_t budget;       // Entries they may look at yet
int64_t invoked;     // Milliseconds of CLOCK_MONOTONIC a method of Invoke() has until, its queries stop there; zero outside one

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

// Interface of Lua

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

void PushContext(lua_State* state, const JournalFilter& filter)
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
void PushWindowContext(lua_State* state, JournalView* view)
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

void LoadPlugins(const std::string& directory, std::vector<std::string>& errors)
{
  OpenConsole();

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

  // The plugins of the directories, the system ones first and the user's last
  LoadPluginFiles(directory, errors);
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
