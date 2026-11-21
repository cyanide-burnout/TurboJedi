#ifndef SERVICEPRIVATE_H
#define SERVICEPRIVATE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// What the parts of the service share: the headers, the limits of a call, the calls going on, the permissions and the
// helpers one part has of another

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

// A user answered besides the own one and root, by the files of allow: every method, or the ones named
struct Permission
{
  bool group;                     // id is a GID: the members of the group the system tells, the primary one included
  unsigned id;
  bool limited;                   // methods= has been given: the ones it names only, a mistyped one is not all of them
  std::set<std::string> methods;
};

// Options of Query() and OpenWindow(), a{sv}; ones of other names or types are left alone
struct Options
{
  std::map<std::string, std::string> texts;
  std::map<std::string, bool> flags;
  std::map<std::string, uint64_t> numbers;
  std::vector<std::string> matches;
};

extern const sd_bus_vtable Table[];  // The methods of the bus
extern sd_bus* bus;
extern std::list<Operation> operations;
extern bool delegated;               // A root instance started by sudo answers the user of sudo as well
extern uid_t delegate;
extern uint64_t started;             // Microseconds of CLOCK_REALTIME

bool IsText(const std::string& value);
int AppendString(sd_bus_message* message, const char* key, const std::string& value);
int AppendStrings(sd_bus_message* message, const char* key, const std::vector<std::string>& values);
int AppendFilter(sd_bus_message* message, const JournalFilter& filter);
int AppendEntry(sd_bus_message* message, const JournalRecord& fields);
int AppendEntries(sd_bus_message* message, const std::vector<JournalRecord>& records);
int ReadOptions(sd_bus_message* message, Options& options);
bool MakeFilter(const Options& options, JournalFilter& filter, std::string& error);
bool CheckValues(sd_bus_message* message, const PluginMethod* method, bool& windowed, uint32_t& window, std::string& error);
JournalWindow* FindWindow(unsigned id);
void CollectWindows(TView* view, void* argument);
TView* FindFrontWindow();
size_t CountDescriptors();
std::string TitleWindow(JournalWindow* window);
uint64_t GetCursorTime(const std::string& cursor);
int Allow(sd_bus_message* message, sd_bus_error* error, const std::string& plugin = std::string(), const std::string& method = std::string(), const char* member = nullptr);
void ReadPermissions(const std::string& path, std::vector<std::string>& problems);

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

// The answer to a call made later: one that cannot be made is told as an error, the caller is not left waiting
template <typename Function> static void Answer(Operation& operation, Function make)
{
  int result;

  if ((result = Reply(operation.message, make)) < 0)
  {
    sd_bus_reply_method_errorf(operation.message, SD_BUS_ERROR_FAILED, "the answer cannot be made: %s", strerror(-result));
  }
}

#endif
