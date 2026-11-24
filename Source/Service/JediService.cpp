// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// The programming interface of TurboJedi on the system bus: the name, the windows, the calls going on and their steps

#include "ServicePrivate.h"

sd_bus* bus;
std::list<Operation> operations;
bool delegated;             // A root instance started by sudo answers the user of sudo as well
uid_t delegate;
uint64_t started;           // Microseconds of CLOCK_REALTIME

static JournalSource* source;
static std::string notice;  // What a caller has done on the screen last
static bool noticed;        // The notice has changed

// Helpers

struct JournalWindowSearch
{
  unsigned id;
  JournalWindow* window;
};

static void MatchWindow(TView* view, void* argument)
{
  JournalWindow* window;
  JournalWindowSearch* search;

  search = static_cast<JournalWindowSearch*>(argument);

  if (((window = dynamic_cast<JournalWindow*>(view)) != nullptr) &&
      (window->view->id() == search->id))
  {
    search->window = window;
  }
}

JournalWindow* FindWindow(unsigned id)
{
  JournalWindowSearch search;

  search = { id, nullptr };

  if (TProgram::deskTop != nullptr)
  {
    TProgram::deskTop->forEach(MatchWindow, &search);
  }

  return search.window;
}

void CollectWindows(TView* view, void* argument)
{
  JournalWindow* window;

  if ((window = dynamic_cast<JournalWindow*>(view)) != nullptr)
  {
    // The desktop walks from the front
    static_cast<std::vector<JournalWindow*>*>(argument)->push_back(window);
  }
}

// The window the user has in front: of a journal, a timeline, the list of watches or the lines of one. A dialog or a
// menu over it is not a window of these
static bool IsFrontWindow(TView* view, void*)
{
  return (dynamic_cast<JournalWindow*>(view) != nullptr) ||
         (dynamic_cast<TimelineWindow*>(view) != nullptr) ||
         (dynamic_cast<WatchWindow*>(view) != nullptr) ||
         (dynamic_cast<WatchLogWindow*>(view) != nullptr);
}

TView* FindFrontWindow()
{
  // The desktop goes from the front
  return (TProgram::deskTop != nullptr) ? TProgram::deskTop->firstThat(IsFrontWindow, nullptr) : nullptr;
}

// Descriptors the process has open, the one of the count itself left out
size_t CountDescriptors()
{
  DIR* directory;
  size_t count;
  struct dirent* entry;

  if ((directory = opendir("/proc/self/fd")) == nullptr)
  {
    return 0;
  }

  for (count = 0; (entry = readdir(directory)) != nullptr; )
  {
    count += (entry->d_name[0] != '.') ? 1 : 0;
  }

  closedir(directory);
  return (count > 0) ? (count - 1) : 0;
}

// The title of a window of a journal as the frame shows it, without the signs of following and the order
std::string TitleWindow(JournalWindow* window)
{
  std::string title;

  title  = (window->title != nullptr) ? window->title : "";
  title += (*window->view->name() != '\0') ? (std::string(" ") + window->view->name()) : std::string();
  return title;
}

// How a notice names a window: by its number on the screen, by its id without one
static std::string NameWindow(JournalWindow* window)
{
  return (window->number != wnNoNumber) ? std::to_string(window->number) : ("of id " + std::to_string(window->view->id()));
}

static void Notify(const std::string& text)
{
  notice  = "API: " + text;
  noticed = true;
  LogConsole("bus", text, false);
}

// t= of a cursor in hex, the time of its entry; zero when it does not tell
uint64_t GetCursorTime(const std::string& cursor)
{
  size_t place;

  place = cursor.find(";t=");
  return (place != std::string::npos) ? strtoull(cursor.c_str() + place + 3, nullptr, 16) : 0;
}

// The calls going on

static void ReplyError(Operation& operation, const char* name, const std::string& text)
{
  sd_bus_reply_method_errorf(operation.message, name, "%s", text.c_str());
}

// One step of a call answered later: true once it is answered
static bool Step(Operation& operation, bool paused, int64_t now)
{
  uint32_t id;
  bool over;
  bool found;
  bool windowed;
  size_t count;
  char moment[32];
  time_t seconds;
  struct tm parts;
  std::string reason;
  sd_bus_message* reply;
  JournalWindow* window;
  JournalWindowRequest request;
  const PluginMethod* described;
  std::vector<PluginAction> actions;

  if (operation.kind == DO_QUERY)
  {
    // Reading goes on while a dialog is open, only the screen waits
    over = operation.query->step(std::min(now + BUS_STEP, operation.deadline));

    if ((!over) &&
        (now < operation.deadline))
    {
      return false;
    }

    if (!over)
    {
      operation.query->stop("time");
    }

    Answer(operation, [&] (sd_bus_message* reply)
    {
      int result;

      result = AppendEntries(reply, operation.query->records);
      result = std::min(result, sd_bus_message_append(reply, "s", (operation.query->stopped != nullptr) ? operation.query->stopped : ""));
      return result;
    });

    return true;
  }

  if ((paused) ||
      (!IsPluginIdle()))
  {
    // Not under a dialog the user answers, nor while Lua runs: a call that has begun to bring the history in waits as
    // well, the window would come to the front over the dialog when it is done
    if (now >= operation.deadline)
    {
      ReplyError(operation, BUS_ERROR "Busy", "a dialog is open in TurboJedi");
      return true;
    }

    return false;
  }

  operation.begun = true;

  if (operation.kind == DO_INVOKE)
  {
    if (sd_bus_message_new_method_return(operation.message, &reply) < 0)
    {
      ReplyError(operation, SD_BUS_ERROR_NO_MEMORY, "the answer cannot be made");
      return true;
    }

    // The values checked by the method as it is now: the plugins may have been loaded again while the call has waited
    if ((described = FindPluginMethod(operation.plugin, operation.method)) == nullptr)
    {
      ReplyError(operation, BUS_ERROR "NoMethod", "plugin " + operation.plugin + " has no method " + operation.method);
      sd_bus_message_unref(reply);
      return true;
    }

    if ((!CheckValues(operation.message, described, windowed, id, reason)) ||
        (sd_bus_message_rewind(operation.message, 1) < 0) ||
        (sd_bus_message_skip(operation.message, "ss") < 0))
    {
      ReplyError(operation, BUS_ERROR "InvalidArgs", reason);
      sd_bus_message_unref(reply);
      return true;
    }

    if ((windowed) &&
        ((window = FindWindow(id)) == nullptr))
    {
      ReplyError(operation, BUS_ERROR "NoWindow", "there is no window of id " + std::to_string(id));
      sd_bus_message_unref(reply);
      return true;
    }

    switch (InvokePluginMethod(operation.plugin, operation.method, operation.message, reply, windowed ? window->view : nullptr, operation.deadline, reason))
    {
      case PLUGIN_METHOD_NONE:
        ReplyError(operation, BUS_ERROR "NoMethod", "plugin " + operation.plugin + " has no method " + operation.method);
        break;

      case PLUGIN_METHOD_FAILED:
        ReplyError(operation, BUS_ERROR "Failed", reason);
        break;

      default:
        // A plugin may do anything with it, the status line tells who has been called
        Notify("called " + operation.method + " of plugin " + operation.plugin);
        sd_bus_send(nullptr, reply, nullptr);
        break;
    }

    sd_bus_message_unref(reply);
    return true;
  }

  if (operation.kind == DO_OPEN)
  {
    request.filter = &operation.filter;
    request.view   = nullptr;
    message(TProgram::application, evBroadcast, cmJournalOpenWatch, &request);

    if (request.view == nullptr)
    {
      ReplyError(operation, BUS_ERROR "Invalid", "the window cannot be opened");
      return true;
    }

    window = FindWindow(request.view->id());
    Notify("opened window " + ((window != nullptr) ? NameWindow(window) : std::to_string(request.view->id())));
    sd_bus_reply_method_return(operation.message, "u", (uint32_t)request.view->id());
    return true;
  }

  if ((window = FindWindow(operation.window)) == nullptr)
  {
    // Closed by the user while the call has waited
    ReplyError(operation, BUS_ERROR "NoWindow", "there is no window of id " + std::to_string(operation.window));
    return true;
  }

  if (operation.kind == DO_ACTIONS)
  {
    if (!ListPluginActions(window->view, operation.cursor, actions))
    {
      ReplyError(operation, BUS_ERROR "NoEntry", "the entry is not in the journal of the window");
      return true;
    }

    Answer(operation, [&] (sd_bus_message* reply)
    {
      int result;

      result = std::min(0, sd_bus_message_open_container(reply, 'a', "a{sv}"));

      for (const PluginAction& action : actions)
      {
        result = std::min(result, sd_bus_message_open_container(reply, 'a', "{sv}"));
        result = std::min(result, AppendString(reply, "title", IsText(action.title) ? action.title : ""));
        result = std::min(result, AppendString(reply, "plugin", action.plugin));

        if (!action.path.empty())
        {
          // The submenus it is in, the outer one first
          std::vector<std::string> path;

          for (const std::string& title : action.path)
          {
            path.push_back(IsText(title) ? title : "");
          }

          result = std::min(result, AppendStrings(reply, "path", path));
        }

        result = std::min(result, sd_bus_message_close_container(reply));
      }

      result = std::min(result, sd_bus_message_close_container(reply));
      return result;
    });

    return true;
  }

  // What has come first, then the history down to the time, by steps of the same budget, without a dialog; a window
  // goes to it only when it is there, as the window itself would bring it in at once
  over = window->view->settle(std::min(now + BUS_STEP, operation.deadline)) &&
         window->view->bring(operation.time, std::min(now + BUS_STEP, operation.deadline));

  if ((!over) &&
      (now < operation.deadline))
  {
    return false;
  }

  if (!over)
  {
    // The call has no more time: as far as the window has come
    operation.time = window->view->earliest();
  }

  switch (operation.kind)
  {
    case DO_MARK:
      count = window->view->markLoaded(operation.cursors, operation.clear);
      Notify("marked " + std::to_string(count) + ((count == 1) ? " entry" : " entries") + " in window " + NameWindow(window));
      sd_bus_reply_method_return(operation.message, "us", (uint32_t)count, over ? "" : "time");
      break;

    case DO_GOTO:
      found = window->view->reach(operation.cursor, operation.time);
      window->select();
      Notify(std::string(found ? "cursor on an entry" : "cursor near an entry") + " in window " + NameWindow(window));
      sd_bus_reply_method_return(operation.message, "bs", (int)found, over ? "" : "time");
      break;

    case DO_SEEK:
      window->view->jump(operation.time);
      window->select();
      seconds = operation.time / 1000000;
      (window->view->getFilter().columns & JOURNAL_COLUMN_UTC) ? gmtime_r(&seconds, &parts) : localtime_r(&seconds, &parts);
      strftime(moment, sizeof(moment), "%Y-%m-%d %H:%M:%S", &parts);
      Notify("window " + NameWindow(window) + " at " + moment);
      sd_bus_reply_method_return(operation.message, "bs", (int)over, over ? "" : "time");
      break;
  }

  return true;
}

bool OpenService(const std::string& allow, std::vector<std::string>& problems)
{
  DIR* directory;
  struct dirent* item;
  std::string found;
  std::vector<std::string> files;
  int result;
  char name[128];
  const char* sudo;
  struct timespec now;

  clock_gettime(CLOCK_REALTIME, &now);
  started   = now.tv_sec * 1000000ULL + now.tv_nsec / 1000;
  sudo      = getenv("SUDO_UID");
  delegated = (getuid() == 0) && (sudo != nullptr) && (*sudo != '\0') && (strtoul(sudo, nullptr, 10) != 0);
  delegate  = delegated ? strtoul(sudo, nullptr, 10) : 0;

  if ((directory = opendir(ALLOW_SYSTEM)) != nullptr)
  {
    while ((item = readdir(directory)) != nullptr)
    {
      // Files of packages, *.conf in the order of their names
      found = item->d_name;
      if ((found.size() > 5) && (found[0] != '.') && (found.compare(found.size() - 5, 5, ".conf") == 0)) files.push_back(found);
    }

    closedir(directory);
    std::sort(files.begin(), files.end());
  }

  for (const std::string& file : files)
  {
    ReadPermissions(std::string(ALLOW_SYSTEM "/") + file, problems);
  }

  // Of the user last, it adds to the ones of the system
  ReadPermissions(allow, problems);

  if (sd_bus_open_system(&bus) < 0)
  {
    // No system bus, TurboJedi goes on without
    bus = nullptr;
    return false;
  }

  // The name tells the user the instance answers, the one of sudo for an instance of root it started
  snprintf(name, sizeof(name), BUS_PREFIX ".U%u.P%u", (unsigned)(delegated ? delegate : getuid()), (unsigned)getpid());

  if (((result = sd_bus_add_object_vtable(bus, nullptr, BUS_PATH, BUS_INTERFACE, Table, nullptr)) < 0) ||
      ((result = sd_bus_request_name(bus, name, 0)) < 0))
  {
    // Without the policy of the package the bus does not let the name be taken: not seen there, nothing else changes
    sd_bus_flush_close_unref(bus);
    bus = nullptr;
    return false;
  }

  // The calls wake the application as the journals do
  source = CreateJournalSource(sd_bus_get_fd(bus));
  return true;
}

int PollService(bool paused)
{
  int count;
  int64_t now;
  int64_t wait;
  bool working;
  std::list<Operation>::iterator operation;

  if (bus == nullptr)
  {
    return -1;
  }

  for (count = 0; (count < 64) && (sd_bus_process(bus, nullptr) > 0); count ++)
  {
    // The calls that have come, a few at a time
  }

  now     = GetQueryMilliseconds();
  wait    = -1;
  working = false;

  for (operation = operations.begin(); operation != operations.end(); )
  {
    if (Step(*operation, paused, now))
    {
      sd_bus_message_unref(operation->message);
      operation = operations.erase(operation);
      continue;
    }

    // A query reads on under a dialog, a change of the screen goes on once there is none. One that waits for a dialog to
    // close needs no look until its deadline: the user closing the dialog wakes the application
    working = working || (operation->kind == DO_QUERY) || ((operation->begun) && (!paused));
    wait    = ((wait < 0) || (operation->deadline - now < wait)) ? std::max<int64_t>(operation->deadline - now, 0) : wait;
    ++ operation;
  }

  // The answers go out at once
  sd_bus_flush(bus);

  if ((working) ||
      (count >= 64))
  {
    // Work goes on between the events, or more calls have come than a round takes
    return 0;
  }

  // The next call wakes the application by its descriptor, the deadline of one that waits by the time
  return (int)wait;
}

std::string DescribeServiceNotice()
{
  return notice;
}

bool ClearServiceNotice()
{
  if (notice.empty())
  {
    return false;
  }

  notice.clear();
  noticed = true;
  return true;
}

bool TakeServiceChange()
{
  bool result;

  result  = noticed;
  noticed = false;
  return result;
}

void CloseService()
{
  if (bus == nullptr)
  {
    return;
  }

  for (Operation& operation : operations)
  {
    // The callers are told, not left waiting
    ReplyError(operation, BUS_ERROR "Busy", "TurboJedi exits");
    sd_bus_message_unref(operation.message);
  }

  operations.clear();
  ReleaseJournalSource(source);
  sd_bus_flush_close_unref(bus);
  bus    = nullptr;
  source = nullptr;
}
