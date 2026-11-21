// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// The methods of the bus and the table of them; the calls that wait for a dialog or read by steps are handed over to
// the service

#include "ServicePrivate.h"

// Methods

static int Describe(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  char host[256];
  TView* front;
  struct passwd* user;

  if ((result = Allow(message, error)) < 0)
  {
    return result;
  }

  if (gethostname(host, sizeof(host)) < 0)
  {
    host[0] = '\0';
  }

  host[sizeof(host) - 1] = '\0';
  user  = getpwuid(getuid());
  front = FindFrontWindow();

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;
    JournalWindow* journal;
    TimelineWindow* timeline;
    WatchWindow* watches;
    WatchLogWindow* lines;
    struct rlimit limit;
    uint64_t first;
    uint64_t last;
    uint64_t step;
    size_t count;
    bool selected;
    std::array<uint64_t, 3> counts;

    result = std::min(0, sd_bus_message_open_container(reply, 'a', "{sv}"));
    result = std::min(result, sd_bus_message_append(reply, "{sv}", "pid", "u", (uint32_t)getpid()));
    result = std::min(result, sd_bus_message_append(reply, "{sv}", "uid", "u", (uint32_t)getuid()));
    result = std::min(result, AppendString(reply, "user", (user != nullptr) ? user->pw_name : ""));
    result = std::min(result, AppendString(reply, "host", host));
    result = std::min(result, AppendString(reply, "version", VERSION));
    result = std::min(result, sd_bus_message_append(reply, "{sv}", "started", "t", started));
    result = std::min(result, sd_bus_message_append(reply, "{sv}", "api", "u", (uint32_t)BUS_VERSION));

    if (delegated)
    {
      // Root started by sudo answers this user as well
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "sudo", "u", (uint32_t)delegate));
    }

    // What the user has in front, and whether a dialog or a menu is open over it: the changes of the screen wait then
    if ((journal = dynamic_cast<JournalWindow*>(front)) != nullptr)
    {
      result = std::min(result, AppendString(reply, "front", "journal"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "window", "u", (uint32_t)journal->view->id()));
      result = std::min(result, AppendString(reply, "title", TitleWindow(journal)));
    }
    else if ((timeline = dynamic_cast<TimelineWindow*>(front)) != nullptr)
    {
      result = std::min(result, AppendString(reply, "front", "timeline"));
      result = std::min(result, AppendString(reply, "title", (timeline->title != nullptr) ? timeline->title : ""));
      result = std::min(result, AppendFilter(reply, timeline->view->getFilter()));

      if (timeline->view->getCounted(first, last, step))
      {
        // Times as Query() takes them, in the zone of the timeline, an end at the last microsecond of its interval
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "step", "t", step));
        result = std::min(result, AppendString(reply, "counted_since", FormatJournalTime(first, timeline->view->getUniversal())));
        result = std::min(result, AppendString(reply, "counted_until", FormatJournalTime(last - 1, timeline->view->getUniversal())));
      }

      if (timeline->view->getFocus(first, last, count, counts, selected))
      {
        result = std::min(result, AppendString(reply, "interval_since", FormatJournalTime(first, timeline->view->getUniversal())));
        result = std::min(result, AppendString(reply, "interval_until", FormatJournalTime(last - 1, timeline->view->getUniversal())));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "intervals", "u", (uint32_t)count));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "selection", "b", (int)selected));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "errors", "t", counts[0]));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "warnings", "t", counts[1]));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "other", "t", counts[2]));
      }
    }
    else if ((watches = dynamic_cast<WatchWindow*>(front)) != nullptr)
    {
      result = std::min(result, AppendString(reply, "front", "watches"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "watch", "u", (uint32_t)std::max(watches->view->current(), 0)));
    }
    else if ((lines = dynamic_cast<WatchLogWindow*>(front)) != nullptr)
    {
      result = std::min(result, AppendString(reply, "front", "watch"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "watch", "u", (uint32_t)std::max(lines->view->number, 0)));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "line", "t", lines->view->current()));
    }
    else
    {
      result = std::min(result, AppendString(reply, "front", ""));
    }

    result = std::min(result, sd_bus_message_append(reply, "{sv}", "dialog", "b", (int)(TProgram::application->TopView() != TProgram::application)));

    // Every window and watch keeps the files of its journal open: how close the process is to its limit
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
    {
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "descriptors", "u", (uint32_t)CountDescriptors()));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "descriptor_limit", "u", (uint32_t)std::min<rlim_t>(limit.rlim_cur, UINT32_MAX)));
    }
    result = std::min(result, sd_bus_message_close_container(reply));
    return result;
  });
}

static int ListWindows(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  size_t total;
  TView* front;
  std::vector<JournalWindow*> windows;

  if ((result = Allow(message, error)) < 0)
  {
    return result;
  }

  TProgram::deskTop->forEach(CollectWindows, &windows);
  front = FindFrontWindow();

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;

    result = std::min(0, sd_bus_message_open_container(reply, 'a', "a{sv}"));

    for (JournalWindow* window : windows)
    {
      window->view->pickedCursors(0, total);

      result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "id", "u", (uint32_t)window->view->id()));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "window", "i", (int32_t)((window->number != wnNoNumber) ? window->number : 0)));
      result = std::min(result, AppendString(reply, "title", TitleWindow(window)));
      result = std::min(result, AppendFilter(reply, window->view->getFilter()));

      result = std::min(result, sd_bus_message_append(reply, "{sv}", "following", "b", (int)window->view->following()));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "marked_only", "b", (int)window->view->markedOnly()));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "active", "b", (int)(window == front)));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "selected", "u", (uint32_t)total));
      result = std::min(result, sd_bus_message_close_container(reply));
    }

    result = std::min(result, sd_bus_message_close_container(reply));
    return result;
  });
}

static JournalWindow* TakeWindow(sd_bus_message* message, uint32_t id, sd_bus_error* error, int& result)
{
  JournalWindow* window;

  if ((window = FindWindow(id)) == nullptr)
  {
    result = sd_bus_error_setf(error, BUS_ERROR "NoWindow", "there is no window of id %u", id);
  }

  return window;
}

static int GetCursor(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  std::string cursor;
  JournalWindow* window;
  JournalRecord fields;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "u", &id)) < 0) ||
      ((window = TakeWindow(message, id, error, result)) == nullptr))
  {
    return result;
  }

  if (!(cursor = window->view->current()).empty())
  {
    // Empty without an entry under the cursor
    fields = ReadJournalFields(window->view->getFilter(), cursor);
  }

  return Reply(message, [&] (sd_bus_message* reply) { return AppendEntry(reply, fields); });
}

static int GetMarked(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  uint32_t limit;
  size_t total;
  JournalWindow* window;
  std::vector<std::string> cursors;
  std::vector<JournalRecord> records;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "uu", &id, &limit)) < 0) ||
      ((window = TakeWindow(message, id, error, result)) == nullptr))
  {
    return result;
  }

  cursors = window->view->pickedCursors((limit > 0) ? limit : BUS_MARKED, total);
  records = ReadJournalRecords(window->view->getFilter(), cursors);

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;

    // One after the other: the order of the arguments of a call is not defined, amd64 appends the last one first
    result = AppendEntries(reply, records);
    result = std::min(result, sd_bus_message_append(reply, "u", (uint32_t)total));
    return result;
  });
}

static int GetEntry(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  const char* cursor;
  JournalWindow* window;
  JournalRecord fields;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "us", &id, &cursor)) < 0) ||
      ((window = TakeWindow(message, id, error, result)) == nullptr))
  {
    return result;
  }

  // Empty when the entry is not in the journal of the window anymore
  fields = ReadJournalFields(window->view->getFilter(), cursor);
  return Reply(message, [&] (sd_bus_message* reply) { return AppendEntry(reply, fields); });
}

static int Query(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  bool older;
  size_t running;
  std::string cursor;
  std::string reason;
  Options options;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = ReadOptions(message, options)) < 0))
  {
    return result;
  }

  running = std::count_if(operations.begin(), operations.end(), [] (const Operation& item) { return item.kind == DO_QUERY; });

  if (running >= BUS_QUERIES)
  {
    return sd_bus_error_setf(error, BUS_ERROR "Busy", "%d queries go on already", BUS_QUERIES);
  }

  if (!MakeFilter(options, operation.filter, reason))
  {
    return sd_bus_error_set(error, BUS_ERROR "Invalid", reason.c_str());
  }

  // Before an entry going older by default, after one going newer, from the newest without either
  older  = true;
  cursor = options.texts.count("before") ? options.texts["before"] : "";

  if (cursor.empty() &&
      options.texts.count("after"))
  {
    cursor = options.texts["after"];
    older  = false;
  }

  operation.kind     = DO_QUERY;
  operation.deadline = GetQueryMilliseconds() + BUS_DEADLINE;
  operation.begun    = true;
  operation.query    = std::make_shared<JournalQuery>();

  if (!operation.query->start(operation.filter, cursor, older,
        options.numbers.count("limit") ? std::max<uint64_t>(1, options.numbers["limit"]) : BUS_LIMIT,
        options.numbers.count("scan")  ? std::max<uint64_t>(1, options.numbers["scan"])  : BUS_SCAN, reason))
  {
    return sd_bus_error_set(error, BUS_ERROR "Invalid", reason.c_str());
  }

  // Answered when read through, between the events
  operation.message = sd_bus_message_ref(message);
  operations.push_back(operation);
  return 1;
}

// A change of the screen or a menu, answered once no dialog is open and it is over
static int Defer(sd_bus_message* message, Operation& operation)
{
  operation.deadline = GetQueryMilliseconds() + BUS_DEADLINE;
  operation.begun    = false;
  operation.message  = sd_bus_message_ref(message);
  operations.push_back(operation);
  return 1;
}

static int ListActions(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  const char* cursor;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "us", &id, &cursor)) < 0))
  {
    return result;
  }

  operation.kind   = DO_ACTIONS;
  operation.window = id;
  operation.cursor = cursor;
  return Defer(message, operation);
}

// The methods of the plugins, the ones the caller may invoke: what each does and takes, as its plugin tells it, empty
// and system for a method that tells nothing
static int ListMethods(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  std::vector<const PluginMethod*> methods;

  if ((result = Allow(message, error)) < 0)
  {
    return result;
  }

  for (const PluginMethod* method : ListPluginMethods())
  {
    if (Allow(message, nullptr, method->plugin, method->method, "Invoke") >= 0)
    {
      methods.push_back(method);
    }
  }

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;

    result = std::min(0, sd_bus_message_open_container(reply, 'a', "a{sv}"));

    for (const PluginMethod* method : methods)
    {
      result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
      result = std::min(result, AppendString(reply, "plugin", method->plugin));
      result = std::min(result, AppendString(reply, "method", method->method));
      result = std::min(result, AppendString(reply, "description", method->description));
      result = std::min(result, AppendString(reply, "effect", method->effect));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "declared", "b", (int)method->declared));
      result = std::min(result, sd_bus_message_open_container(reply, 'e', "sv"));
      result = std::min(result, sd_bus_message_append(reply, "s", "arguments"));
      result = std::min(result, sd_bus_message_open_container(reply, 'v', "aa{sv}"));
      result = std::min(result, sd_bus_message_open_container(reply, 'a', "a{sv}"));

      for (const PluginArgument& argument : method->arguments)
      {
        result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
        result = std::min(result, AppendString(reply, "name", argument.name));
        result = std::min(result, AppendString(reply, "type", argument.type));
        result = std::min(result, AppendString(reply, "description", argument.description));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "required", "b", (int)argument.required));
        result = std::min(result, sd_bus_message_close_container(reply));
      }

      result = std::min(result, sd_bus_message_close_container(reply));
      result = std::min(result, sd_bus_message_close_container(reply));
      result = std::min(result, sd_bus_message_close_container(reply));
      result = std::min(result, sd_bus_message_close_container(reply));
    }

    result = std::min(result, sd_bus_message_close_container(reply));
    return result;
  });
}

static int Invoke(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  const char* name;
  const char* method;
  Operation operation;

  if (((result = sd_bus_message_read(message, "ss", &name, &method)) < 0) ||
      ((result = Allow(message, error, name, method)) < 0))
  {
    return result;
  }

  // Lua runs it once no dialog is open and nothing else runs there, as the menu of ListActions()
  operation.kind   = DO_INVOKE;
  operation.plugin = name;
  operation.method = method;
  return Defer(message, operation);
}

static int Mark(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  int clear;
  uint32_t id;
  char** list;
  uint64_t stamp;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "u", &id)) < 0) ||
      ((result = sd_bus_message_read_strv(message, &list)) < 0) ||
      ((result = sd_bus_message_read(message, "b", &clear)) < 0))
  {
    return result;
  }

  operation.time = UINT64_MAX;

  for (char** item = list; (item != nullptr) && (*item != nullptr); item ++)
  {
    // The history down to the oldest of them comes in, a cursor that does not tell its time brings in all there is
    stamp          = GetCursorTime(*item);
    operation.time = std::min(operation.time, stamp);
    operation.cursors.insert(*item);
    free(*item);
  }

  free(list);

  if (operation.cursors.empty())
  {
    return sd_bus_error_set(error, BUS_ERROR "Invalid", "Mark() takes cursors of entries");
  }

  operation.kind   = DO_MARK;
  operation.window = id;
  operation.clear  = clear;
  return Defer(message, operation);
}

static int GoTo(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  const char* cursor;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "us", &id, &cursor)) < 0))
  {
    return result;
  }

  if ((operation.time = GetCursorTime(cursor)) == 0)
  {
    return sd_bus_error_set(error, BUS_ERROR "Invalid", "the cursor does not tell the time of its entry");
  }

  operation.kind   = DO_GOTO;
  operation.window = id;
  operation.cursor = cursor;
  return Defer(message, operation);
}

static int Seek(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  uint64_t time;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "ut", &id, &time)) < 0))
  {
    return result;
  }

  operation.kind   = DO_SEEK;
  operation.window = id;
  operation.time   = time;
  return Defer(message, operation);
}

static int OpenWindow(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  std::string reason;
  Options options;
  Operation operation;

  if (((result = Allow(message, error)) < 0) ||
      ((result = ReadOptions(message, options)) < 0))
  {
    return result;
  }

  if (!MakeFilter(options, operation.filter, reason))
  {
    return sd_bus_error_set(error, BUS_ERROR "Invalid", reason.c_str());
  }

  operation.kind = DO_OPEN;
  return Defer(message, operation);
}

static int ListWatches(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  std::vector<JournalWatch*> watches;

  if ((result = Allow(message, error)) < 0)
  {
    return result;
  }

  watches = ListPluginWatches();

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;

    result = std::min(0, sd_bus_message_open_container(reply, 'a', "a{sv}"));

    for (JournalWatch* watch : watches)
    {
      result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "id", "u", (uint32_t)watch->number));
      result = std::min(result, AppendString(reply, "plugin", watch->plugin));
      result = std::min(result, AppendString(reply, "key", watch->key));
      result = std::min(result, AppendString(reply, "title", watch->title));
      result = std::min(result, AppendString(reply, "state", (watch->state == WATCH_FAILED) ? "failed" : (watch->state == WATCH_STOPPED) ? "stopped" : "active"));
      result = std::min(result, AppendString(reply, "status", IsText(watch->status) ? watch->status : ""));
      result = std::min(result, AppendString(reply, "reason", IsText(watch->reason) ? watch->reason : ""));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "gaps", "u", (uint32_t)watch->gaps));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "unread", "u", (uint32_t)watch->unread()));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "latest", "t", watch->latest));
      result = std::min(result, sd_bus_message_close_container(reply));
    }

    result = std::min(result, sd_bus_message_close_container(reply));
    return result;
  });
}

static int GetWatchLines(sd_bus_message* message, void*, sd_bus_error* error)
{
  int result;
  uint32_t id;
  JournalWatch* watch;

  if (((result = Allow(message, error)) < 0) ||
      ((result = sd_bus_message_read(message, "u", &id)) < 0))
  {
    return result;
  }

  if ((watch = FindPluginWatch((int)id)) == nullptr)
  {
    return sd_bus_error_setf(error, BUS_ERROR "NoWatch", "there is no watch of id %u", id);
  }

  return Reply(message, [&] (sd_bus_message* reply)
  {
    int result;

    result = std::min(0, sd_bus_message_open_container(reply, 'a', "a{sv}"));

    // The newest first, as the window shows them
    for (std::deque<JournalWatchLine>::const_reverse_iterator line = watch->lines.rbegin(); line != watch->lines.rend(); ++ line)
    {
      result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "number", "t", line->number));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "moment", "t", line->moment));
      result = std::min(result, AppendString(reply, "text", IsText(line->text) ? line->text : ""));
      result = std::min(result, sd_bus_message_append(reply, "{sv}", "unread", "b", (int)line->unread));

      if (!line->entry.cursor.empty())
      {
        result = std::min(result, AppendString(reply, "cursor", line->entry.cursor));
        result = std::min(result, sd_bus_message_append(reply, "{sv}", "realtime", "t", line->entry.realtime));
      }

      result = std::min(result, sd_bus_message_close_container(reply));
    }

    result = std::min(result, sd_bus_message_close_container(reply));
    return result;
  });
}

const sd_bus_vtable Table[] =
{
  SD_BUS_VTABLE_START(0),
  SD_BUS_METHOD_WITH_NAMES("Describe",      "",      SD_BUS_PARAM(),                                    "a{sv}",  SD_BUS_PARAM(instance),              Describe,      SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("ListWindows",   "",      SD_BUS_PARAM(),                                    "aa{sv}", SD_BUS_PARAM(windows),               ListWindows,   SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("GetCursor",     "u",     SD_BUS_PARAM(id),                                  "a{sv}",  SD_BUS_PARAM(entry),                 GetCursor,     SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("GetMarked",     "uu",    SD_BUS_PARAM(id) SD_BUS_PARAM(limit),              "aa{sv}u", SD_BUS_PARAM(entries) SD_BUS_PARAM(total), GetMarked, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("GetEntry",      "us",    SD_BUS_PARAM(id) SD_BUS_PARAM(cursor),             "a{sv}",  SD_BUS_PARAM(entry),                 GetEntry,      SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("Query",         "a{sv}", SD_BUS_PARAM(options),                             "aa{sv}s", SD_BUS_PARAM(entries) SD_BUS_PARAM(stopped), Query,  SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("ListActions",   "us",    SD_BUS_PARAM(id) SD_BUS_PARAM(cursor),             "aa{sv}", SD_BUS_PARAM(actions),               ListActions,   SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("ListWatches",   "",      SD_BUS_PARAM(),                                    "aa{sv}", SD_BUS_PARAM(watches),               ListWatches,   SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("GetWatchLines", "u",     SD_BUS_PARAM(id),                                  "aa{sv}", SD_BUS_PARAM(lines),                 GetWatchLines, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("Mark",          "uasb",  SD_BUS_PARAM(id) SD_BUS_PARAM(cursors) SD_BUS_PARAM(clear), "us", SD_BUS_PARAM(marked) SD_BUS_PARAM(stopped), Mark, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("GoTo",          "us",    SD_BUS_PARAM(id) SD_BUS_PARAM(cursor),             "bs",     SD_BUS_PARAM(found) SD_BUS_PARAM(stopped), GoTo,    SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("Seek",          "ut",    SD_BUS_PARAM(id) SD_BUS_PARAM(time),               "bs",     SD_BUS_PARAM(reached) SD_BUS_PARAM(stopped), Seek,  SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("OpenWindow",    "a{sv}", SD_BUS_PARAM(options),                             "u",      SD_BUS_PARAM(id),                    OpenWindow,    SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("ListMethods",   "",      SD_BUS_PARAM(),                                    "aa{sv}", SD_BUS_PARAM(methods),               ListMethods,   SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD_WITH_NAMES("Invoke",        "ssa{sv}", SD_BUS_PARAM(plugin) SD_BUS_PARAM(method) SD_BUS_PARAM(values), "a{sv}", SD_BUS_PARAM(answer), Invoke,   SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};
