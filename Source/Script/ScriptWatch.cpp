// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: watch() and its methods, the watches read and called, kept by a layout, the console

#include "ScriptPrivate.h"

#define WATCH_CALL_TIME    50      // Milliseconds the callbacks of all watches may take in a round of events
#define WATCH_CALL_LIMIT   1000    // Callbacks of all watches in a round

std::map<int, std::unique_ptr<JournalWatch>> journals;  // Of watch(), by the same numbers
int watching;      // The watch whose callback runs, zero outside

static int turn;          // The watch the next round of reading or calling starts after, so that every one gets its share
bool changed;      // Watches have changed, their windows and the status line are told
static int raising;       // A watch a line has raised the list of watches for, zero for none
static JournalFilter kept;  // The journal of a watch a layout restores, its function takes it as the base of watch()
bool universal;      // Times of the watches and of date() are of UTC, not of the local zone

static int console;  // The number of the watch of the console, made before the plugins

// A watch of watch() by its number, nullptr when there is none or it has been removed
JournalWatch* FindJournalWatch(int number)
{
  std::map<int, std::unique_ptr<JournalWatch>>::iterator watch;

  return (((watch = journals.find(number)) != journals.end()) && (watch->second->state != WATCH_REMOVED)) ? watch->second.get() : nullptr;
}

// The window of a watch while it shows the journal of the watch: F7 Source may have given it another one, the window is
// lost for the watch then as if it had been closed
static JournalView* GetWatchWindow(JournalWatch* watch)
{
  if ((watch->window != nullptr) &&
      ((watch->window->getFilter().directory != watch->filter.directory) ||
       (watch->window->getFilter().merge != watch->filter.merge)))
  {
    watch->window = nullptr;
  }

  return watch->window;
}

// A new window of the journal and the filter of a watch, which takes it as its own
JournalView* OpenWatchWindow(JournalWatch* watch)
{
  JournalWindowRequest request;

  request.filter = &watch->filter;
  request.view   = nullptr;

  message(TProgram::application, evBroadcast, cmJournalOpenWatch, &request);

  watch->window = request.view;
  return request.view;
}

// Watches

// The journal a watch follows unless its filter tells another one: of the window of an action, of the watch whose
// callback this is, of this machine; the one a layout has kept while it gives the watches back
static JournalFilter GetWatchBase()
{
  JournalWatch* parent;
  JournalFilter base;

  parent         = (phase == PHASE_ENTRY) ? FindJournalWatch(watching) : nullptr;
  base           = JournalFilter { };
  base.columns   = JOURNAL_COLUMN_DEFAULT;
  base.highlight = JOURNAL_HIGHLIGHT_BUILTIN;
  base           = (origin != nullptr) ? origin->getFilter() : (parent != nullptr) ? parent->filter : base;

  if (phase == PHASE_RESTORE)
  {
    // The journal the watch has had, whatever window it comes back with
    base.directory = kept.directory;
    base.merge     = kept.merge;
  }

  return base;
}

// The filter and the place of the options of a journal to follow, -1 with the error for a wrong one: checked before a
// watch is made
static int ReadFollow(lua_State* state, int options, const char* name, JournalFilter& filter, JournalPosition& from)
{
  int type;
  std::string value;
  std::string error;

  if (lua_type(state, options) != LUA_TTABLE)
  {
    return Fail(state, "%s takes a table of options", name);
  }

  if (lua_type(state, options + 1) != LUA_TFUNCTION)
  {
    return Fail(state, "%s takes a function after the options", name);
  }

  lua_pushliteral(state, "skipped");
  type = lua_rawget(state, options);
  lua_pop(state, 1);

  if ((type != LUA_TNIL) &&
      (type != LUA_TFUNCTION))
  {
    return Fail(state, "skipped of %s is a function", name);
  }

  if ((GetOption(state, options, "since", value)) ||
      (GetOption(state, options, "until", value)))
  {
    return Fail(state, "%s starts at the end of the journal, it takes neither since nor until", name);
  }

  if (GetFlag(state, options, "window"))
  {
    // A layout keeps the matches of the watch, not the window it has been made in
    return Fail(state, "%s follows the matches of its options, it takes no window", name);
  }

  // The place a layout has kept, given by on('restore')
  from = { std::string(), 0 };
  lua_pushliteral(state, "from");

  if (lua_rawget(state, options) == LUA_TTABLE)
  {
    lua_pushliteral(state, "cursor");
    lua_rawget(state, -2);
    GetString(state, -1, from.cursor);
    lua_pushliteral(state, "time");
    lua_rawget(state, -3);
    from.realtime = lua_tointeger(state, -1);
    lua_pop(state, 2);
  }
  else if (!lua_isnil(state, -1))
  {
    lua_pop(state, 1);
    return Fail(state, "from of %s is the position on('restore') gives", name);
  }

  lua_pop(state, 1);

  if (!MakeFilter(state, options, GetWatchBase(), filter, error))
  {
    return Fail(state, "%s: %s", name, error.c_str());
  }

  return 0;
}

// The journal of a watch: the entries of the filter as they come go to the function after the options, with the values
// after it, the gaps to options.skipped. 0 when it reads, nil and the reason pushed when the journal refuses
static int FollowJournal(lua_State* state, JournalWatch* watch, int options, const JournalFilter& filter, const JournalPosition& from)
{
  std::string error;

  if (!watch->start(filter, from.cursor.empty() ? nullptr : &from, error))
  {
    // The journal refuses, not the plugin
    lua_pushnil(state);
    lua_pushlstring(state, error.data(), error.size());
    return 2;
  }

  // The function and the values after it, the callback of the gaps along with them
  watch->callback = KeepCallback(state, options + 1, options + 2);
  lua_rawgeti(state, LUA_REGISTRYINDEX, watch->callback);
  lua_pushliteral(state, "skipped");
  lua_rawget(state, options);
  lua_setfield(state, -2, "skipped");
  lua_pop(state, 1);

  changed = true;
  return 0;
}

// watch(options): a watch of a plugin, a line in the list of watches with a status and lines, fed by the plugin and by
// the journal once watch:follow() is called. With a function after the options, the form of 0.15, it follows the journal
// of the filter of the options at once: nil and the reason when the journal cannot be opened
int AddJournalWatch(lua_State* state)
{
  int result;
  bool follow;
  bool current;
  std::string title;
  std::string value;
  std::string error;
  std::string key;
  std::string context;
  JournalFilter filter;
  JournalPosition from;
  std::unique_ptr<JournalWatch> watch;

  follow = (lua_type(state, 2) == LUA_TFUNCTION);

  if (lua_type(state, 1) != LUA_TTABLE)
  {
    return Fail(state, "watch() takes a table of options");
  }

  if (!follow)
  {
    for (const char* option : { "matches", "text", "regex", "priority", "since", "until", "from", "skipped" })
    {
      lua_pushstring(state, option);

      if (lua_rawget(state, 1) != LUA_TNIL)
      {
        // The journal is the business of follow()
        lua_pop(state, 1);
        return Fail(state, "watch() reads no journal, %s of the options is for its follow()", option);
      }

      lua_pop(state, 1);
    }
  }

  if ((!GetOption(state, 1, "title", title)) ||
      (title.empty()))
  {
    return Fail(state, "watch() takes a title among the options");
  }

  // What the watch is to its plugin, apart from what the user sees: two of the same key would leave a layout guessing
  if (GetOption(state, 1, "key", key))
  {
    if (key.empty() ||
        (key.find_first_of("\r\n") != std::string::npos))
    {
      return Fail(state, "key of watch() is a text of one line");
    }

    for (std::pair<const int, std::unique_ptr<JournalWatch>>& other : journals)
    {
      if ((other.second->state != WATCH_REMOVED) &&
          (other.second->plugin == plugin) &&
          (other.second->key == key))
      {
        return Fail(state, "key %s of watch() is taken by another watch of the plugin", key.c_str());
      }
    }
  }

  current = true;

  if (GetOption(state, 1, "window", value))
  {
    if ((value != "current") &&
        (value != "new"))
    {
      return Fail(state, "window of watch() is 'current' or 'new'");
    }

    current = (value == "current");
  }

  // What a layout keeps for the plugin, checked now: a watch never drops out of a layout for its context
  context.clear();
  lua_pushliteral(state, "context");

  if ((lua_rawget(state, 1) != LUA_TNIL) &&
      ((lua_type(state, -1) != LUA_TTABLE) || (!WriteNotation(state, -1, context, error))))
  {
    lua_pop(state, 1);
    return Fail(state, "context of watch() is a table a layout can keep: %s", error.empty() ? "it is not a table" : error.c_str());
  }

  lua_pop(state, 1);

  if ((follow) &&
      (ReadFollow(state, 1, "watch()", filter, from) < 0))
  {
    return -1;
  }

  // Until it follows one, a window opened for its lines shows the journal it has been made in
  watch = std::make_unique<JournalWatch>();
  watch->filter = GetWatchBase();

  if ((follow) &&
      ((result = FollowJournal(state, watch.get(), 1, filter, from)) != 0))
  {
    return result;
  }

  watch->number = ++ serial;
  watch->order  = serial;
  watch->plugin = plugin;
  watch->title  = title;
  watch->key    = key;
  watch->global = (phase == PHASE_LOAD);
  watch->window = current ? origin : nullptr;
  watch->context = context;

  // One table for the watch in every callback, a plugin may keep it as a key
  PushHandle(state, watch->number, WATCHER);
  lua_pushvalue(state, -1);
  watch->self = luaL_ref(state, LUA_REGISTRYINDEX);

  journals[watch->number] = std::move(watch);
  changed = true;
  return 1;
}

// watch:follow(options, func, ...): the journal feeds the watch, one journal a watch. The watch itself, or nil and the
// reason when the journal cannot be opened; a watch removed meanwhile follows nothing
int FollowWatch(lua_State* state)
{
  int result;
  JournalWatch* watch;
  JournalFilter filter;
  JournalPosition from;

  if ((watch = FindJournalWatch(GetHandle(state))) == nullptr)
  {
    lua_pushnil(state);
    lua_pushliteral(state, "the watch has been removed");
    return 2;
  }

  if (watch->callback != LUA_NOREF)
  {
    return Fail(state, "follow() of watch %s: it follows a journal already, one is for a watch", watch->title.c_str());
  }

  if (ReadFollow(state, 2, "follow()", filter, from) < 0)
  {
    return -1;
  }

  if ((result = FollowJournal(state, watch, 2, filter, from)) != 0)
  {
    return result;
  }

  lua_settop(state, 1);
  return 1;
}

int SetWatchStatus(lua_State* state)
{
  std::string text;
  JournalWatch* watch;

  if (!GetString(state, 2, text))
  {
    return Fail(state, "status() takes a text");
  }

  if ((watch = FindJournalWatch(GetHandle(state))) != nullptr)
  {
    // A watch removed meanwhile takes nothing
    watch->status = text;
    changed       = true;
  }

  return 0;
}

// The action of a line of a watch, all of them with no line: the line has gone
static void DropLineActions(int watch, lua_Integer line)
{
  int base;

  base = lua_gettop(state);
  lua_getfield(state, LUA_REGISTRYINDEX, ACTIONS);

  if (line == 0)
  {
    lua_pushnil(state);
    lua_rawseti(state, -2, watch);
  }
  else if (lua_rawgeti(state, -1, watch) == LUA_TTABLE)
  {
    lua_pushnil(state);
    lua_rawseti(state, -2, line);
  }

  lua_settop(state, base);
}

// The actions of the lines that have gone: the oldest past the limit, the ones the user has deleted. The lines go in the
// order of their numbers, the actions are few
static void PruneLineActions(JournalWatch* watch)
{
  int base;
  lua_Integer line;
  std::deque<JournalWatchLine>::iterator found;

  base = lua_gettop(state);
  lua_getfield(state, LUA_REGISTRYINDEX, ACTIONS);

  if (lua_rawgeti(state, -1, watch->number) == LUA_TTABLE)
  {
    lua_pushnil(state);

    while (lua_next(state, -2) != 0)
    {
      lua_pop(state, 1);
      line  = lua_tointeger(state, -1);
      found = std::lower_bound(watch->lines.begin(), watch->lines.end(), (uint64_t)line,
                               [] (const JournalWatchLine& item, uint64_t number) { return item.number < number; });

      if ((found == watch->lines.end()) ||
          (found->number != (uint64_t)line))
      {
        // Clearing a field of the table it goes through is allowed
        lua_pushvalue(state, -1);
        lua_pushnil(state);
        lua_rawset(state, -4);
      }
    }
  }

  lua_settop(state, base);
}

// watch:add(text, entry, options): a line of the list of the watch, its number to delete it by
int AddWatchLine(lua_State* state)
{
  uint64_t number;
  std::string text;
  JournalWatch* watch;
  JournalPosition entry;

  if (!GetString(state, 2, text))
  {
    return Fail(state, "add() takes a text");
  }

  entry = { std::string(), 0 };

  if (lua_type(state, 3) == LUA_TTABLE)
  {
    // An entry the window goes to
    lua_pushliteral(state, "__CURSOR");
    lua_rawget(state, 3);
    GetString(state, -1, entry.cursor);
    lua_pushliteral(state, "__REALTIME_TIMESTAMP");
    lua_rawget(state, 3);
    entry.realtime = lua_tointeger(state, -1);
    lua_pop(state, 2);
  }
  else if (!lua_isnoneornil(state, 3))
  {
    return Fail(state, "add() takes an entry after the text");
  }

  if ((watch = FindJournalWatch(GetHandle(state))) == nullptr)
  {
    return 0;
  }

  if ((lua_type(state, 4) == LUA_TTABLE) &&
      (lua_getfield(state, 4, "action") != LUA_TNIL) &&
      (lua_type(state, -1) != LUA_TFUNCTION))
  {
    return Fail(state, "action of add() is a function");
  }

  lua_settop(state, 4);
  number = watch->add(text, entry, GetFlag(state, 4, "quiet"));
  changed = true;
  PruneLineActions(watch);

  if ((lua_type(state, 4) == LUA_TTABLE) &&
      (lua_getfield(state, 4, "action") == LUA_TFUNCTION))
  {
    // Enter or a double click on the line calls it, by the number of the watch and of the line
    lua_getfield(state, LUA_REGISTRYINDEX, ACTIONS);

    if (lua_rawgeti(state, -1, watch->number) != LUA_TTABLE)
    {
      lua_pop(state, 1);
      lua_newtable(state);
      lua_pushvalue(state, -1);
      lua_rawseti(state, -3, watch->number);
    }

    lua_pushvalue(state, -3);
    lua_rawseti(state, -2, number);
  }

  lua_settop(state, 4);
  lua_pushinteger(state, number);

  if (GetFlag(state, 4, "raise"))
  {
    // Red in the list of watches and up in front, with a bell
    watch->raised = true;
    raising       = watch->number;
  }

  return 1;
}

int DeleteWatchLine(lua_State* state)
{
  int whole;
  lua_Integer number;
  JournalWatch* watch;

  number = lua_tointegerx(state, 2, &whole);

  if (!whole)
  {
    return Fail(state, "delete() takes the number add() has given");
  }

  if ((watch = FindJournalWatch(GetHandle(state))) != nullptr)
  {
    watch->erase(number);
    DropLineActions(watch->number, number);
    changed = true;
  }

  return 0;
}

// watch:context(table): what a layout keeps for the plugin from now on
int SetWatchContext(lua_State* state)
{
  std::string text;
  std::string error;
  JournalWatch* watch;

  if ((lua_type(state, 2) != LUA_TTABLE) ||
      (!WriteNotation(state, 2, text, error)))
  {
    return Fail(state, "context() takes a table a layout can keep: %s", error.empty() ? "it is not a table" : error.c_str());
  }

  if ((watch = FindJournalWatch(GetHandle(state))) != nullptr)
  {
    watch->context = text;
  }

  return 0;
}

int ClearWatchLines(lua_State* state)
{
  JournalWatch* watch;

  if ((watch = FindJournalWatch(GetHandle(state))) != nullptr)
  {
    watch->clear();
    DropLineActions(watch->number, 0);
    changed = true;
  }

  return 0;
}

// Marked removed at once, freed once no callback runs: a callback may remove its own watch or another one
static void DropJournalWatch(int number)
{
  JournalWatch* watch;

  if ((watch = FindJournalWatch(number)) != nullptr)
  {
    watch->stop(WATCH_REMOVED, std::string());
    changed = true;
  }
}

int RemoveJournalWatch(lua_State* state)
{
  DropJournalWatch(GetHandle(state));
  return 0;
}

void CollectJournalWatches()
{
  std::map<int, std::unique_ptr<JournalWatch>>::iterator watch;

  if ((dispatching > 0) ||
      (phase != PHASE_IDLE))
  {
    // A callback may still hold one
    return;
  }

  for (watch = journals.begin(); watch != journals.end(); )
  {
    if (watch->second->state != WATCH_REMOVED)
    {
      ++ watch;
      continue;
    }

    luaL_unref(state, LUA_REGISTRYINDEX, watch->second->callback);
    luaL_unref(state, LUA_REGISTRYINDEX, watch->second->self);
    DropLineActions(watch->first, 0);
    watch = journals.erase(watch);
  }
}

// Interface

// The console comes before the plugins, their print() while they load goes there as well
void OpenConsole()
{
  std::unique_ptr<JournalWatch> watch(new JournalWatch());

  watch->number    = ++ serial;
  watch->order     = serial;
  watch->plugin    = "turbojedi";
  watch->title     = "Console";
  watch->key       = "console";
  watch->status    = "print() and log() of plugins, problems, calls of the bus";
  watch->global    = true;
  watch->permanent = true;
  console          = watch->number;
  journals[console] = std::move(watch);
}

void LogConsole(const std::string& source, const std::string& text, bool problem)
{
  JournalWatch* watch;

  if ((watch = FindJournalWatch(console)) != nullptr)
  {
    // A problem calls for attention, the rest is there to be read
    watch->add(source + ": " + text, { std::string(), 0 }, !problem);
    changed = true;
  }
}

int GetConsoleWatch()
{
  return console;
}

// Numbers of the watches of journals in a round that starts after the one of the turn, so that each gets its share
std::vector<int> RoundJournalWatches()
{
  std::vector<int> numbers;
  std::map<int, std::unique_ptr<JournalWatch>>::iterator start;

  start = journals.upper_bound(turn);

  for (std::map<int, std::unique_ptr<JournalWatch>>::iterator watch = start; watch != journals.end(); ++ watch)
  {
    numbers.push_back(watch->first);
  }

  for (std::map<int, std::unique_ptr<JournalWatch>>::iterator watch = journals.begin(); watch != start; ++ watch)
  {
    numbers.push_back(watch->first);
  }

  return numbers;
}

// An entry or a gap of a watch to its callback, in the phase of a watch: the window of the watch is the one of mark();
// an error leaves the watch failed with its lines kept
static void CallJournalWatch(JournalWatch* watch, const JournalWatchEvent& event, std::vector<std::string>& errors)
{
  int base;
  int extra;
  bool done;
  int number;
  std::string error;

  base   = lua_gettop(state);
  number = watch->number;

  // Trace, the function, the entry or the gap, the watch, the values of the plugin
  lua_pushcfunction(state, Trace);
  lua_rawgeti(state, LUA_REGISTRYINDEX, watch->callback);

  if (event.gap)
  {
    lua_getfield(state, base + 2, "skipped");

    if (lua_type(state, -1) != LUA_TFUNCTION)
    {
      // The plugin has not asked about gaps, the list of watches tells them anyway
      lua_settop(state, base);
      return;
    }

    lua_createtable(state, 0, 3);
    lua_pushinteger(state, event.range.since);
    lua_setfield(state, -2, "since");
    lua_pushinteger(state, event.range.till);
    lua_setfield(state, -2, "till");

    if (event.range.uncertain)
    {
      lua_pushboolean(state, true);
      lua_setfield(state, -2, "uncertain");
    }
  }
  else
  {
    lua_rawgeti(state, base + 2, 1);
    PushEntry(state, event.fields);
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, watch->self);
  lua_getfield(state, base + 2, "n");
  extra = lua_tointeger(state, -1);
  lua_pop(state, 1);

  for (int index = 2; index <= extra; index ++)
  {
    lua_rawgeti(state, base + 2, index);
  }

  lua_remove(state, base + 2);

  phase    = PHASE_ENTRY;
  plugin   = watch->plugin;
  watching = number;
  origin   = GetWatchWindow(watch);
  done     = (lua_pcall(state, 2 + extra - 1, 0, base + 1) == LUA_OK);
  phase    = PHASE_IDLE;
  watching = 0;
  origin   = nullptr;

  if (!done)
  {
    error = GetError(state);
    errors.push_back(watch->plugin + ": " + error);

    if ((watch = FindJournalWatch(number)) != nullptr)
    {
      // Stops reading, the lines stay to look at: the first line of the error tells why, with the file and not its path
      error = error.substr(0, error.find('\n'));
      error = ((error.find(".lua:") != std::string::npos) && (error.rfind('/', error.find(".lua:")) != std::string::npos)) ? error.substr(error.rfind('/', error.find(".lua:")) + 1) : error;
      watch->stop(WATCH_FAILED, error);
      changed = true;
    }
  }

  lua_settop(state, base);
}

// The entries and gaps the watches have read, in turns within a time and a count of all of them; the rest waits for the
// next round
void DispatchJournalWatches(std::vector<std::string>& errors)
{
  bool progress;
  size_t calls;
  int64_t end;
  JournalWatch* watch;
  JournalWatchEvent event;

  dispatching ++;
  calls    = 0;
  progress = true;
  end      = GetQueryMilliseconds() + WATCH_CALL_TIME;

  while ((progress) &&
         (calls < WATCH_CALL_LIMIT) &&
         (GetQueryMilliseconds() < end))
  {
    progress = false;

    for (int number : RoundJournalWatches())
    {
      if (((watch = FindJournalWatch(number)) == nullptr) ||
          (watch->state != WATCH_ACTIVE) ||
          (watch->events.empty()))
      {
        continue;
      }

      event = std::move(watch->events.front());
      watch->events.pop_front();

      if (event.gap)
      {
        // Shown with the watch whether its plugin asks about them or not
        watch->gaps ++;
        watch->gap = event.range;
        changed    = true;
      }
      else
      {
        // The place a layout keeps: the entry has come to the plugin
        watch->delivered = event.position;
      }

      CallJournalWatch(watch, event, errors);

      turn     = number;
      progress = true;

      if (((++ calls) >= WATCH_CALL_LIMIT) ||
          (GetQueryMilliseconds() >= end))
      {
        break;
      }
    }
  }

  dispatching --;
}

void ForgetPluginWindow(JournalView* view)
{
  for (std::pair<const int, std::unique_ptr<JournalWatch>>& watch : journals)
  {
    if (watch.second->window == view)
    {
      // An entry of the watch opens a window of its own next time
      watch.second->window = nullptr;
    }
  }
}

std::vector<JournalWatch*> ListPluginWatches()
{
  std::vector<JournalWatch*> list;

  for (std::pair<const int, std::unique_ptr<JournalWatch>>& watch : journals)
  {
    if (watch.second->state != WATCH_REMOVED)
    {
      list.push_back(watch.second.get());
    }
  }

  // The newest line first, the ones without lines after them in the order they have been set
  std::stable_sort(list.begin(), list.end(), [] (const JournalWatch* one, const JournalWatch* other)
  {
    return (one->latest != other->latest) ? (one->latest > other->latest) : (one->order < other->order);
  });

  return list;
}

JournalWatch* FindPluginWatch(int number)
{
  return FindJournalWatch(number);
}

// The watches of a plugin and a title in the order they have been set: the plugins set them again the same way at load,
// a layout gives its ones back in that order too
static std::vector<JournalWatch*> NamePluginWatches(const std::string& plugin, const std::string& title)
{
  std::vector<JournalWatch*> list;

  for (JournalWatch* watch : ListPluginWatches())
  {
    if ((watch->plugin == plugin) &&
        (watch->title == title))
    {
      list.push_back(watch);
    }
  }

  std::sort(list.begin(), list.end(), [] (const JournalWatch* one, const JournalWatch* other) { return one->order < other->order; });
  return list;
}

JournalWatch* FindPluginWatch(const std::string& plugin, const std::string& title, int index)
{
  std::vector<JournalWatch*> list;

  list = NamePluginWatches(plugin, title);
  return ((index >= 0) && (index < (int)list.size())) ? list[index] : nullptr;
}

JournalWatch* FindPluginWatch(const std::string& plugin, const std::string& key)
{
  for (JournalWatch* watch : ListPluginWatches())
  {
    if ((watch->plugin == plugin) &&
        (watch->key == key))
    {
      return watch;
    }
  }

  return nullptr;
}

int IndexPluginWatch(const JournalWatch* watch)
{
  std::vector<JournalWatch*> list;

  list = NamePluginWatches(watch->plugin, watch->title);
  return std::find(list.begin(), list.end(), watch) - list.begin();
}

bool RemovePluginWatch(int number)
{
  JournalWatch* watch;

  if (((watch = FindJournalWatch(number)) == nullptr) ||
      (watch->permanent))
  {
    // The console stays
    return false;
  }

  DropJournalWatch(number);
  return true;
}

void ErasePluginWatchLine(JournalWatch* watch, uint64_t line)
{
  watch->erase(line);

  if (state != nullptr)
  {
    DropLineActions(watch->number, line);
  }

  changed = true;
}

void ClearPluginWatchLines(JournalWatch* watch)
{
  watch->clear();

  if (state != nullptr)
  {
    DropLineActions(watch->number, 0);
  }

  changed = true;
}

bool RunPluginWatchLine(JournalWatch* watch, uint64_t line)
{
  int base;
  int number;
  std::string name;

  if (state == nullptr)
  {
    return false;
  }

  base = lua_gettop(state);
  lua_pushcfunction(state, Trace);
  lua_getfield(state, LUA_REGISTRYINDEX, ACTIONS);

  if ((lua_rawgeti(state, -1, watch->number) != LUA_TTABLE) ||
      (lua_rawgeti(state, -1, line) != LUA_TFUNCTION))
  {
    lua_settop(state, base);
    return false;
  }

  // action(watch, line) as a callback of the watch: dialogs, commands and requests, mark() in its window
  lua_remove(state, base + 2);
  lua_remove(state, base + 2);
  lua_rawgeti(state, LUA_REGISTRYINDEX, watch->self);
  lua_pushinteger(state, line);

  number   = watch->number;
  name     = watch->plugin;
  phase    = PHASE_ENTRY;
  plugin   = name;
  watching = number;
  origin   = GetWatchWindow(watch);

  if (lua_pcall(state, 2, 0, base + 1) != LUA_OK)
  {
    phase    = PHASE_IDLE;
    watching = 0;
    origin   = nullptr;
    ShowErrors({ name + ": " + GetError(state) });
  }

  phase    = PHASE_IDLE;
  watching = 0;
  origin   = nullptr;
  lua_settop(state, base);
  changed  = true;
  return true;
}

void ShowPluginWatchEntry(JournalWatch* watch, const JournalPosition& entry)
{
  JournalView* view;

  if (((view = GetWatchWindow(watch)) == nullptr) &&
      ((view = OpenWatchWindow(watch)) == nullptr))
  {
    return;
  }

  // The window of the watch comes to the front with the cursor on the entry
  static_cast<TView*>(view->owner)->select();
  view->reach(entry.cursor, entry.realtime);
}

void MarkPluginWatchesChanged()
{
  changed = true;
}

bool TakePluginWatchChanges()
{
  bool result;

  result  = changed;
  changed = false;
  return result;
}

int TakePluginWatchRaise()
{
  int number;

  number  = raising;
  raising = 0;
  return number;
}

void DropWorkspaceWatches()
{
  for (std::pair<const int, std::unique_ptr<JournalWatch>>& watch : journals)
  {
    if ((!watch.second->global) &&
        (watch.second->state != WATCH_REMOVED))
    {
      // Of the layout that goes, with their lines
      DropJournalWatch(watch.first);
    }
  }
}

std::vector<std::string> SavePluginWatches(const std::vector<JournalView*>& windows)
{
  std::string line;
  std::vector<std::string> parts;
  std::vector<JournalWatch*> list;
  std::vector<std::string> lines;
  std::vector<JournalView*>::const_iterator window;

  for (std::pair<const int, std::unique_ptr<JournalWatch>>& watch : journals)
  {
    if (watch.second->state != WATCH_REMOVED)
    {
      list.push_back(watch.second.get());
    }
  }

  // In the order they have been set, as they come back
  std::sort(list.begin(), list.end(), [] (const JournalWatch* one, const JournalWatch* other) { return one->order < other->order; });

  for (JournalWatch* watch : list)
  {
    if (!watch->saved.empty())
    {
      // Of a plugin not there, kept as it has come
      lines.push_back(watch->saved);
      continue;
    }

    if (watch->context.empty())
    {
      // Lives until the exit
      continue;
    }

    // The part of TurboJedi: its journal, the window by the number of its section among the journals, the place by the cursor
    // and the time of the entry given last
    parts.clear();
    window = std::find(windows.begin(), windows.end(), GetWatchWindow(watch));

    if (!watch->filter.directory.empty()) parts.push_back("directory = " + QuoteNotation(watch->filter.directory));
    if (watch->filter.merge)              parts.push_back("merge = true");

    if ((watch->window != nullptr) &&
        (window != windows.end()))
    {
      parts.push_back("window = " + std::to_string(window - windows.begin() + 1));
    }

    if (!watch->delivered.cursor.empty())
    {
      parts.push_back("cursor = " + QuoteNotation(watch->delivered.cursor));
      parts.push_back("time = " + std::to_string(watch->delivered.realtime));
    }

    line = watch->plugin + " {";

    for (size_t index = 0; index < parts.size(); index ++)
    {
      line += ((index > 0) ? ", " : " ") + parts[index];
    }

    line += parts.empty() ? "} " : " } ";
    line += watch->context;
    lines.push_back(line);
  }

  return lines;
}

// A watch of a layout whose plugin is not there or cannot take it: kept in the list as it has come, until it is removed
static void KeepPluginWatch(const std::string& name, const std::string& line, const std::string& reason)
{
  std::unique_ptr<JournalWatch> watch;

  watch = std::make_unique<JournalWatch>();
  watch->number = ++ serial;
  watch->order  = serial;
  watch->plugin = name;
  watch->title  = "saved watch";
  watch->saved  = line;
  watch->stop(WATCH_STOPPED, reason);

  journals[watch->number] = std::move(watch);
  changed = true;
}

void RestorePluginWatches(const std::vector<std::string>& lines, const std::vector<JournalView*>& windows, std::vector<std::string>& errors)
{
  int base;
  int number;
  size_t position;
  std::string name;
  std::string error;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    return;
  }

  for (const std::string& line : lines)
  {
    base     = lua_gettop(state);
    position = line.find(' ');
    name     = line.substr(0, position);
    position = (position == std::string::npos) ? line.size() : position;

    // The part of TurboJedi, then the context of the plugin, both data only
    if (!ReadNotation(state, line, position, error))
    {
      errors.push_back("A watch of " + name + " in the layout cannot be read: " + error);
      lua_settop(state, base);
      continue;
    }

    if ((lua_type(state, -1) != LUA_TTABLE) ||
        (!ReadNotation(state, line, position, error)) ||
        (lua_type(state, -1) != LUA_TTABLE))
    {
      errors.push_back("A watch of " + name + " in the layout cannot be read: " + (error.empty() ? std::string("two tables are expected") : error));
      lua_settop(state, base);
      continue;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, RESTORERS);
    lua_getfield(state, -1, name.c_str());
    lua_remove(state, -2);

    if (lua_type(state, -1) != LUA_TFUNCTION)
    {
      KeepPluginWatch(name, line, "plugin " + name + " is not loaded or does not restore watches");
      lua_settop(state, base);
      continue;
    }

    // Trace, the function, the context, the position: { cursor, time } of the part of TurboJedi
    lua_pushcfunction(state, Trace);
    lua_insert(state, -2);
    lua_pushvalue(state, base + 2);
    lua_createtable(state, 0, 2);
    lua_getfield(state, base + 1, "cursor");
    lua_setfield(state, -2, "cursor");
    lua_getfield(state, base + 1, "time");
    lua_setfield(state, -2, "time");

    // The window the watch has gone to its entries in, by the number of its journal in the layout, and its own journal
    lua_getfield(state, base + 1, "window");
    number = lua_tointeger(state, -1);
    lua_getfield(state, base + 1, "directory");
    kept.directory = (lua_type(state, -1) == LUA_TSTRING) ? lua_tostring(state, -1) : "";
    lua_getfield(state, base + 1, "merge");
    kept.merge = lua_toboolean(state, -1);
    lua_pop(state, 3);

    phase  = PHASE_RESTORE;
    plugin = name;
    origin = ((number >= 1) && (number <= (int)windows.size())) ? windows[number - 1] : nullptr;

    if (lua_pcall(state, 2, 0, base + 3) != LUA_OK)
    {
      errors.push_back(name + ": " + GetError(state));
    }

    phase  = PHASE_IDLE;
    origin = nullptr;
    lua_settop(state, base);
  }
}

bool GetPluginUniversal()
{
  return universal;
}

void SetPluginUniversal(bool value)
{
  universal = value;
  changed   = true;
}
