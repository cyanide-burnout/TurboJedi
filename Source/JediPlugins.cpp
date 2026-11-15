// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TMenuItem
#define Uses_TKeys
#define Uses_TEvent
#include <tvision/tv.h>

#include "JediPlugins.h"
#include "JediProcess.h"
#include "ActionDialogs.h"
#include "JediClipboard.h"
#include "JournalExpression.h"
#include "JournalView.h"
#include "JournalQuery.h"
#include "Commands.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#if LUA_VERSION_NUM != 504
#error "Plugins are written for Lua 5.4"
#endif

#include <grp.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <map>
#include <set>
#include <algorithm>

#define PHASE_ANY     -1
#define PHASE_IDLE    0
#define PHASE_LOAD    1  // The files run, handlers are registered
#define PHASE_MENU    2  // Handlers add items to the menu
#define PHASE_ACTION  3  // The chosen item runs

#define ACTION_LIMIT       100  // Items of a menu
#define CAPTURE_TIMEOUT    30   // Seconds capture() waits by default
#define EXPRESSION_LIMIT   64   // Compiled patterns kept for the next calls

#define cmJournalAction    1200  // The first item of the menu, one per item

#define QUERY_LIMIT        100     // Entries query() gives by default
#define QUERY_SCAN         100000  // Entries it looks at by default
#define QUERY_TIMEOUT      30      // Seconds it reads in an action by default
#define MENU_TIME          200     // Milliseconds the queries of all handlers of a menu may take together
#define MENU_SCAN          10000   // Entries they may look at together
#define MARKED_LIMIT       1000    // Entries of the selection a handler gets in context.marked

#define HANDLERS  "TurboJedi.handlers"
#define NAMES     "TurboJedi.names"     // Of the plugin of every handler, by the same index
#define MENU      "TurboJedi.menu"

static const char* const SystemDirectories[] = { "/usr/share/turbojedi/plugins", "/usr/local/share/turbojedi/plugins" };

static lua_State* state;
static int phase;
static std::string plugin;  // Name of the plugin being loaded
static JournalView* origin; // Window the menu has been opened in, for open(), find(), seek() and query()
static int64_t deadline;    // Of the queries of the handlers making the menu, shared by all of them
static size_t budget;       // Entries they may look at yet

static void PushEntry(lua_State* state, const std::vector<std::pair<std::string, std::string>>& fields);
static std::vector<std::string> directories;  // In the order a file of the same name takes the place: the user last
static std::map<std::string, JournalExpression> expressions;

// Errors of Lua leave a function by longjmp(), the system Lua is built as C: a function of the interface is a body that
// gives -1 with the message on the stack instead of raising it, Call raises it once the body has returned and no object
// of C++ is left there to destroy

typedef int (*Body)(lua_State* state);

template <Body body, int needed> static int Call(lua_State* state)
{
  int result;

  if ((needed != PHASE_ANY) &&
      (phase != needed))
  {
    switch (needed)
    {
      case PHASE_LOAD: return luaL_error(state, "handlers are registered only while the plugin is loaded");
      case PHASE_MENU: return luaL_error(state, "items are added only by a handler while it makes the menu");
      default:         return luaL_error(state, "dialogs and commands are used only by an action chosen from the menu");
    }
  }

  result = body(state);
  return (result < 0) ? lua_error(state) : result;
}

static int Fail(lua_State* state, const char* format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  lua_pushvfstring(state, format, arguments);
  va_end(arguments);

  return -1;
}

// Message handler of every call into Lua, the place in the plugin goes along
static int Trace(lua_State* state)
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

static std::string GetError(lua_State* state)
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

static bool GetString(lua_State* state, int index, std::string& value)
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

static int RegisterHandler(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TFUNCTION)
  {
    return Fail(state, "registerHandler() takes a function");
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

static int ShowMessage(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "message() takes a text");
  }

  ShowActionText("Message", text);
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

static int Execute(lua_State* state)
{
  std::string failure;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "execute() takes a list of the command and its arguments");
  }

  if (!RunBackground(arguments, failure))
  {
    lua_pushnil(state);
    lua_pushlstring(state, failure.data(), failure.size());
    return 2;
  }

  lua_pushboolean(state, true);
  return 1;
}

static int Output(lua_State* state)
{
  std::string failure;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "output() takes a list of the command and its arguments");
  }

  if (!OpenOutput(arguments, failure))
  {
    lua_pushnil(state);
    lua_pushlstring(state, failure.data(), failure.size());
    return 2;
  }

  lua_pushboolean(state, true);
  return 1;
}

static void PushOutcome(lua_State* state, const JediOutcome& outcome)
{
  lua_pushstring(state, outcome.how.c_str());

  if (outcome.how == "error") lua_pushlstring(state, outcome.text.data(), outcome.text.size());
  else if (outcome.how == "cancel") lua_pushnil(state);
  else lua_pushinteger(state, outcome.code);
}

static int Terminal(lua_State* state)
{
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "terminal() takes a list of the command and its arguments");
  }

  PushOutcome(state, RunTerminal(arguments));
  return 2;
}

static int Capture(lua_State* state)
{
  int timeout;
  std::string output;
  std::string errors;
  JediOutcome outcome;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "capture() takes a list of the command and its arguments");
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

  if (outcome.how == "error") lua_pushnil(state);
  else                        lua_pushlstring(state, output.data(), output.size());

  PushOutcome(state, outcome);

  if (errors.empty()) lua_pushnil(state);
  else                lua_pushlstring(state, errors.data(), errors.size());

  return 4;
}

// A field of an options table without its metamethods, false when it is not there or not a string or a number
static bool GetOption(lua_State* state, int index, const char* name, std::string& value)
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

static bool GetFlag(lua_State* state, int index, const char* name)
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

// A filter of the options of open() and query(): the journals, columns and highlighting of the window of the menu, the
// matches, text, times and levels of the options; false and the reason when an option is wrong
static bool MakeFilter(lua_State* state, int index, JournalFilter& filter, std::string& error)
{
  int top;
  int level;
  uint64_t stamp;
  size_t equal;
  std::string key;
  std::string value;

  top              = lua_gettop(state);
  filter           = JournalFilter { };
  filter.directory = origin->getFilter().directory;
  filter.merge     = origin->getFilter().merge;
  filter.columns   = origin->getFilter().columns;
  filter.highlight = origin->getFilter().highlight;
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
    error = "unknown time " + (filter.until.empty() ? filter.since : filter.until);
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

// A new window like the one of the menu with the filter of the options
static int Open(lua_State* state)
{
  std::string error;
  JournalFilter filter;

  if (lua_type(state, 1) != LUA_TTABLE)
  {
    return Fail(state, "open() takes a table of options");
  }

  if (!MakeFilter(state, 1, filter, error))
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

  if (!MakeFilter(state, 1, filter, error))
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

// Find in the window of the menu: highlighted, on into the older entries, a step of its history
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

  // Older by default, the way the first Find goes
  lua_pushboolean(state, origin->search(text, mode & ~JOURNAL_TEXT_EXCLUDE, !GetFlag(state, 2, "newer")));
  return 1;
}

// Go to a time in the window of the menu: microseconds such as __REALTIME_TIMESTAMP, or a time as Go to time takes it
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

  origin->jump(stamp);
  return 0;
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

// Marks in the window of the menu the lines of an entry, of a list of entries such as query() gives, or of cursors
static int Mark(lua_State* state)
{
  bool single;
  uint64_t oldest;
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

static const luaL_Reg functions[] =
{
  { "registerHandler", Call<RegisterHandler, PHASE_LOAD>   },
  { "message",         Call<ShowMessage,     PHASE_ACTION> },
  { "confirm",         Call<AskConfirm,      PHASE_ACTION> },
  { "input",           Call<AskInput,        PHASE_ACTION> },
  { "choose",          Call<AskChoice,       PHASE_ACTION> },
  { "copy",            Call<Copy,            PHASE_ACTION> },
  { "execute",         Call<Execute,         PHASE_ACTION> },
  { "output",          Call<Output,          PHASE_ACTION> },
  { "terminal",        Call<Terminal,        PHASE_ACTION> },
  { "capture",         Call<Capture,         PHASE_ACTION> },
  { "open",            Call<Open,            PHASE_ACTION> },
  { "find",            Call<Find,            PHASE_ACTION> },
  { "seek",            Call<Seek,            PHASE_ACTION> },
  { "query",           Call<Query,           PHASE_ANY>    },
  { "mark",            Call<Mark,            PHASE_ACTION> },
  { "quote",           Call<Quote,           PHASE_ANY>    },
  { "regex",           Call<Match,           PHASE_ANY>    },
  { "matches",         Call<Matches,         PHASE_ANY>    },
  { "which",           Call<Which,           PHASE_ANY>    },
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

static void PushEntry(lua_State* state, const std::vector<std::pair<std::string, std::string>>& fields)
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

// Name of a plugin by its file: 100-fail2ban.lua is fail2ban
static std::string GetPluginName(const std::string& file)
{
  size_t start;

  start = file.find_first_not_of("0123456789");
  start = ((start != std::string::npos) && (start > 0) && ((file[start] == '-') || (file[start] == '_'))) ? (start + 1) : 0;
  return file.substr(start, file.size() - 4 - start);
}

// The title fits the screen, cut by whole characters
static std::string Fit(std::string title, int width)
{
  while ((strwidth(title) > width) &&
         (!title.empty()))
  {
    do
    {
      title.pop_back();
    }
    while ((!title.empty()) &&
           ((static_cast<unsigned char>(title.back()) & 0xc0) == 0x80));

    if ((!title.empty()) &&
        (static_cast<unsigned char>(title.back()) >= 0xc0))
    {
      // The lead byte of the character cut
      title.pop_back();
    }
  }

  return title;
}

static void ShowErrors(const std::vector<std::string>& errors)
{
  std::string text;

  for (const std::string& error : errors)
  {
    text += (text.empty() ? "" : "\n\n") + error;
  }

  ShowActionText("Plugins", text);
}

// Interface

void LoadPlugins(const std::string& directory, std::vector<std::string>& errors)
{
  DIR* folder;
  size_t length;
  std::string path;
  std::string reason;
  struct dirent* item;
  struct stat status;
  std::map<std::string, std::string> files;

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

  luaL_newlib(state, functions);
  lua_setglobal(state, "turbojedi");

  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, HANDLERS);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, NAMES);

  // The menu a handler gets: menu:add(title, func, ...)
  luaL_newmetatable(state, MENU);
  lua_newtable(state);
  lua_pushcfunction(state, (Call<AddItem, PHASE_MENU>));
  lua_setfield(state, -2, "add");
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
      continue;
    }

    if (!CheckTrust(path, reason))
    {
      errors.push_back(reason);
      continue;
    }

    plugin = GetPluginName(file.first);
    lua_pushcfunction(state, Trace);

    if ((luaL_loadfile(state, path.c_str()) != LUA_OK) ||
        (lua_pcall(state, 0, 0, 1) != LUA_OK))
    {
      errors.push_back(GetError(state));
    }

    lua_settop(state, 0);
  }

  phase = PHASE_IDLE;
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
  size_t total;
  std::vector<std::string> errors;
  std::vector<std::string> cursors;
  std::vector<std::pair<std::string, std::string>> fields;
  std::vector<std::vector<std::pair<std::string, std::string>>> records;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice: an action runs, its dialog keeps the windows going
    return;
  }

  if ((cursor.empty()) ||
      ((fields = ReadJournalFields(filter, cursor)).empty()))
  {
    // Rotated away meanwhile
    ShowActionText("Actions", "The entry is not in the journal anymore");
    return;
  }

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);                   // base + 1
  PushEntry(state, fields);                          // base + 2
  PushContext(state, filter);                        // base + 3

  // The selection and the marks of the window, beside the entry of the cursor: the entries in context.marked, how many
  // there are in context.selected, as the list may be cut
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

    lua_setfield(state, base + 3, "marked");
    lua_pushinteger(state, total);
    lua_setfield(state, base + 3, "selected");
  }

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
  count  = std::min<int>(lua_rawlen(state, base + 4), ACTION_LIMIT);

  if (!errors.empty())
  {
    ShowErrors(errors);
  }

  if (count == 0)
  {
    if (errors.empty())
    {
      ShowActionText("Actions", "No actions for this entry");
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

void ClosePlugins()
{
  if (state != nullptr)
  {
    lua_close(state);
    state = nullptr;
  }

  expressions.clear();
}
