// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JediDBus.h"
#include "JournalSource.h"
#include "JournalQuery.h"

#include <errno.h>
#include <malloc.h>
#include <string.h>

#include <map>
#include <algorithm>

#include <systemd/sd-login.h>

// A call going on: its slot until the answer comes, then the reply or the error
struct Call
{
  int which;
  sd_bus_slot* slot;
  bool done;
  sd_bus_message* reply;
  JediDBusError error;
};

struct Connection
{
  sd_bus* bus;
  JournalSource* source;
};

static Connection connections[2];
static std::map<int, Call> calls;

static const char* const Names[] = { "the system bus", "the bus of the user" };

void SetDBusError(JediDBusError& error, int code, const std::string& text)
{
  sd_bus_error failure = SD_BUS_ERROR_NULL;

  // The name sd-bus gives an errno, System.Error.ENOENT and the like
  sd_bus_error_set_errno(&failure, code);
  error.name = (failure.name != nullptr) ? failure.name : "System.Error.EIO";
  error.text = text + ": " + strerror(code);
  sd_bus_error_free(&failure);
}

static void FinishCall(Call& call, sd_bus_message* message)
{
  const sd_bus_error* failure;

  call.done = true;

  if (sd_bus_message_is_method_error(message, nullptr))
  {
    // A refusal of the service, of the bus, or a time out sd-bus has made itself
    failure          = sd_bus_message_get_error(message);
    call.error.name  = ((failure != nullptr) && (failure->name != nullptr)) ? failure->name : "System.Error.EIO";
    call.error.text  = ((failure != nullptr) && (failure->message != nullptr)) ? failure->message : "";
    return;
  }

  call.reply = sd_bus_message_ref(message);
}

static int ReceiveAnswer(sd_bus_message* message, void* context, sd_bus_error*)
{
  std::map<int, Call>::iterator call;

  // Only kept here: Lua never runs from sd-bus, the answer waits for its turn
  if ((call = calls.find((int)(intptr_t)context)) != calls.end())
  {
    FinishCall(call->second, message);
  }

  return 1;
}

// A connection that has broken: its calls end with the error, the next call opens a new one
static void DropConnection(int which, int code)
{
  Connection& connection = connections[which];

  for (std::pair<const int, Call>& call : calls)
  {
    if ((call.second.which == which) &&
        (!call.second.done))
    {
      call.second.done = true;
      call.second.slot = sd_bus_slot_unref(call.second.slot);
      SetDBusError(call.second.error, code, std::string(Names[which]) + " has gone away");
    }
  }

  ReleaseJournalSource(connection.source);
  sd_bus_close_unref(connection.bus);
  connection.bus    = nullptr;
  connection.source = nullptr;
}

sd_bus* GetPluginBus(int which, JediDBusError& error)
{
  int result;
  Connection& connection = connections[which];

  if ((connection.bus != nullptr) &&
      (!sd_bus_is_open(connection.bus)))
  {
    // The bus has closed it, a restart of dbus as a rule
    DropConnection(which, ECONNRESET);
  }

  if (connection.bus != nullptr)
  {
    return connection.bus;
  }

  result = (which == DBUS_SYSTEM) ? sd_bus_open_system(&connection.bus) : sd_bus_open_user(&connection.bus);

  if (result < 0)
  {
    // Root has no bus of a user, as a rule: told, and tried again next time
    connection.bus = nullptr;
    SetDBusError(error, -result, std::string(Names[which]) + " cannot be reached");
    return nullptr;
  }

  // The answers wake the application as the journals do
  connection.source = CreateJournalSource(sd_bus_get_fd(connection.bus));
  return connection.bus;
}

bool StartDBusCall(int number, int which, sd_bus_message* message, uint64_t timeout, JediDBusError& error)
{
  int result;
  Call call;

  if (calls.size() >= DBUS_CALLS)
  {
    SetDBusError(error, EBUSY, std::to_string(DBUS_CALLS) + " calls of the plugins go on already");
    return false;
  }

  call.which = which;
  call.slot  = nullptr;
  call.done  = false;
  call.reply = nullptr;
  calls[number] = call;

  if ((result = sd_bus_call_async(connections[which].bus, &calls[number].slot, message, ReceiveAnswer, (void*)(intptr_t)number, timeout)) < 0)
  {
    calls.erase(number);
    SetDBusError(error, -result, "the call cannot be sent");
    return false;
  }

  return true;
}

static void ProcessBus(int which)
{
  int count;
  int result;

  for (count = 0; (count < 64) && ((result = sd_bus_process(connections[which].bus, nullptr)) > 0); count ++)
  {
    // The answers that have come, a few at a time; signals and the rest are dropped by sd-bus itself
  }

  if ((result < 0) ||
      (!sd_bus_is_open(connections[which].bus)))
  {
    DropConnection(which, (result < 0) ? -result : ECONNRESET);
  }
}

bool PollDBus()
{
  for (int which = 0; which < 2; which ++)
  {
    if (connections[which].bus != nullptr)
    {
      ProcessBus(which);
    }
  }

  return std::any_of(calls.begin(), calls.end(), [] (const std::pair<const int, Call>& call) { return !call.second.done; });
}

size_t CountDBusCalls()
{
  return calls.size();
}

bool IsDBusDone(int number)
{
  std::map<int, Call>::iterator call;

  return ((call = calls.find(number)) != calls.end()) && call->second.done;
}

bool WaitDBusCall(int number, int64_t deadline)
{
  int which;
  int result;
  int64_t now;
  sd_bus* bus;
  std::map<int, Call>::iterator call;

  if ((call = calls.find(number)) == calls.end())
  {
    return false;
  }

  which = call->second.which;
  bus   = connections[which].bus;

  while ((!IsDBusDone(number)) &&
         ((now = GetQueryMilliseconds()) < deadline))
  {
    if ((result = sd_bus_process(bus, nullptr)) > 0)
    {
      // Another message has come, perhaps the answer: looked at again before waiting
      continue;
    }

    if ((result < 0) ||
        (!sd_bus_is_open(bus)) ||
        (((result = sd_bus_wait(bus, (uint64_t)(deadline - now) * 1000)) < 0) && (result != -EINTR)))
    {
      // The connection has broken: the call ends with it instead of waiting until its time is over
      DropConnection(which, (result < 0) ? -result : ECONNRESET);
      break;
    }
  }

  return IsDBusDone(number);
}

static void DropCall(std::map<int, Call>::iterator call)
{
  // sd-bus does not call back for a slot that is gone
  sd_bus_slot_unref(call->second.slot);
  sd_bus_message_unref(call->second.reply);
  calls.erase(call);
}

bool TakeDBusAnswer(int number, sd_bus_message** reply, JediDBusError& error)
{
  std::map<int, Call>::iterator call;

  if (((call = calls.find(number)) == calls.end()) ||
      (!call->second.done))
  {
    return false;
  }

  *reply = sd_bus_message_ref(call->second.reply);
  error  = call->second.error;
  DropCall(call);
  return true;
}

void CancelDBusCall(int number)
{
  std::map<int, Call>::iterator call;

  if ((call = calls.find(number)) != calls.end())
  {
    DropCall(call);
  }
}

// A call to the bus itself, waited for up to the deadline
static int CallBus(sd_bus* bus, const char* method, int64_t deadline, sd_bus_message** reply, JediDBusError& error, const char* name)
{
  int result;
  int64_t left;
  sd_bus_message* call;
  sd_bus_error failure = SD_BUS_ERROR_NULL;

  if ((left = deadline - GetQueryMilliseconds()) <= 0)
  {
    error.name = SD_BUS_ERROR_NO_REPLY;
    error.text = "there is no time left for the bus";
    return -ETIMEDOUT;
  }

  if (((result = sd_bus_message_new_method_call(bus, &call, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method)) < 0) ||
      ((name != nullptr) && ((result = sd_bus_message_append(call, "s", name)) < 0)))
  {
    sd_bus_message_unref(call);
    SetDBusError(error, -result, "the bus cannot be asked");
    return result;
  }

  result = sd_bus_call(bus, call, (uint64_t)left * 1000, &failure, reply);
  sd_bus_message_unref(call);

  if (result < 0)
  {
    error.name = (failure.name != nullptr) ? failure.name : "System.Error.EIO";
    error.text = (failure.message != nullptr) ? failure.message : strerror(-result);
  }

  sd_bus_error_free(&failure);
  return result;
}

bool ListDBusServices(int which, const std::string& prefix, int64_t deadline, std::vector<JediDBusService>& list, JediDBusError& error)
{
  bool over;
  char** names;
  char* unit;
  const char* key;
  sd_bus* bus;
  sd_bus_message* reply;
  JediDBusService service;
  JediDBusError ignored;

  if ((bus = GetPluginBus(which, error)) == nullptr)
  {
    return false;
  }

  reply = nullptr;
  names = nullptr;

  if ((CallBus(bus, "ListNames", deadline, &reply, error, nullptr) < 0) ||
      (sd_bus_message_read_strv(reply, &names) < 0))
  {
    sd_bus_message_unref(reply);
    return false;
  }

  sd_bus_message_unref(reply);
  over = false;

  for (char** name = names; (name != nullptr) && (*name != nullptr); name ++)
  {
    if ((!over) &&
        ((*name)[0] != ':') &&
        (strncmp(*name, prefix.c_str(), prefix.size()) == 0))
    {
      service = { *name, 0, 0, std::string() };
      reply   = nullptr;

      if (CallBus(bus, "GetConnectionCredentials", deadline, &reply, ignored, *name) >= 0)
      {
        // a{sv}: UnixUserID and ProcessID among others; a name gone meanwhile is left out
        if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0)
        {
          while (sd_bus_message_enter_container(reply, 'e', "sv") > 0)
          {
            sd_bus_message_read(reply, "s", &key);

            if      (strcmp(key, "UnixUserID") == 0) sd_bus_message_read(reply, "v", "u", &service.uid);
            else if (strcmp(key, "ProcessID") == 0)  sd_bus_message_read(reply, "v", "u", &service.pid);
            else                                     sd_bus_message_skip(reply, "v");

            sd_bus_message_exit_container(reply);
          }
        }

        if ((service.pid != 0) &&
            (sd_pid_get_unit(service.pid, &unit) >= 0))
        {
          service.unit = unit;
          free(unit);
        }

        list.push_back(service);
      }
      else if (ignored.name == SD_BUS_ERROR_NO_REPLY)
      {
        // The time is over, not one name: the rest are only freed
        error = ignored;
        over  = true;
      }

      sd_bus_message_unref(reply);
    }

    free(*name);
  }

  free(names);

  if (over)
  {
    list.clear();
    return false;
  }

  std::sort(list.begin(), list.end(), [] (const JediDBusService& one, const JediDBusService& other) { return one.name < other.name; });
  return true;
}

void CloseDBus()
{
  while (!calls.empty())
  {
    DropCall(calls.begin());
  }

  for (Connection& connection : connections)
  {
    if (connection.bus != nullptr)
    {
      ReleaseJournalSource(connection.source);
      sd_bus_flush_close_unref(connection.bus);
      connection.bus    = nullptr;
      connection.source = nullptr;
    }
  }
}
