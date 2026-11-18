// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Plugins in Lua: the state, the files loaded, on(), the Actions menu and the Plugins menu, Invoke() of the bus

#include "ScriptPrivate.h"

#define ACTION_LIMIT       100  // Items of a menu
#define CAPTURE_TIMEOUT    30   // Seconds execute() of mode capture waits by default
#define EXPRESSION_LIMIT   64   // Compiled patterns kept for the next calls

#define cmJournalAction    1200  // The first item of the menu, one per item

#define QUERY_LIMIT        100     // Entries query() gives by default
#define QUERY_SCAN         100000  // Entries it looks at by default
#define QUERY_TIMEOUT      30      // Seconds it reads in an action by default
#define MENU_TIME          200     // Milliseconds the queries of all handlers of a menu may take together
#define MENU_SCAN          10000   // Entries they may look at together
#define MARKED_LIMIT       1000    // Entries of the selection a handler gets in context.marked

#define POLL_INTERVAL      50      // Milliseconds between the looks at the descriptors plugins watch
#define FETCH_TIMEOUT      30      // Seconds a request of fetch() may take by default

#define WATCH_READ_TIME    10      // Milliseconds all watches may read their journals for in a poll
#define WATCH_READ_LIMIT   256     // Entries a watch may look at in a poll

#define HANDLERS  "TurboJedi.handlers"
#define POLLER    "TurboJedi.poller"   // Of poll()

#define TIMER     "TurboJedi.timer"
#define FETCH     "TurboJedi.fetch"

#define NAMES     "TurboJedi.names"     // Of the plugin of every handler, by the same index

#define METHODS   "TurboJedi.methods"   // Tables of the functions of on('invoke') by the names of their plugins

#define MENU      "TurboJedi.menu"
#define MENUS     "TurboJedi.menus"     // Items of the Plugins menu by on('menu'): the title, the plugin, the function and its values

static const char* const SystemDirectories[] = { "/usr/share/turbojedi/plugins", "/usr/local/share/turbojedi/plugins" };

lua_State* state;
int phase;
std::string plugin;  // Name of the plugin being loaded
JournalView* origin; // Window the menu has been opened in, for open(), find(), seek() and query(); of the watch in its callbacks
static const JournalFilter* basis;  // What open() and query() of an item of the Plugins menu start from when no window is open
int64_t deadline;    // Of the queries of the handlers making the menu, shared by all of them
static size_t budget;       // Entries they may look at yet

static std::map<int, Watch> watches;
static std::map<int, Timer> timers;
static std::map<int, Request> requests;

int dispatching;   // Rounds of events going on, nested by the dialogs of callbacks

int serial;

static std::vector<std::string> directories;  // In the order a file of the same name takes the place: the user last
static std::map<std::string, JournalExpression> expressions;

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
static bool GetArguments(lua_State* state, int index, std::vector<std::string>& arguments)
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

static JournalExpression* GetExpression(lua_State* state, int index, int options, std::string& error)
{
  bool caseless;
  std::string key;
  std::string pattern;
  std::string flags;
  JournalExpression expression;
  std::map<std::string, JournalExpression>::iterator place;

  if (!GetString(state, index, pattern))
  {
    error = "the pattern is not a string";
    return nullptr;
  }

  GetString(state, options, flags);

  caseless = (flags.find('i') != std::string::npos);
  key      = (caseless ? "i:" : "-:") + pattern;
  place    = expressions.find(key);

  if (place != expressions.end())
  {
    return &place->second;
  }

  if (!expression.compile(pattern, caseless, &error))
  {
    return nullptr;
  }

  if (expressions.size() >= EXPRESSION_LIMIT)
  {
    // Patterns are few, made anew they cost a compilation
    expressions.clear();
  }

  return &expressions.emplace(key, std::move(expression)).first->second;
}

// Groups of a match: [0] the match, [1] and on the groups, named ones under their names as well
static void PushMatch(lua_State* state, const std::string& text, const std::vector<std::pair<size_t, size_t>>& groups, const std::vector<std::string>& names)
{
  size_t index;

  lua_createtable(state, groups.size(), names.size());

  for (index = 0; index < groups.size(); index ++)
  {
    if (groups[index].first == std::string::npos)
    {
      // Has not taken part
      continue;
    }

    lua_pushlstring(state, text.data() + groups[index].first, groups[index].second - groups[index].first);

    if ((index < names.size()) &&
        (!names[index].empty()))
    {
      lua_pushvalue(state, -1);
      lua_setfield(state, -3, names[index].c_str());
    }

    lua_rawseti(state, -2, index);
  }
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

// log(text, options): a line of the console with the plugin; { problem = true } leaves it unread
static int LogText(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "log() takes a text");
  }

  LogConsole(plugin.empty() ? std::string("lua") : plugin, text, GetFlag(state, 2, "problem"));
  return 0;
}

// append(path, text): the text at the end of a file of the process alone, made with 0600; nil and why when it is not
// one: a link, not a file, of another owner, or readable or writable by others. What a plugin keeps of the journal stays
// with its user, root included
static int AppendText(lua_State* state)
{
  int handle;
  size_t length;
  ssize_t written;
  const char* text;
  std::string path;
  std::string reason;
  struct stat status;

  if ((!GetString(state, 1, path)) ||
      (lua_type(state, 2) != LUA_TSTRING))
  {
    return Fail(state, "append() takes a path and a text");
  }

  text = lua_tolstring(state, 2, &length);

  if ((handle = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)) < 0)
  {
    reason = strerror(errno);
  }
  else if ((fstat(handle, &status) < 0) ||
           (!S_ISREG(status.st_mode)))
  {
    reason = "not a regular file";
  }
  else if (status.st_uid != geteuid())
  {
    reason = "of another owner";
  }
  else if ((status.st_mode & 077) != 0)
  {
    reason = "open to others, chmod 600 it";
  }
  else
  {
    for (size_t done = 0; done < length; )
    {
      if (((written = write(handle, text + done, length - done)) < 0) &&
          (errno == EINTR))
      {
        // A signal has come before anything has been written
        continue;
      }

      if (written <= 0)
      {
        // Nothing written would be written forever
        reason = (written < 0) ? strerror(errno) : "nothing could be written";
        break;
      }

      done += written;
    }
  }

  if (handle >= 0)
  {
    close(handle);
  }

  if (!reason.empty())
  {
    lua_pushnil(state);
    lua_pushstring(state, reason.c_str());
    return 2;
  }

  lua_pushboolean(state, 1);
  return 1;
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

// message(text, title, options): the text with OK; { input = prompt } puts a line to type a question into under it, the
// line comes back by Ask, nil by Close or an empty line
static int ShowMessage(lua_State* state)
{
  std::string text;
  std::string title;
  std::string prompt;
  std::string value;

  if ((!GetString(state, 1, text)) ||
      ((!lua_isnoneornil(state, 2)) && (!GetString(state, 2, title))))
  {
    return Fail(state, "message() takes a text and a title");
  }

  if (!lua_isnoneornil(state, 3))
  {
    if ((lua_type(state, 3) != LUA_TTABLE) ||
        (!GetOption(state, 3, "input", prompt)))
    {
      return Fail(state, "message() takes options of { input = prompt } after the title");
    }

    if ((!AskActionText(title.empty() ? "Message" : title.c_str(), text, prompt, value)) ||
        (value.find_first_not_of(" \t") == std::string::npos))
    {
      lua_pushnil(state);
      return 1;
    }

    lua_pushlstring(state, value.data(), value.size());
    return 1;
  }

  ShowActionText(title.empty() ? "Message" : title.c_str(), text);
  return 0;
}

static int AskConfirm(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "confirm() takes a text");
  }

  lua_pushboolean(state, AskActionConfirm(text));
  return 1;
}

static int AskInput(lua_State* state)
{
  std::string prompt;
  std::string value;

  if (!GetString(state, 1, prompt))
  {
    return Fail(state, "input() takes a prompt");
  }

  GetString(state, 2, value);

  if (!AskActionInput(prompt, value))
  {
    lua_pushnil(state);
    return 1;
  }

  lua_pushlstring(state, value.data(), value.size());
  return 1;
}

static int AskChoice(lua_State* state)
{
  int chosen;
  int initial;
  std::string title;
  std::string value;
  std::vector<std::string> items;

  if ((!GetString(state, 1, title)) ||
      (lua_type(state, 2) != LUA_TTABLE))
  {
    return Fail(state, "choose() takes a title and a list of items");
  }

  for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, 2); number ++)
  {
    lua_rawgeti(state, 2, number);

    if (!GetString(state, -1, value))
    {
      lua_pop(state, 1);
      return Fail(state, "items of choose() are strings");
    }

    items.push_back(value);
    lua_pop(state, 1);
  }

  if (items.empty())
  {
    // Nothing to choose from
    lua_pushnil(state);
    return 1;
  }

  initial = 0;

  if (lua_isinteger(state, 3))
  {
    // An index of the list
    initial = lua_tointeger(state, 3) - 1;
  }
  else if (GetString(state, 3, value))
  {
    // An item of it
    initial = std::find(items.begin(), items.end(), value) - items.begin();
  }

  chosen = AskActionChoice(title, items, initial);

  if (chosen < 0)
  {
    lua_pushnil(state);
    return 1;
  }

  lua_pushlstring(state, items[chosen].data(), items[chosen].size());
  lua_pushinteger(state, chosen + 1);
  return 2;
}

static int Copy(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "copy() takes a text");
  }

  // The way Ctrl-Ins copies: the command of the user, the clipboard of Turbo Vision when it fails
  CopyClipboardText(text);
  return 0;
}

static void PushOutcome(lua_State* state, const JediOutcome& outcome)
{
  lua_pushstring(state, outcome.how.c_str());

  if (outcome.how == "error") lua_pushlstring(state, outcome.text.data(), outcome.text.size());
  else if (outcome.how == "cancel") lua_pushnil(state);
  else lua_pushinteger(state, outcome.code);
}

// execute(argv, options): mode tells where the output goes and whether the plugin waits, how and code come first in
// every one, 'error' and the reason when the command has not started
static int Execute(lua_State* state)
{
  int timeout;
  std::string mode;
  std::string output;
  std::string errors;
  std::string failure;
  JediOutcome outcome;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "execute() takes a list of the command and its arguments");
  }

  if ((lua_gettop(state) >= 2) &&
      (!lua_isnil(state, 2)) &&
      (lua_type(state, 2) != LUA_TTABLE))
  {
    return Fail(state, "execute() takes a table of options after the command");
  }

  if (!GetOption(state, 2, "mode", mode))
  {
    // Nothing shown, nothing waited for
    mode = "background";
  }

  if ((mode == "background") ||
      (mode == "window"))
  {
    // Started and left: background tells only an end other than exit 0, window shows the output as it comes
    if (!((mode == "background") ? RunBackground(arguments, failure) : OpenOutput(arguments, failure)))
    {
      lua_pushliteral(state, "error");
      lua_pushlstring(state, failure.data(), failure.size());
      return 2;
    }

    lua_pushliteral(state, "started");
    return 1;
  }

  if (mode == "terminal")
  {
    PushOutcome(state, RunTerminal(arguments));
    return 2;
  }

  if (mode != "capture")
  {
    return Fail(state, "mode of execute() is background, window, terminal or capture");
  }

  timeout = CAPTURE_TIMEOUT;

  if (lua_type(state, 2) == LUA_TTABLE)
  {
    lua_pushliteral(state, "timeout");

    if (lua_rawget(state, 2) == LUA_TNUMBER)
    {
      // Seconds, zero waits for as long as it takes
      timeout = std::max<int>(0, lua_tointeger(state, -1));
    }

    lua_pop(state, 1);
  }

  outcome = CaptureCommand(arguments, timeout, output, errors);

  PushOutcome(state, outcome);

  if (outcome.how == "error") lua_pushnil(state);
  else                        lua_pushlstring(state, output.data(), output.size());

  if (errors.empty()) lua_pushnil(state);
  else                lua_pushlstring(state, errors.data(), errors.size());

  return 4;
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

// The way the text filter and Find take a text: plain or an expression, case or not, to show or to leave out
static uint16_t GetTextMode(lua_State* state, int index)
{
  return (GetFlag(state, index, "regex")   ? JOURNAL_TEXT_EXPRESSION : 0) |
         (GetFlag(state, index, "exclude") ? JOURNAL_TEXT_EXCLUDE    : 0) |
         (GetFlag(state, index, "case")    ? JOURNAL_TEXT_SENSITIVE  : 0);
}

static int GetPriority(const std::string& value)
{
  static const char* const names[] = { "emerg", "alert", "crit", "err", "warning", "notice", "info", "debug" };

  for (int level = 0; level < 8; level ++)
  {
    if ((value == names[level]) || (value == std::to_string(level)))
    {
      return level;
    }
  }

  return -1;
}

// A match FIELD=VALUE of a value or of the values of a list, checked the way the command line checks it
static bool AddMatch(JournalFilter& filter, const std::string& field, const std::string& value, std::string& error)
{
  std::string match;

  match = field + "=" + value;

  if (!CheckJournalMatch(match))
  {
    error = match + " is not a FIELD=VALUE match";
    return false;
  }

  filter.matches.push_back(match);
  return true;
}

// A filter of the options of open(), query() and watch(): the journals, columns and highlighting of a base, the window of the
// menu for one, the matches, text, times and levels of the options; false and the reason when an option is wrong
bool MakeFilter(lua_State* state, int index, const JournalFilter& base, JournalFilter& filter, std::string& error)
{
  int top;
  int level;
  uint64_t stamp;
  size_t equal;
  std::string key;
  std::string value;

  top              = lua_gettop(state);
  filter           = JournalFilter { };
  filter.directory = base.directory;
  filter.merge     = base.merge;
  filter.columns   = base.columns;
  filter.highlight = base.highlight;
  filter.levels    = 0xff;

  lua_pushliteral(state, "matches");
  lua_rawget(state, index);

  if (lua_type(state, -1) == LUA_TTABLE)
  {
    // { FIELD = value or a list of values } or { 'FIELD=VALUE', ... }, one field ORed, different ones ANDed
    for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1))
    {
      if (lua_type(state, -2) == LUA_TNUMBER)
      {
        GetString(state, -1, value);
        equal = value.find('=');

        if ((equal == std::string::npos) || !AddMatch(filter, value.substr(0, equal), value.substr(equal + 1), error))
        {
          error = error.empty() ? (value + " is not a FIELD=VALUE match") : error;
          lua_settop(state, top);
          return false;
        }

        continue;
      }

      GetString(state, -2, key);

      if (lua_type(state, -1) == LUA_TTABLE)
      {
        for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, -1); number ++)
        {
          lua_rawgeti(state, -1, number);
          GetString(state, -1, value);
          lua_pop(state, 1);

          if (!AddMatch(filter, key, value, error))
          {
            lua_settop(state, top);
            return false;
          }
        }

        continue;
      }

      if ((!GetString(state, -1, value)) ||
          (!AddMatch(filter, key, value, error)))
      {
        error = error.empty() ? ("a value of " + key + " is not a string") : error;
        lua_settop(state, top);
        return false;
      }
    }
  }

  lua_settop(state, top);

  if (GetOption(state, index, "text", filter.text))
  {
    filter.mode = GetTextMode(state, index);

    if ((filter.mode & JOURNAL_TEXT_EXPRESSION) &&
        (!JournalExpression().compile(filter.text, !(filter.mode & JOURNAL_TEXT_SENSITIVE), &error)))
    {
      return false;
    }
  }

  if ((GetOption(state, index, "since", filter.since) && !ParseJournalTime(filter.since, &stamp)) ||
      (GetOption(state, index, "until", filter.until) && !ParseJournalTime(filter.until, &stamp)))
  {
    error = "not a time TurboJedi knows, or one before 1970: " + (filter.until.empty() ? filter.since : filter.until);
    return false;
  }

  if (GetOption(state, index, "priority", value))
  {
    // Up to the level, like -p
    if ((level = GetPriority(value)) < 0)
    {
      error = "unknown priority " + value;
      return false;
    }

    filter.levels = (2 << level) - 1;
  }

  return true;
}

// What open() and query() start from: the window of the menu, or what TurboJedi has started with for an item of the
// Plugins menu when no window is open
static const JournalFilter& GetOriginFilter()
{
  return (origin != nullptr) ? origin->getFilter() : *basis;
}

// A new window like the one of the menu with the filter of the options
static int Open(lua_State* state)
{
  std::string error;
  JournalFilter filter;

  if (lua_type(state, 1) != LUA_TTABLE)
  {
    return Fail(state, "open() takes a table of options");
  }

  if (!MakeFilter(state, 1, GetOriginFilter(), filter, error))
  {
    return Fail(state, "open(): %s", error.c_str());
  }

  // The application makes the window as for a field of Details
  message(TProgram::application, evBroadcast, cmJournalOpenFilter, &filter);
  return 0;
}

// The cursor of a bound: an entry or its __CURSOR
static bool GetBound(lua_State* state, int index, const char* name, std::string& cursor)
{
  bool found;

  lua_pushstring(state, name);
  lua_rawget(state, index);

  if (lua_type(state, -1) == LUA_TTABLE)
  {
    lua_pushliteral(state, "__CURSOR");
    lua_rawget(state, -2);
    lua_replace(state, -2);
  }

  found = GetString(state, -1, cursor);
  lua_pop(state, 1);
  return found;
}

// Entries of a filter for the plugin to look into: a list of tables like entry, and why the reading has stopped before
// the journal or the range has ended. A handler shares the budget of the menu with the others, an action reads on
// with a dialog
static int Query(lua_State* state)
{
  bool older;
  int timeout;
  size_t limit;
  size_t scan;
  std::string text;
  std::string cursor;
  std::string error;
  JournalFilter filter;
  JournalQuery query;

  if (phase == PHASE_LOAD)
  {
    return Fail(state, "query() is used by a handler or an action, not while the plugin is loaded");
  }

  if (lua_type(state, 1) != LUA_TTABLE)
  {
    return Fail(state, "query() takes a table of options");
  }

  if (!MakeFilter(state, 1, GetOriginFilter(), filter, error))
  {
    return Fail(state, "query(): %s", error.c_str());
  }

  // Before an entry going older by default, after one going newer, from the newest without either
  older = true;

  if (!GetBound(state, 1, "before", cursor) &&
      GetBound(state, 1, "after", cursor))
  {
    older = false;
  }

  limit   = GetOption(state, 1, "limit", text) ? std::max<long long>(1, atoll(text.c_str())) : QUERY_LIMIT;
  scan    = GetOption(state, 1, "scan", text) ? std::max<long long>(1, atoll(text.c_str())) : QUERY_SCAN;
  timeout = GetOption(state, 1, "timeout", text) ? std::max(0, atoi(text.c_str())) : QUERY_TIMEOUT;

  if (phase == PHASE_MENU)
  {
    // Every handler takes from what the menu has left
    scan = std::min(scan, budget);
  }

  if (!query.start(filter, cursor, older, limit, scan, error))
  {
    return Fail(state, "query(): %s", error.c_str());
  }

  if (phase == PHASE_MENU)
  {
    if (!query.step(deadline))
    {
      // The menu is not kept waiting
      query.stop("time");
    }

    budget -= std::min(budget, query.scanned);

    if ((budget == 0) && (query.stopped != nullptr) && (strcmp(query.stopped, "limit") != 0))
    {
      // The menu has spent its entries rather than this query its own
      query.stopped = "scan";
    }
  }
  else
  {
    RunJournalQuery(query, timeout);
  }

  lua_createtable(state, query.records.size(), 0);

  for (size_t index = 0; index < query.records.size(); index ++)
  {
    PushEntry(state, query.records[index]);
    lua_rawseti(state, -2, index + 1);
  }

  if (query.stopped != nullptr) lua_pushstring(state, query.stopped);
  else                          lua_pushnil(state);

  return 2;
}

// Find in the window of the menu: highlighted, on into the older entries, a step of its history; nil and why when there
// is no window
static int Find(lua_State* state)
{
  std::string text;
  std::string error;
  uint16_t mode;

  if ((!GetString(state, 1, text)) ||
      (text.empty()))
  {
    return Fail(state, "find() takes a text");
  }

  mode = GetTextMode(state, 2);

  if ((mode & JOURNAL_TEXT_EXPRESSION) &&
      (!JournalExpression().compile(text, !(mode & JOURNAL_TEXT_SENSITIVE), &error)))
  {
    return Fail(state, "find(): %s", error.c_str());
  }

  if (origin == nullptr)
  {
    // An item of the Plugins menu with no window open
    lua_pushnil(state);
    lua_pushliteral(state, "no window of the journal is open");
    return 2;
  }

  // Older by default, the way the first Find goes
  lua_pushboolean(state, origin->search(text, mode & ~JOURNAL_TEXT_EXCLUDE, !GetFlag(state, 2, "newer")));
  return 1;
}

// Go to a time in the window of the menu: microseconds such as __REALTIME_TIMESTAMP, or a time as Go to time takes it;
// true, or nil and why when there is no window
static int Seek(lua_State* state)
{
  uint64_t stamp;
  std::string text;

  if (lua_isinteger(state, 1))
  {
    stamp = lua_tointeger(state, 1);
  }
  else if ((!GetString(state, 1, text)) ||
           (!ParseJournalTime(text, &stamp)))
  {
    return Fail(state, "seek() takes microseconds or a time such as 2026-10-23 19:00, -10m");
  }

  if (origin == nullptr)
  {
    lua_pushnil(state);
    lua_pushliteral(state, "no window of the journal is open");
    return 2;
  }

  origin->jump(stamp);
  lua_pushboolean(state, true);
  return 1;
}

// The cursor of an entry or a cursor itself, with the time it tells: __REALTIME_TIMESTAMP, or t= of the cursor in hex
static bool AddCursor(lua_State* state, int index, std::set<std::string>& cursors, uint64_t& oldest)
{
  bool found;
  size_t place;
  uint64_t stamp;
  std::string cursor;

  index = lua_absindex(state, index);

  if (lua_type(state, index) == LUA_TTABLE)
  {
    lua_pushliteral(state, "__CURSOR");
    lua_rawget(state, index);
    found = GetString(state, -1, cursor);
    lua_pop(state, 1);
  }
  else
  {
    found = GetString(state, index, cursor);
  }

  if (!found)
  {
    return false;
  }

  cursors.insert(cursor);
  place = cursor.find(";t=");
  stamp = (place != std::string::npos) ? strtoull(cursor.c_str() + place + 3, nullptr, 16) : 0;

  if (stamp != 0)
  {
    oldest = std::min(oldest, stamp);
  }

  return true;
}

// Marks in the window of the menu the lines of an entry, of a list of entries such as query() gives, or of cursors; in a
// callback of a watch, in the window of the watch
static int Mark(lua_State* state)
{
  bool single;
  uint64_t oldest;
  JournalWatch* watch;
  std::set<std::string> cursors;

  oldest = UINT64_MAX;

  if (lua_type(state, 1) == LUA_TTABLE)
  {
    lua_pushliteral(state, "__CURSOR");
    single = (lua_rawget(state, 1) != LUA_TNIL);
    lua_pop(state, 1);

    if (single)
    {
      AddCursor(state, 1, cursors, oldest);
    }

    for (lua_Integer number = 1; (!single) && (number <= (lua_Integer)lua_rawlen(state, 1)); number ++)
    {
      lua_rawgeti(state, 1, number);

      if (!AddCursor(state, -1, cursors, oldest))
      {
        lua_pop(state, 1);
        return Fail(state, "mark() takes entries or cursors, item %d is neither", (int)number);
      }

      lua_pop(state, 1);
    }
  }
  else if (!AddCursor(state, 1, cursors, oldest))
  {
    return Fail(state, "mark() takes an entry, a list of entries or cursors, or a cursor");
  }

  if ((origin == nullptr) &&
      (phase == PHASE_ACTION))
  {
    // An item of the Plugins menu with no window open
    lua_pushnil(state);
    lua_pushliteral(state, "no window of the journal is open");
    return 2;
  }

  if ((origin == nullptr) &&
      ((!GetFlag(state, 2, "open")) ||
       ((watch = FindJournalWatch(watching)) == nullptr) ||
       ((origin = OpenWatchWindow(watch)) == nullptr)))
  {
    // A watch without a window opens one only when asked, a refusal otherwise
    lua_pushnil(state);
    lua_pushliteral(state, "the watch has no window, mark() with { open = true } opens one");
    return 2;
  }

  lua_pushinteger(state, origin->mark(cursors, oldest, GetFlag(state, 2, "clear")));
  return 1;
}

static int Quote(lua_State* state)
{
  std::string line;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "quote() takes a list of the command and its arguments");
  }

  line = QuoteArguments(arguments);
  lua_pushlstring(state, line.data(), line.size());
  return 1;
}

static int Match(lua_State* state)
{
  std::string text;
  std::string error;
  JournalExpression* expression;
  std::vector<std::pair<size_t, size_t>> groups;

  if (!GetString(state, 1, text))
  {
    // A field the entry does not have
    lua_pushnil(state);
    return 1;
  }

  if ((expression = GetExpression(state, 2, 3, error)) == nullptr)
  {
    return Fail(state, "regex(): %s", error.c_str());
  }

  if (!expression->search(text, 0, groups))
  {
    lua_pushnil(state);
    return 1;
  }

  PushMatch(state, text, groups, expression->names());
  return 1;
}

static int NextMatch(lua_State* state)
{
  size_t offset;
  std::string text;
  std::string error;
  JournalExpression* expression;
  std::vector<std::pair<size_t, size_t>> groups;

  // Up values: the text, the pattern, the flags and where the search goes on
  if ((!GetString(state, lua_upvalueindex(1), text)) ||
      (lua_type(state, lua_upvalueindex(4)) != LUA_TNUMBER))
  {
    lua_pushnil(state);
    return 1;
  }

  offset = lua_tointeger(state, lua_upvalueindex(4));

  if ((expression = GetExpression(state, lua_upvalueindex(2), lua_upvalueindex(3), error)) == nullptr)
  {
    return Fail(state, "matches(): %s", error.c_str());
  }

  if ((offset > text.size()) ||
      (!expression->search(text, offset, groups)))
  {
    // Over, the next call says so again
    lua_pushnil(state);
    lua_replace(state, lua_upvalueindex(4));
    lua_pushnil(state);
    return 1;
  }

  lua_pushinteger(state, JournalExpression::advance(text, groups[0].first, groups[0].second));
  lua_replace(state, lua_upvalueindex(4));
  PushMatch(state, text, groups, expression->names());
  return 1;
}

static int Matches(lua_State* state)
{
  std::string error;

  if (GetExpression(state, 2, 3, error) == nullptr)
  {
    return Fail(state, "matches(): %s", error.c_str());
  }

  // Without a text the loop goes round no time
  lua_pushvalue(state, 1);
  lua_pushvalue(state, 2);
  lua_pushvalue(state, 3);
  lua_pushinteger(state, 0);
  lua_pushcclosure(state, Call<NextMatch, PHASE_ANY>, 4);
  return 1;
}

static int Which(lua_State* state)
{
  size_t start;
  size_t end;
  std::string name;
  std::string path;
  std::string list;
  struct stat status;

  if ((!GetString(state, 1, name)) ||
      (name.empty()))
  {
    return Fail(state, "which() takes the name of a program");
  }

  list = (getenv("PATH") != nullptr) ? getenv("PATH") : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

  for (start = 0; start <= list.size(); start = end + 1)
  {
    end  = std::min(list.find(':', start), list.size());
    path = (name.find('/') != std::string::npos) ? name : (((end > start) ? list.substr(start, end - start) : std::string(".")) + "/" + name);

    if ((stat(path.c_str(), &status) == 0) &&
        (S_ISREG(status.st_mode)) &&
        (access(path.c_str(), X_OK) == 0))
    {
      lua_pushlstring(state, path.data(), path.size());
      return 1;
    }

    if (name.find('/') != std::string::npos)
    {
      // A path is not looked for
      break;
    }
  }

  lua_pushnil(state);
  return 1;
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

static short GetEvents(const std::string& mode)
{
  return ((mode.find('r') != std::string::npos) ? POLLIN : 0) | ((mode.find('w') != std::string::npos) ? POLLOUT : 0);
}

// poll(handle, 'r' | 'w' | 'rw', callback, ...): the callback gets 'r', 'w', 'rw', with 'e' for an error or a hang-up
static int AddWatch(lua_State* state)
{
  int whole;
  lua_Integer handle;
  std::string mode;
  Watch watch;

  // getfd() of luasocket gives a float, a whole one is taken
  handle = lua_tointegerx(state, 1, &whole);

  if ((!whole) ||
      (handle < 0))
  {
    return Fail(state, "poll() takes a descriptor");
  }

  if ((!GetString(state, 2, mode)) ||
      ((watch.events = GetEvents(mode)) == 0))
  {
    return Fail(state, "poll() takes 'r', 'w' or 'rw' after the descriptor");
  }

  if (lua_type(state, 3) != LUA_TFUNCTION)
  {
    return Fail(state, "poll() takes a function after the events");
  }

  watch.callback = KeepCallback(state, 3, 4);
  watch.handle   = handle;
  watch.revents  = 0;
  watch.plugin   = plugin;
  watch.pending  = false;

  watches[++ serial] = watch;
  PushHandle(state, serial, POLLER);
  return 1;
}

static int UpdateWatch(lua_State* state)
{
  short events;
  std::string mode;
  std::map<int, Watch>::iterator watch;

  if ((!GetString(state, 2, mode)) ||
      ((events = GetEvents(mode)) == 0))
  {
    return Fail(state, "update() takes 'r', 'w' or 'rw'");
  }

  if ((watch = watches.find(GetHandle(state))) != watches.end())
  {
    watch->second.events = events;
  }

  return 0;
}

static void DropWatch(lua_State* state, int number)
{
  std::map<int, Watch>::iterator watch;

  if ((watch = watches.find(number)) != watches.end())
  {
    luaL_unref(state, LUA_REGISTRYINDEX, watch->second.callback);
    watches.erase(watch);
  }
}

static int RemoveWatch(lua_State* state)
{
  DropWatch(state, GetHandle(state));
  return 0;
}

// timeout(milliseconds, [periodic,] callback, ...), as poll() has the function after what it waits for: a function second
// is a timer of once
static int AddTimer(lua_State* state)
{
  int whole;
  int function;
  lua_Integer interval;
  Timer timer;

  interval = lua_tointegerx(state, 1, &whole);

  if ((!whole) ||
      (interval < 0))
  {
    return Fail(state, "timeout() takes whole milliseconds");
  }

  function = (lua_type(state, 2) == LUA_TFUNCTION) ? 2 : 3;

  if (lua_type(state, function) != LUA_TFUNCTION)
  {
    return Fail(state, "timeout() takes a function after the milliseconds, or after whether it is periodic");
  }

  timer.callback = KeepCallback(state, function, function + 1);
  timer.period   = ((function == 3) && lua_toboolean(state, 2)) ? std::max<int64_t>(1, interval) : 0;
  timer.due      = GetQueryMilliseconds() + interval;
  timer.plugin   = plugin;
  timer.pending  = false;

  timers[++ serial] = timer;
  PushHandle(state, serial, TIMER);
  return 1;
}

static void DropTimer(lua_State* state, int number)
{
  std::map<int, Timer>::iterator timer;

  if ((timer = timers.find(number)) != timers.end())
  {
    luaL_unref(state, LUA_REGISTRYINDEX, timer->second.callback);
    timers.erase(timer);
  }
}

static int CancelTimer(lua_State* state)
{
  DropTimer(state, GetHandle(state));
  return 0;
}

// fetch(url | request, callback, ...): url, method, headers, body, timeout of a request in a table
static int GetRequest(lua_State* state, int index, JediRequest& request)
{
  int whole;
  lua_Integer timeout;
  std::string name;
  std::string value;

  request.post    = false;
  request.timeout = FETCH_TIMEOUT;

  if (GetString(state, index, request.url))
  {
    // Only the address
    return 0;
  }

  if ((lua_type(state, index) != LUA_TTABLE) ||
      (!GetOption(state, index, "url", request.url)))
  {
    return Fail(state, "fetch() takes an address or a table of the request with url");
  }

  if (GetOption(state, index, "method", request.method))
  {
    for (char& character : request.method)
    {
      if (!isalpha(static_cast<unsigned char>(character)))
      {
        return Fail(state, "method of fetch() is a word such as GET or POST");
      }

      character = toupper(static_cast<unsigned char>(character));
    }
  }

  request.post = GetOption(state, index, "body", request.body) || (request.method == "POST");

  lua_pushliteral(state, "timeout");

  if (lua_rawget(state, index) != LUA_TNIL)
  {
    timeout = lua_tointegerx(state, -1, &whole);

    if ((!whole) ||
        (timeout < 0))
    {
      lua_pop(state, 1);
      return Fail(state, "timeout of fetch() is whole seconds, zero for no limit");
    }

    request.timeout = std::min<lua_Integer>(timeout, INT_MAX);
  }

  lua_pop(state, 1);
  lua_pushliteral(state, "headers");

  if (lua_rawget(state, index) == LUA_TTABLE)
  {
    lua_pushnil(state);

    while (lua_next(state, -2) != 0)
    {
      // Neither may end a line, nor a name have more than a token
      if ((lua_type(state, -2) != LUA_TSTRING) ||
          (!GetString(state, -1, value)) ||
          ((name = lua_tostring(state, -2)).empty()) ||
          (name.find_first_of(" \t\r\n:") != std::string::npos) ||
          (value.find_first_of("\r\n") != std::string::npos) ||
          (name.find('\0') != std::string::npos) ||
          (value.find('\0') != std::string::npos))
      {
        lua_pop(state, 3);
        return Fail(state, "headers of fetch() are names and their texts");
      }

      request.headers.emplace_back(name, value);
      lua_pop(state, 1);
    }
  }

  lua_pop(state, 1);

  if (request.url.find('\0') != std::string::npos)
  {
    return Fail(state, "url of fetch() is a text");
  }

  return 0;
}

// The code, the body and the headers of an answer; nil, how and the text when there is none
static int PushAnswer(lua_State* state, const JediAnswer& answer)
{
  if (answer.how != "done")
  {
    lua_pushnil(state);
    lua_pushstring(state, answer.how.c_str());
    lua_pushlstring(state, answer.text.data(), answer.text.size());
    return 3;
  }

  lua_pushinteger(state, answer.code);
  lua_pushlstring(state, answer.body.data(), answer.body.size());
  lua_createtable(state, 0, answer.headers.size());

  for (const std::pair<std::string, std::string>& header : answer.headers)
  {
    lua_pushlstring(state, header.first.data(), header.first.size());

    if (lua_rawget(state, -2) == LUA_TSTRING)
    {
      // A header told twice is one, its texts go apart by commas
      lua_pushliteral(state, ", ");
      lua_pushlstring(state, header.second.data(), header.second.size());
      lua_concat(state, 3);
    }
    else
    {
      lua_pop(state, 1);
      lua_pushlstring(state, header.second.data(), header.second.size());
    }

    lua_pushlstring(state, header.first.data(), header.first.size());
    lua_insert(state, -2);
    lua_rawset(state, -3);
  }

  return 3;
}

static int Fetch(lua_State* state)
{
  int number;
  size_t start;
  std::string host;
  std::string error;
  JediAnswer answer;
  JediRequest request;
  Request item;

  if (GetRequest(state, 1, request) < 0)
  {
    return -1;
  }

  if ((lua_type(state, 2) != LUA_TFUNCTION) &&
      (lua_type(state, 2) != LUA_TNONE) &&
      (lua_type(state, 2) != LUA_TNIL))
  {
    return Fail(state, "fetch() takes a function after the request");
  }

  if ((lua_type(state, 2) != LUA_TFUNCTION) &&
      ((phase & PHASE_DO) == 0))
  {
    return Fail(state, "fetch() without a callback waits in a dialog, it is used only by an action or a callback");
  }

  number = ++ serial;

  if (!StartFetch(number, request, error))
  {
    answer.how  = "error";
    answer.text = error;
    return PushAnswer(state, answer);
  }

  if (lua_type(state, 2) == LUA_TFUNCTION)
  {
    // The answer goes to the callback once no dialog is open
    item.callback    = KeepCallback(state, 2, 3);
    item.plugin      = plugin;
    requests[number] = item;
    PushHandle(state, number, FETCH);
    return 1;
  }

  // As execute() of mode capture waits: the dialog with Cancel comes up when the answer is late
  start = request.url.find("://");
  host  = request.url.substr((start != std::string::npos) ? (start + 3) : 0);
  host  = host.substr(0, host.find_first_of("/?#"));

  switch (RunSteps(Fit("Waiting for " + host, 46).c_str(), [number] (int64_t deadline)
  {
    int64_t now;

    PollFetches(0);

    while ((!IsFetchDone(number)) &&
           ((now = GetQueryMilliseconds()) < deadline))
    {
      PollFetches(std::min<int64_t>(deadline - now, FETCH_STEP));
    }

    return IsFetchDone(number);
  }, 0, "Waiting"))
  {
    case STEPS_DONE:
      TakeFetch(number, answer);
      break;

    default:
      CancelFetch(number);
      answer.how  = "cancel";
      answer.text = "the request has been canceled";
      break;
  }

  return PushAnswer(state, answer);
}

static void DropRequest(lua_State* state, int number)
{
  std::map<int, Request>::iterator request;

  if ((request = requests.find(number)) != requests.end())
  {
    luaL_unref(state, LUA_REGISTRYINDEX, request->second.callback);
    requests.erase(request);
  }

  CancelFetch(number);
}

static int CancelRequest(lua_State* state)
{
  DropRequest(state, GetHandle(state));
  return 0;
}

// Time

// utc(): whether the user wants the times of the watches in UTC
static int GetUniversal(lua_State* state)
{
  lua_pushboolean(state, universal);
  return 1;
}

// settings(name, text): a file of the plugin among the ones TurboJedi writes by itself, ~/.config/turbojedi/name, under
// sudo the one of the user of sudo, read and written as that user. Without the text, its text or nil; with it, the file
// replaced whole, true or nil and why
static int Settings(lua_State* state)
{
  FILE* file;
  size_t length;
  std::string name;
  std::string text;
  std::string path;
  char buffer[4096];

  if ((!GetString(state, 1, name)) ||
      (name.empty()) ||
      (name[0] == '.') ||
      (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos))
  {
    return Fail(state, "settings() takes the name of a file, letters, digits, dots, dashes and underscores");
  }

  if ((!lua_isnoneornil(state, 2)) &&
      (!GetString(state, 2, text)))
  {
    return Fail(state, "settings() takes a text after the name");
  }

  path = GetSettingsPath(name);

  if (lua_isnoneornil(state, 2))
  {
    if ((path.empty()) ||
        ((file = OpenSettings(path)) == nullptr))
    {
      lua_pushnil(state);
      return 1;
    }

    while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      text.append(buffer, length);
    }

    fclose(file);
    lua_pushlstring(state, text.data(), text.size());
    return 1;
  }

  if (!SaveSettings(path, text))
  {
    lua_pushnil(state);
    lua_pushstring(state, path.empty() ? "there is no directory of settings, HOME is not set" : (path + " cannot be written").c_str());
    return 2;
  }

  lua_pushboolean(state, true);
  return 1;
}

// sudo(): the user who has run TurboJedi as root by sudo, { name, uid, home }, nil otherwise; the system tells the home,
// the passwd file or a directory service, for a plugin to read a file of settings of that user
static int GetSudoUser(lua_State* state)
{
  char* end;
  char buffer[4096];
  const char* value;
  unsigned long uid;
  struct passwd entry;
  struct passwd* user;

  value = getenv("SUDO_UID");

  if ((getuid() != 0) ||
      (value == nullptr) ||
      (*value == '\0') ||
      (strspn(value, "0123456789") != strlen(value)))
  {
    lua_pushnil(state);
    return 1;
  }

  errno = 0;
  uid   = strtoul(value, &end, 10);

  if ((errno != 0) ||
      (uid == 0) ||
      (uid >= (unsigned long)(uid_t)-1) ||
      (getpwuid_r((uid_t)uid, &entry, buffer, sizeof(buffer), &user) != 0) ||
      (user == nullptr))
  {
    lua_pushnil(state);
    return 1;
  }

  lua_createtable(state, 0, 3);
  lua_pushstring(state, user->pw_name);
  lua_setfield(state, -2, "name");
  lua_pushinteger(state, user->pw_uid);
  lua_setfield(state, -2, "uid");
  lua_pushstring(state, user->pw_dir);
  lua_setfield(state, -2, "home");
  return 1;
}

// date(format, time): os.date() in the zone the user has chosen for the watches, ! in front of the format forces UTC
static int FormatDate(lua_State* state)
{
  int whole;
  bool utc;
  time_t moment;
  struct tm parts;
  std::string format;
  char buffer[512];

  format = "%c";

  if ((!lua_isnoneornil(state, 1)) &&
      (!GetString(state, 1, format)))
  {
    return Fail(state, "date() takes a format of strftime");
  }

  utc    = universal || ((!format.empty()) && (format[0] == '!'));
  format = ((!format.empty()) && (format[0] == '!')) ? format.substr(1) : format;
  moment = time(nullptr);

  if (!format.empty() && (format[0] == '*'))
  {
    return Fail(state, "date() takes a format of strftime, os.date() makes the table of *t");
  }

  if (!lua_isnoneornil(state, 2))
  {
    moment = lua_tointegerx(state, 2, &whole);

    if (!whole)
    {
      return Fail(state, "date() takes whole seconds after the format");
    }
  }

  if (((utc ? gmtime_r(&moment, &parts) : localtime_r(&moment, &parts)) == nullptr) ||
      ((strftime(buffer, sizeof(buffer), format.c_str(), &parts) == 0) && (!format.empty())))
  {
    return Fail(state, "date() cannot make the time of %s", format.c_str());
  }

  lua_pushstring(state, format.empty() ? "" : buffer);
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

int PollPlugins(bool paused)
{
  int64_t now;
  int64_t wait;
  int64_t end;
  bool pending;
  JournalWatch* watch;
  JournalWatchState before;
  std::vector<int> numbers;
  std::vector<struct pollfd> pollers;

  wait = -1;

  CollectJournalWatches();

  for (std::pair<const int, Watch>& watch : watches)
  {
    if (!watch.second.pending)
    {
      pollers.push_back({ watch.second.handle, watch.second.events, 0 });
      numbers.push_back(watch.first);
    }
  }

  if ((!pollers.empty()) &&
      (poll(pollers.data(), pollers.size(), 0) > 0))
  {
    for (size_t index = 0; index < pollers.size(); index ++)
    {
      if (pollers[index].revents != 0)
      {
        // Told once no dialog is open, not looked at meanwhile
        watches[numbers[index]].pending = true;
        watches[numbers[index]].revents = pollers[index].revents;
      }
    }
  }

  PollFetches(0);

  if ((!watches.empty()) ||
      (HasFetches()) ||
      (PollDBus()))
  {
    // Looked at again before long, nothing wakes the loop for them
    wait = POLL_INTERVAL;
  }

  now = GetQueryMilliseconds();

  for (std::pair<const int, Timer>& timer : timers)
  {
    if (timer.second.pending)
    {
      continue;
    }

    if (timer.second.due <= now)
    {
      timer.second.pending = true;
      continue;
    }

    wait = (wait < 0) ? (timer.second.due - now) : std::min(wait, timer.second.due - now);
  }

  // Watches of journals read in turns within a time of all of them, a dialog open pauses them
  end     = GetQueryMilliseconds() + WATCH_READ_TIME;
  pending = false;

  for (int number : RoundJournalWatches())
  {
    if ((watch = FindJournalWatch(number)) == nullptr)
    {
      continue;
    }

    before = watch->state;
    watch->poll(WATCH_READ_LIMIT, end, paused);
    changed = changed || (watch->state != before);
    pending = pending || (!watch->events.empty());

    if (watch->state == WATCH_ACTIVE)
    {
      wait = (wait < 0) ? watch->wait(POLL_INTERVAL * 20) : std::min<int64_t>(wait, watch->wait(POLL_INTERVAL * 20));
    }
  }

  if (pending && !paused)
  {
    // Their callbacks are due as soon as nothing modal is open
    wait = 0;
  }

  return (int)wait;
}

bool HasPluginEvents()
{
  for (const std::pair<const int, Watch>& watch : watches)
  {
    if (watch.second.pending) return true;
  }

  for (const std::pair<const int, Timer>& timer : timers)
  {
    if (timer.second.pending) return true;
  }

  for (const std::pair<const int, Request>& request : requests)
  {
    if (IsFetchDone(request.first)) return true;
  }

  for (const std::pair<const int, Request>& invocation : invocations)
  {
    if (IsDBusDone(invocation.first)) return true;
  }

  for (const std::pair<const int, std::unique_ptr<JournalWatch>>& watch : journals)
  {
    if ((watch.second->state == WATCH_ACTIVE) && (!watch.second->events.empty())) return true;
  }

  return false;
}

// A callback in the phase of events with the arguments push gives, its error goes to the errors with its plugin; false when
// it has failed
static bool RunCallback(int callback, const std::string& name, const std::function<int(lua_State*)>& push, std::vector<std::string>& errors)
{
  int base;
  int count;
  int extra;
  bool done;

  base = lua_gettop(state);

  // Trace, the function, the values of the event, the ones of the plugin
  lua_pushcfunction(state, Trace);
  lua_rawgeti(state, LUA_REGISTRYINDEX, callback);
  lua_rawgeti(state, base + 2, 1);

  count = push ? push(state) : 0;

  lua_getfield(state, base + 2, "n");
  extra = lua_tointeger(state, -1);
  lua_pop(state, 1);

  for (int index = 2; index <= extra; index ++)
  {
    lua_rawgeti(state, base + 2, index);
  }

  lua_remove(state, base + 2);

  phase  = PHASE_EVENT;
  plugin = name;
  done   = (lua_pcall(state, count + extra - 1, 0, base + 1) == LUA_OK);
  phase  = PHASE_IDLE;

  if (!done)
  {
    errors.push_back(name + ": " + GetError(state));
  }

  lua_settop(state, base);
  return done;
}

void DispatchPluginEvents()
{
  int callback;
  char flags[4];
  size_t length;
  std::string name;
  std::vector<int> numbers;
  std::vector<std::string> errors;
  std::map<int, Watch>::iterator watch;
  std::map<int, Timer>::iterator timer;
  std::map<int, Request>::iterator request;
  JediAnswer answer;
  sd_bus_message* reply;
  JediDBusError failure;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice
    return;
  }

  for (const std::pair<const int, Watch>& item : watches)
  {
    if (item.second.pending) numbers.push_back(item.first);
  }

  for (int number : numbers)
  {
    // A callback before may have removed it
    if ((watch = watches.find(number)) == watches.end())
    {
      continue;
    }

    watch->second.pending = false;

    if (watch->second.revents & POLLNVAL)
    {
      errors.push_back(watch->second.plugin + ": descriptor " + std::to_string(watch->second.handle) + " has been closed without remove()");
      DropWatch(state, number);
      continue;
    }

    length = 0;
    if (watch->second.revents & POLLIN)              flags[length ++] = 'r';
    if (watch->second.revents & POLLOUT)             flags[length ++] = 'w';
    if (watch->second.revents & (POLLERR | POLLHUP)) flags[length ++] = 'e';
    flags[length] = '\0';

    if (!RunCallback(watch->second.callback, watch->second.plugin, [&flags] (lua_State* state) { lua_pushstring(state, flags); return 1; }, errors))
    {
      // A callback that fails is not called again
      DropWatch(state, number);
    }
  }

  numbers.clear();

  for (const std::pair<const int, Timer>& item : timers)
  {
    if (item.second.pending) numbers.push_back(item.first);
  }

  for (int number : numbers)
  {
    if ((timer = timers.find(number)) == timers.end())
    {
      continue;
    }

    timer->second.pending = false;
    callback = timer->second.callback;
    name     = timer->second.plugin;

    if (timer->second.period == 0)
    {
      // One shot is gone before it runs, the callback is kept until it has run
      timers.erase(timer);
      RunCallback(callback, name, nullptr, errors);
      luaL_unref(state, LUA_REGISTRYINDEX, callback);
      continue;
    }

    timer->second.due = GetQueryMilliseconds() + timer->second.period;

    if (!RunCallback(callback, name, nullptr, errors))
    {
      DropTimer(state, number);
    }
  }

  numbers.clear();

  for (const std::pair<const int, Request>& item : requests)
  {
    if (IsFetchDone(item.first)) numbers.push_back(item.first);
  }

  for (int number : numbers)
  {
    if ((request = requests.find(number)) == requests.end())
    {
      continue;
    }

    // Gone before the callback runs, the way a timer of one shot is
    callback = request->second.callback;
    name     = request->second.plugin;
    requests.erase(request);
    TakeFetch(number, answer);
    RunCallback(callback, name, [&answer] (lua_State* state) { return PushAnswer(state, answer); }, errors);
    luaL_unref(state, LUA_REGISTRYINDEX, callback);
  }

  numbers.clear();

  for (const std::pair<const int, Request>& item : invocations)
  {
    if (IsDBusDone(item.first)) numbers.push_back(item.first);
  }

  for (int number : numbers)
  {
    if ((request = invocations.find(number)) == invocations.end())
    {
      continue;
    }

    // Gone before the callback runs, as a request of fetch() is
    callback = request->second.callback;
    name     = request->second.plugin;
    invocations.erase(request);
    reply    = nullptr;
    TakeDBusAnswer(number, &reply, failure);
    RunCallback(callback, name, [reply, &failure] (lua_State* state) { return PushDBusAnswer(state, reply, failure); }, errors);
    sd_bus_message_unref(reply);
    luaL_unref(state, LUA_REGISTRYINDEX, callback);
  }

  DispatchJournalWatches(errors);

  if (!errors.empty())
  {
    ShowErrors(errors);
  }
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
