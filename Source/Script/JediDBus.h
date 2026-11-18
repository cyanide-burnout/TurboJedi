#ifndef JEDIDBUS_H
#define JEDIDBUS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <stdint.h>

#include <string>
#include <vector>

#include <systemd/sd-bus.h>

// Calls of plugins to the services of the system and the user bus, as fetch() asks web services: connections of their own,
// not the one TurboJedi answers on (JediService), opened on the first call and moved on between the events. The values are
// made and read by the engine of the plugins, here go the messages

#define DBUS_SYSTEM  0
#define DBUS_USER    1
#define DBUS_CALLS   64  // Calls going on at once for all plugins

// A failure as D-Bus names it, System.Error.<errno> for one of this side
struct JediDBusError
{
  std::string name;
  std::string text;
};

// A name on a bus and the one who owns it
struct JediDBusService
{
  std::string name;
  uint32_t pid;   // Zero when the bus does not tell
  uint32_t uid;
  std::string unit;
};

// The error of an errno of this side, named System.Error.ENOENT and the like, as sd-bus names them
void SetDBusError(JediDBusError& error, int code, const std::string& text);

// The connection to a bus, opened on the first call; nullptr with the reason when it cannot be
sd_bus* GetPluginBus(int which, JediDBusError& error);

// A call made by the caller goes under its number, the timeout in microseconds; false with the reason
bool StartDBusCall(int number, int which, sd_bus_message* call, uint64_t timeout, JediDBusError& error);

// Moves the calls on, the answers wait for their turn; true while some are going on
bool PollDBus();
size_t CountDBusCalls();
bool IsDBusDone(int number);

// Waits for one call on its own connection, nothing else of the application runs meanwhile, up to the deadline in
// milliseconds of CLOCK_MONOTONIC; true once it is done
bool WaitDBusCall(int number, int64_t deadline);

// The answer of a call that is done, and the call is gone: the reply with a reference the caller drops, or the error
bool TakeDBusAnswer(int number, sd_bus_message** reply, JediDBusError& error);

void CancelDBusCall(int number);

// Names that have an owner, of a prefix or all, by name, with their owners; false with the reason, the deadline in
// milliseconds of CLOCK_MONOTONIC
bool ListDBusServices(int which, const std::string& prefix, int64_t deadline, std::vector<JediDBusService>& list, JediDBusError& error);

void CloseDBus();

#endif
