// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include <tvision/tv.h>

#include "Version.h"
#include "JediService.h"
#include "JediScript.h"
#include "JournalWindow.h"
#include "TimelineWindow.h"
#include "WatchWindow.h"
#include "JournalView.h"
#include "JournalWatch.h"
#include "JournalQuery.h"
#include "JournalTools.h"
#include "JournalSource.h"
#include "JournalExpression.h"
#include "Commands.h"

#include <systemd/sd-bus.h>

#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/resource.h>

#include <map>
#include <array>
#include <deque>
#include <set>
#include <list>
#include <memory>
#include <vector>
#include <algorithm>

#define BUS_PREFIX     "me.burnaway.TurboJedi"
#define BUS_PATH       "/me/burnaway/TurboJedi"
#define BUS_INTERFACE  "me.burnaway.TurboJedi"
#define BUS_ERROR      "me.burnaway.TurboJedi.Error."
#define BUS_VERSION    1       // Of the interface: fields and methods are only added within it
#define BUS_DEADLINE   30000   // Milliseconds a call may take, the wait for a dialog to close included
#define BUS_STEP       10      // Milliseconds of reading in a round between the events
#define BUS_QUERIES    4       // Queries going on at once
#define BUS_LIMIT      100     // Entries a query gives by default, as query() of a plugin
#define BUS_SCAN       100000  // Entries it looks at by default
#define BUS_MARKED     1000    // Entries GetMarked() gives by default
#define ALLOW_SYSTEM   "/etc/turbojedi/allow.d"  // *.conf of the packages and of root, before the file of the user

enum
{
  DO_QUERY,
  DO_MARK,
  DO_GOTO,
  DO_SEEK,
  DO_OPEN,
  DO_ACTIONS,
  DO_INVOKE
};

// A call answered later: a query read by steps, or a change of the screen that waits for a dialog to close and may read
// the history of a window by steps
struct Operation
{
  int kind;
  sd_bus_message* message;
  int64_t deadline;  // Milliseconds of CLOCK_MONOTONIC the whole call has
  bool begun;        // A change of the screen has started, no dialog was open then
  unsigned window;   // Its id
  std::set<std::string> cursors;
  std::string cursor;
  uint64_t time;     // Microseconds: the oldest entry to bring in, the entry to go to, the moment
  bool clear;
  std::string plugin;  // Of Invoke(), its values stay in the message until the plugin is called
  std::string method;
  JournalFilter filter;
  std::shared_ptr<JournalQuery> query;
};

static const char* const FieldNames[JOURNAL_FIELD_COUNT] = { "unit", "identifier", "command", "transport", "boot", "user", "machine" };
static const char* const LevelNames[] = { "emerg", "alert", "crit", "err", "warning", "notice", "info", "debug" };
static const char LevelLetters[] = "EACRWNID";

static sd_bus* bus;
static JournalSource* source;
static std::list<Operation> operations;
static std::string notice;  // What a caller has done on the screen last
static bool noticed;        // The notice has changed
static bool delegated;      // A root instance started by sudo answers the user of sudo as well
static uid_t delegate;
static uint64_t started;    // Microseconds of CLOCK_REALTIME

// A user answered besides the own one and root, by the files of allow: every method, or the ones named
struct Permission
{
  bool group;                     // id is a GID: the members of the group the system tells, the primary one included
  unsigned id;
  bool limited;                   // methods= has been given: the ones it names only, a mistyped one is not all of them
  std::set<std::string> methods;
};

static std::vector<Permission> permissions;

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

static JournalWindow* FindWindow(unsigned id)
{
  JournalWindowSearch search;

  search = { id, nullptr };

  if (TProgram::deskTop != nullptr)
  {
    TProgram::deskTop->forEach(MatchWindow, &search);
  }

  return search.window;
}

static void CollectWindows(TView* view, void* argument)
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

static TView* FindFrontWindow()
{
  // The desktop goes from the front
  return (TProgram::deskTop != nullptr) ? TProgram::deskTop->firstThat(IsFrontWindow, nullptr) : nullptr;
}

// Descriptors the process has open, the one of the count itself left out
static size_t CountDescriptors()
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
static std::string TitleWindow(JournalWindow* window)
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
static uint64_t GetCursorTime(const std::string& cursor)
{
  size_t place;

  place = cursor.find(";t=");
  return (place != std::string::npos) ? strtoull(cursor.c_str() + place + 3, nullptr, 16) : 0;
}

// A value D-Bus takes as a string: UTF-8 without NUL, each character in its shortest form, no surrogates, nothing past
// U+10FFFF; sd-bus refuses the message otherwise
static bool IsText(const std::string& value)
{
  size_t index;
  size_t count;
  uint32_t code;
  unsigned char byte;

  for (index = 0; index < value.size(); index += count + 1)
  {
    byte = value[index];

    if      (byte == 0x00)          return false;
    else if (byte <  0x80)          count = 0, code = byte;
    else if ((byte & 0xe0) == 0xc0) count = 1, code = byte & 0x1f;
    else if ((byte & 0xf0) == 0xe0) count = 2, code = byte & 0x0f;
    else if ((byte & 0xf8) == 0xf0) count = 3, code = byte & 0x07;
    else                            return false;

    if (count >= value.size() - index)
    {
      // Cut short
      return false;
    }

    for (size_t next = 1; next <= count; next ++)
    {
      byte = value[index + next];

      if ((byte & 0xc0) != 0x80)
      {
        return false;
      }

      code = (code << 6) | (byte & 0x3f);
    }

    if (((count == 1) && (code < 0x80)) ||
        ((count == 2) && (code < 0x800)) ||
        ((count == 3) && (code < 0x10000)) ||
        ((code >= 0xd800) && (code <= 0xdfff)) ||
        (code > 0x10ffff))
    {
      return false;
    }
  }

  return true;
}

// Appending to a message: every call adds to the result, an error of sd-bus stays negative

static int AppendString(sd_bus_message* message, const char* key, const std::string& value)
{
  return sd_bus_message_append(message, "{sv}", key, "s", value.c_str());
}

static int AppendStrings(sd_bus_message* message, const char* key, const std::vector<std::string>& values)
{
  int result;

  result = std::min(0, sd_bus_message_open_container(message, 'e', "sv"));
  result = std::min(result, sd_bus_message_append(message, "s", key));
  result = std::min(result, sd_bus_message_open_container(message, 'v', "as"));
  result = std::min(result, sd_bus_message_open_container(message, 'a', "s"));

  for (const std::string& value : values)
  {
    result = std::min(result, sd_bus_message_append(message, "s", value.c_str()));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  result = std::min(result, sd_bus_message_close_container(message));
  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

// The filter of a window or a timeline, the keys of ListWindows()
static int AppendFilter(sd_bus_message* message, const JournalFilter& filter)
{
  int result;
  std::string levels;

  for (int index = 0; index < 8; index ++)
  {
    levels += (filter.levels & (1 << index)) ? LevelLetters[index] : '-';
  }

  result = std::min(0, AppendString(message, "directory", filter.directory));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "merge", "b", (int)filter.merge));
  result = std::min(result, AppendString(message, "text", filter.text));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "regex", "b", (int)((filter.mode & JOURNAL_TEXT_EXPRESSION) != 0)));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "exclude", "b", (int)((filter.mode & JOURNAL_TEXT_EXCLUDE) != 0)));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "case", "b", (int)((filter.mode & JOURNAL_TEXT_SENSITIVE) != 0)));
  result = std::min(result, AppendString(message, "levels", levels));
  result = std::min(result, AppendStrings(message, "matches", filter.matches));

  for (int field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (!filter.values[field].empty())
    {
      // Only the ones the filter has
      result = std::min(result, AppendString(message, FieldNames[field], filter.values[field]));
    }
  }

  if (filter.pid != 0)          result = std::min(result, sd_bus_message_append(message, "{sv}", "pid", "u", (uint32_t)filter.pid));
  if (!filter.since.empty())    result = std::min(result, AppendString(message, "since", filter.since));
  if (!filter.until.empty())    result = std::min(result, AppendString(message, "until", filter.until));

  return result;
}

// An entry as a{sv}: a field once is s, several times as, binary ay or aay, the timestamps t
static int AppendEntry(sd_bus_message* message, const JournalRecord& fields)
{
  int result;
  bool text;
  std::vector<std::string> names;
  std::map<std::string, std::vector<std::string>> values;

  for (const std::pair<std::string, std::string>& field : fields)
  {
    if (values.find(field.first) == values.end())
    {
      // In the order they come
      names.push_back(field.first);
    }

    values[field.first].push_back(field.second);
  }

  result = std::min(0, sd_bus_message_open_container(message, 'a', "{sv}"));

  for (const std::string& name : names)
  {
    const std::vector<std::string>& list = values[name];

    if ((name == "__REALTIME_TIMESTAMP") ||
        (name == "__MONOTONIC_TIMESTAMP"))
    {
      result = std::min(result, sd_bus_message_append(message, "{sv}", name.c_str(), "t", (uint64_t)strtoull(list[0].c_str(), nullptr, 10)));
      continue;
    }

    text   = std::all_of(list.begin(), list.end(), IsText);
    result = std::min(result, sd_bus_message_open_container(message, 'e', "sv"));
    result = std::min(result, sd_bus_message_append(message, "s", name.c_str()));

    if (list.size() == 1)
    {
      result = std::min(result, sd_bus_message_open_container(message, 'v', text ? "s" : "ay"));
      result = std::min(result, text ? sd_bus_message_append(message, "s", list[0].c_str()) : sd_bus_message_append_array(message, 'y', list[0].data(), list[0].size()));
      result = std::min(result, sd_bus_message_close_container(message));
    }
    else
    {
      result = std::min(result, sd_bus_message_open_container(message, 'v', text ? "as" : "aay"));
      result = std::min(result, sd_bus_message_open_container(message, 'a', text ? "s" : "ay"));

      for (const std::string& value : list)
      {
        result = std::min(result, text ? sd_bus_message_append(message, "s", value.c_str()) : sd_bus_message_append_array(message, 'y', value.data(), value.size()));
      }

      result = std::min(result, sd_bus_message_close_container(message));
      result = std::min(result, sd_bus_message_close_container(message));
    }

    result = std::min(result, sd_bus_message_close_container(message));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

static int AppendEntries(sd_bus_message* message, const std::vector<JournalRecord>& records)
{
  int result;

  result = std::min(0, sd_bus_message_open_container(message, 'a', "a{sv}"));

  for (const JournalRecord& record : records)
  {
    result = std::min(result, AppendEntry(message, record));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

// A reply made by a function on a new message, sent at once
template <typename Function> static int Reply(sd_bus_message* call, Function make)
{
  int result;
  sd_bus_message* reply;

  if ((result = sd_bus_message_new_method_return(call, &reply)) < 0)
  {
    return result;
  }

  if (((result = make(reply)) >= 0) &&
      ((result = sd_bus_send(bus, reply, nullptr)) >= 0))
  {
    result = 1;
  }

  sd_bus_message_unref(reply);
  return result;
}

// Who calls: the real UID the bus tells, never the name. Its own user, root, and the user of sudo of a root instance for
// as long as it lives
// Of the groups the system database tells for the user of the UID, not the ones of the process the bus may not tell
static bool IsMember(uid_t uid, gid_t gid)
{
  int count;
  char buffer[4096];
  struct passwd entry;
  struct passwd* user;
  std::vector<gid_t> groups;

  if ((getpwuid_r(uid, &entry, buffer, sizeof(buffer), &user) != 0) ||
      (user == nullptr))
  {
    return false;
  }

  count = 64;
  groups.resize(count);

  if (getgrouplist(user->pw_name, user->pw_gid, groups.data(), &count) < 0)
  {
    // count tells how many there are
    groups.resize(count);

    if (getgrouplist(user->pw_name, user->pw_gid, groups.data(), &count) < 0)
    {
      return false;
    }
  }

  groups.resize(count);
  return std::find(groups.begin(), groups.end(), gid) != groups.end();
}

// A method a permission names: one of the bus, or Invoke:plugin and Invoke:plugin.method for Invoke() of those only
static bool Permits(const Permission& permission, const char* member, const std::string& plugin, const std::string& method)
{
  if ((!permission.limited) ||
      (permission.methods.count(member) != 0))
  {
    return true;
  }

  return (strcmp(member, "Invoke") == 0) &&
         (!plugin.empty()) &&
         ((permission.methods.count("Invoke:" + plugin) != 0) ||
          (permission.methods.count("Invoke:" + plugin + "." + method) != 0));
}

// Invoke() tells its plugin and method, the files of allow may name them; ListMethods() asks it for every method as if
// Invoke() were called, the member tells which one
static int Allow(sd_bus_message* message, sd_bus_error* error, const std::string& plugin = std::string(), const std::string& method = std::string(), const char* member = nullptr)
{
  int result;
  uid_t uid;
  sd_bus_creds* creds;

  creds = nullptr;

  if (((result = sd_bus_query_sender_creds(message, SD_BUS_CREDS_EUID, &creds)) < 0) ||
      ((result = sd_bus_creds_get_euid(creds, &uid)) < 0))
  {
    sd_bus_creds_unref(creds);
    return sd_bus_error_set(error, BUS_ERROR "Denied", "the bus does not tell who calls");
  }

  sd_bus_creds_unref(creds);

  if ((uid == 0) ||
      (uid == getuid()) ||
      ((delegated) && (uid == delegate)))
  {
    return 0;
  }

  for (const Permission& permission : permissions)
  {
    if ((permission.group ? IsMember(uid, permission.id) : (permission.id == uid)) &&
        (Permits(permission, (member != nullptr) ? member : sd_bus_message_get_member(message), plugin, method)))
    {
      // A service or another user the files let in, for the methods they name
      return 0;
    }
  }

  return sd_bus_error_setf(error, BUS_ERROR "Denied", "TurboJedi %d answers its own user and root only", (int)getpid());
}

// Options of Query() and OpenWindow(), a{sv}; ones of other names or types are left alone
struct Options
{
  std::map<std::string, std::string> texts;
  std::map<std::string, bool> flags;
  std::map<std::string, uint64_t> numbers;
  std::vector<std::string> matches;
};

static int ReadOptions(sd_bus_message* message, Options& options)
{
  int result;
  char type;
  char** list;
  int flag;
  uint32_t small;
  uint64_t large;
  int32_t signed32;
  int64_t signed64;
  const char* key;
  const char* contents;
  const char* text;

  if ((result = sd_bus_message_enter_container(message, 'a', "{sv}")) < 0)
  {
    return result;
  }

  while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0)
  {
    if (((result = sd_bus_message_read(message, "s", &key)) < 0) ||
        ((result = sd_bus_message_peek_type(message, &type, &contents)) < 0) ||
        ((result = sd_bus_message_enter_container(message, 'v', contents)) < 0))
    {
      return result;
    }

    if (strcmp(contents, "s") == 0)
    {
      result = sd_bus_message_read(message, "s", &text);
      options.texts[key] = text;
    }
    else if (strcmp(contents, "b") == 0)
    {
      result = sd_bus_message_read(message, "b", &flag);
      options.flags[key] = flag;
    }
    else if (strcmp(contents, "u") == 0)
    {
      result = sd_bus_message_read(message, "u", &small);
      options.numbers[key] = small;
    }
    else if (strcmp(contents, "t") == 0)
    {
      result = sd_bus_message_read(message, "t", &large);
      options.numbers[key] = large;
    }
    else if (strcmp(contents, "i") == 0)
    {
      // Python and others send a number as i by default, a negative one is taken as zero
      result = sd_bus_message_read(message, "i", &signed32);
      options.numbers[key] = std::max(signed32, 0);
    }
    else if (strcmp(contents, "x") == 0)
    {
      result = sd_bus_message_read(message, "x", &signed64);
      options.numbers[key] = std::max<int64_t>(signed64, 0);
    }
    else if ((strcmp(contents, "as") == 0) &&
             ((result = sd_bus_message_read_strv(message, &list)) >= 0))
    {
      for (char** item = list; (item != nullptr) && (*item != nullptr); item ++)
      {
        options.matches.push_back(*item);
        free(*item);
      }

      free(list);
    }
    else
    {
      result = sd_bus_message_skip(message, contents);
    }

    if ((result < 0) ||
        ((result = sd_bus_message_exit_container(message)) < 0) ||
        ((result = sd_bus_message_exit_container(message)) < 0))
    {
      return result;
    }
  }

  return (result < 0) ? result : sd_bus_message_exit_container(message);
}

// The filter of the options as query() and open() of a plugin make it: the journal of the window of `id`, or of this
// machine; false and the reason when an option is wrong
static bool MakeFilter(const Options& options, JournalFilter& filter, std::string& error)
{
  int level;
  uint64_t stamp;
  JournalWindow* window;
  std::map<std::string, std::string>::const_iterator text;
  std::map<std::string, uint64_t>::const_iterator number;

  filter           = JournalFilter { };
  filter.levels    = 0xff;
  filter.columns   = JOURNAL_COLUMN_DEFAULT;
  filter.highlight = JOURNAL_HIGHLIGHT_BUILTIN;

  if ((number = options.numbers.find("id")) != options.numbers.end())
  {
    if ((window = FindWindow(number->second)) == nullptr)
    {
      error = "there is no window of id " + std::to_string(number->second);
      return false;
    }

    // Its journal and the way it shows them
    filter.directory = window->view->getFilter().directory;
    filter.merge     = window->view->getFilter().merge;
    filter.columns   = window->view->getFilter().columns;
    filter.highlight = window->view->getFilter().highlight;
  }

  for (const std::string& match : options.matches)
  {
    if (!CheckJournalMatch(match))
    {
      error = match + " is not a FIELD=VALUE match";
      return false;
    }

    filter.matches.push_back(match);
  }

  if ((text = options.texts.find("text")) != options.texts.end())
  {
    filter.text = text->second;
    filter.mode = (options.flags.count("regex")   && options.flags.at("regex")   ? JOURNAL_TEXT_EXPRESSION : 0) |
                  (options.flags.count("exclude") && options.flags.at("exclude") ? JOURNAL_TEXT_EXCLUDE    : 0) |
                  (options.flags.count("case")    && options.flags.at("case")    ? JOURNAL_TEXT_SENSITIVE  : 0);

    if ((filter.mode & JOURNAL_TEXT_EXPRESSION) &&
        (!JournalExpression().compile(filter.text, !(filter.mode & JOURNAL_TEXT_SENSITIVE), &error)))
    {
      return false;
    }
  }

  if (((text = options.texts.find("since")) != options.texts.end()) && (!ParseJournalTime(filter.since = text->second, &stamp)))
  {
    error = "not a time TurboJedi knows, or one before 1970: " + text->second;
    return false;
  }

  if (((text = options.texts.find("until")) != options.texts.end()) && (!ParseJournalTime(filter.until = text->second, &stamp)))
  {
    error = "not a time TurboJedi knows, or one before 1970: " + text->second;
    return false;
  }

  if ((text = options.texts.find("priority")) != options.texts.end())
  {
    // Up to the level, like -p
    for (level = 0; (level < 8) && (text->second != LevelNames[level]) && (text->second != std::to_string(level)); level ++);

    if (level == 8)
    {
      error = "unknown priority " + text->second;
      return false;
    }

    filter.levels = (2 << level) - 1;
  }

  return true;
}

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

// An integer type of the bus: y n q i u x t
static bool IsInteger(const char* type)
{
  return (type[0] != '\0') && (type[1] == '\0') && (strchr("ynqiuxt", type[0]) != nullptr);
}

// The integer of the variant the message is at, of a type IsInteger() has taken, as a sign and a magnitude: the variant is
// read through
static bool ReadInteger(sd_bus_message* message, const char* type, bool& negative, uint64_t& magnitude)
{
  int64_t value;
  union { uint8_t y; int16_t n; uint16_t q; int32_t i; uint32_t u; int64_t x; uint64_t t; } number;

  if ((sd_bus_message_enter_container(message, 'v', type) < 0) ||
      (sd_bus_message_read_basic(message, type[0], &number) < 0) ||
      (sd_bus_message_exit_container(message) < 0))
  {
    return false;
  }

  switch (type[0])
  {
    case 'y': value = number.y; break;
    case 'n': value = number.n; break;
    case 'q': value = number.q; break;
    case 'i': value = number.i; break;
    case 'u': value = number.u; break;
    case 'x': value = number.x; break;
    default:  negative = false; magnitude = number.t; return true;
  }

  negative  = (value < 0);
  magnitude = negative ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
  return true;
}

// A value of the bus fits a type of an argument: an integer of another type when its value fits, any number for d. An
// integer is read through, read tells it; the variant of another value is left for the caller to skip
static bool FitsArgument(sd_bus_message* message, const char* contents, const std::string& type, bool& read)
{
  bool negative;
  uint64_t magnitude;

  read = false;

  if ((type == "s") || (type == "b") || (type == "as") ||
      (strcmp(contents, "d") == 0))
  {
    return (type == contents) || ((type == "d") && (strcmp(contents, "d") == 0));
  }

  if ((!IsInteger(contents)) ||
      (!(read = ReadInteger(message, contents, negative, magnitude))))
  {
    return false;
  }

  return (type == "d") ||
         ((type == "i") && (magnitude <= (negative ? 0x80000000ULL : 0x7fffffffULL))) ||
         ((type == "u") && (!negative) && (magnitude <= 0xffffffffULL)) ||
         ((type == "x") && (magnitude <= (negative ? 0x8000000000000000ULL : 0x7fffffffffffffffULL))) ||
         ((type == "t") && (!negative));
}

// The values of Invoke() at the place of the message: the keys of TurboJedi taken, _window of the window the method runs
// in, the ones of the method checked by its arguments when it has told them. False and why for a value it does not take,
// a key of TurboJedi there is not, or an argument it needs and has not got
static bool CheckValues(sd_bus_message* message, const PluginMethod* method, bool& windowed, uint32_t& window, std::string& error)
{
  int result;
  char type;
  bool read;
  bool negative;
  uint64_t magnitude;
  const char* key;
  const char* contents;
  std::set<std::string> given;
  std::vector<PluginArgument>::const_iterator argument;

  windowed = false;
  error    = "the values cannot be read";

  if (sd_bus_message_enter_container(message, 'a', "{sv}") < 0)
  {
    return false;
  }

  while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0)
  {
    read = false;

    if ((sd_bus_message_read(message, "s", &key) < 0) ||
        (sd_bus_message_peek_type(message, &type, &contents) < 0))
    {
      return false;
    }

    if (key[0] == '_')
    {
      // Of TurboJedi, not of the method
      if (strcmp(key, "_window") != 0)
      {
        error = std::string("TurboJedi has no key ") + key + ", the keys beginning with _ are of TurboJedi";
        return false;
      }

      if ((!IsInteger(contents)) ||
          (!(read = ReadInteger(message, contents, negative, magnitude))) ||
          (negative) ||
          (magnitude > 0xffffffffULL))
      {
        error = "_window is the id of a window, u";
        return false;
      }

      windowed = true;
      window   = (uint32_t)magnitude;
    }
    else if (method->declared)
    {
      argument = std::find_if(method->arguments.begin(), method->arguments.end(), [key] (const PluginArgument& item)
      {
        return item.name == key;
      });

      if (argument == method->arguments.end())
      {
        error = "method " + method->method + " of plugin " + method->plugin + " takes no argument " + key;
        return false;
      }

      if (!FitsArgument(message, contents, argument->type, read))
      {
        error = "argument " + argument->name + " is " + argument->type + ", the value given is not";
        return false;
      }

      given.insert(key);
    }

    if (((!read) && (sd_bus_message_skip(message, "v") < 0)) ||
        (sd_bus_message_exit_container(message) < 0))
    {
      return false;
    }
  }

  if ((result < 0) ||
      (sd_bus_message_exit_container(message) < 0))
  {
    return false;
  }

  for (const PluginArgument& item : method->arguments)
  {
    if (item.required && (given.count(item.name) == 0))
    {
      error = "method " + method->method + " of plugin " + method->plugin + " needs argument " + item.name;
      return false;
    }
  }

  return true;
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

static const sd_bus_vtable Table[] =
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

// The calls going on

static void ReplyError(Operation& operation, const char* name, const std::string& text)
{
  sd_bus_reply_method_errorf(operation.message, name, "%s", text.c_str());
}

// The answer to a call made later: one that cannot be made is told as an error, the caller is not left waiting
template <typename Function> static void Answer(Operation& operation, Function make)
{
  int result;

  if ((result = Reply(operation.message, make)) < 0)
  {
    sd_bus_reply_method_errorf(operation.message, SD_BUS_ERROR_FAILED, "the answer cannot be made: %s", strerror(-result));
  }
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

// Interface

static std::string TrimText(const std::string& text)
{
  size_t first;
  size_t last;

  first = text.find_first_not_of(" \t");
  last  = text.find_last_not_of(" \t\r\n");
  return (first == std::string::npos) ? std::string() : text.substr(first, last - first + 1);
}

// Lines user=NAME or UID, then methods=Describe,Query,... for it; a problem tells the file and the line
static void ReadPermissions(const std::string& path, std::vector<std::string>& problems)
{
  int number;
  bool known;
  bool numeric;
  bool fits;
  char* end;
  char line[1024];
  FILE* file;
  size_t equal;
  size_t start;
  size_t comma;
  unsigned long id;
  std::string key;
  std::string text;
  std::string value;
  std::string method;
  std::set<std::string> names;
  struct passwd* user;
  struct group* group;

  if ((path.empty()) ||
      ((file = fopen(path.c_str(), "r")) == nullptr))
  {
    // None is the default: the own user and root
    return;
  }

  for (const sd_bus_vtable* item = Table; item->type != _SD_BUS_VTABLE_END; item ++)
  {
    // Methods there are to name
    if (item->type == _SD_BUS_VTABLE_METHOD) names.insert(item->x.method.member);
  }

  known = false;

  for (number = 1; fgets(line, sizeof(line), file) != nullptr; number ++)
  {
    text = TrimText(line);

    if (text.empty() || (text[0] == '#') || (text[0] == ';'))
    {
      // Blank line or comment
      continue;
    }

    if ((equal = text.find('=')) == std::string::npos)
    {
      problems.push_back(path + ", line " + std::to_string(number) + ": key=value is expected");
      continue;
    }

    key   = TrimText(text.substr(0, equal));
    value = TrimText(text.substr(equal + 1));

    if ((key == "user") ||
        (key == "group"))
    {
      // Digits only are a UID or a GID, anything else a name the system has
      errno   = 0;
      id      = strtoul(value.c_str(), &end, 10);
      numeric = (!value.empty()) && (value.find_first_not_of("0123456789") == std::string::npos);
      user    = ((!numeric) && (key == "user"))  ? getpwnam(value.c_str()) : nullptr;
      group   = ((!numeric) && (key == "group")) ? getgrnam(value.c_str()) : nullptr;
      fits    = (key == "user") ?
        ((id < (unsigned long)(uid_t)-1) && (id == (unsigned long)(uid_t)id)) :
        ((id < (unsigned long)(gid_t)-1) && (id == (unsigned long)(gid_t)id));
      known   = numeric ? ((errno == 0) && (*end == '\0') && fits) : ((user != nullptr) || (group != nullptr));

      if (!known)
      {
        problems.push_back(path + ", line " + std::to_string(number) + ": no " + ((key == "user") ? (numeric ? "UID " : "user ") : (numeric ? "GID " : "group ")) + value + ", its lines are left out");
        continue;
      }

      permissions.push_back({ key == "group", (user != nullptr) ? (unsigned)user->pw_uid : (group != nullptr) ? (unsigned)group->gr_gid : (unsigned)id, false, { } });
      continue;
    }

    if (key == "methods")
    {
      if (!known)
      {
        // Of no user, or of one left out
        problems.push_back(path + ", line " + std::to_string(number) + ": methods go after user or group");
        continue;
      }

      permissions.back().limited = true;

      for (start = 0; start <= value.size(); start = comma + 1)
      {
        comma  = std::min(value.find(',', start), value.size());
        method = TrimText(value.substr(start, comma - start));

        if ((names.count(method) == 0) &&
            ((method.compare(0, 7, "Invoke:") != 0) || (method.size() == 7) || (method.find_first_of(" \t") != std::string::npos)))
        {
          problems.push_back(path + ", line " + std::to_string(number) + ": no method " + method);
          continue;
        }

        permissions.back().methods.insert(method);
      }

      continue;
    }

    problems.push_back(path + ", line " + std::to_string(number) + ": unknown key " + key);
  }

  fclose(file);
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

  snprintf(name, sizeof(name), BUS_PREFIX ".U%u.P%u", (unsigned)getuid(), (unsigned)getpid());

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
