// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: the dialogs, and open(), find(), seek(), query() and mark() in the window of the menu

#include "ScriptPrivate.h"

#define QUERY_LIMIT        100     // Entries query() gives by default
#define QUERY_SCAN         100000  // Entries it looks at by default
#define QUERY_TIMEOUT      30      // Seconds it reads in an action by default

// message(text, title, options): the text with OK; { input = prompt } puts a line to type a question into under it, the
// line comes back by Ask, nil by Close or an empty line
int ShowMessage(lua_State* state)
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

int AskConfirm(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "confirm() takes a text");
  }

  lua_pushboolean(state, AskActionConfirm(text));
  return 1;
}

int AskInput(lua_State* state)
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

int AskChoice(lua_State* state)
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

int Copy(lua_State* state)
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
int Open(lua_State* state)
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
int Query(lua_State* state)
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
int Find(lua_State* state)
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
int Seek(lua_State* state)
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
int Mark(lua_State* state)
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
