// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: poll(), timeout() and fetch(), their handles, and the events of plugins between the ones of Turbo Vision

#include "ScriptPrivate.h"

#define POLL_INTERVAL      50      // Milliseconds between the looks at the descriptors plugins watch
#define FETCH_TIMEOUT      30      // Seconds a request of fetch() may take by default

#define WATCH_READ_TIME    10      // Milliseconds all watches may read their journals for in a poll
#define WATCH_READ_LIMIT   256     // Entries a watch may look at in a poll

std::map<int, Watch> watches;
std::map<int, Timer> timers;
std::map<int, Request> requests;

int dispatching;   // Rounds of events going on, nested by the dialogs of callbacks

int serial;

static short GetEvents(const std::string& mode)
{
  return ((mode.find('r') != std::string::npos) ? POLLIN : 0) | ((mode.find('w') != std::string::npos) ? POLLOUT : 0);
}

// poll(handle, 'r' | 'w' | 'rw', callback, ...): the callback gets 'r', 'w', 'rw', with 'e' for an error or a hang-up
int AddWatch(lua_State* state)
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

int UpdateWatch(lua_State* state)
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

int RemoveWatch(lua_State* state)
{
  DropWatch(state, GetHandle(state));
  return 0;
}

// timeout(milliseconds, [periodic,] callback, ...), as poll() has the function after what it waits for: a function second
// is a timer of once
int AddTimer(lua_State* state)
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

int CancelTimer(lua_State* state)
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

int Fetch(lua_State* state)
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

int CancelRequest(lua_State* state)
{
  DropRequest(state, GetHandle(state));
  return 0;
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
    if (watch.second->due) return true;
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

  for (const RemovedWatch& removed : TakeRemovedWatches())
  {
    // Its methods do nothing anymore, the plugin forgets it; freed by the next round
    RunCallback(removed.callback, removed.plugin, [&removed] (lua_State* state) { lua_rawgeti(state, LUA_REGISTRYINDEX, removed.self); return 1; }, errors);
  }

  DispatchJournalWatches(errors);

  if (!errors.empty())
  {
    ShowErrors(errors);
  }
}
