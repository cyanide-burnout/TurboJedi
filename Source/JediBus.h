#ifndef JEDIBUS_H
#define JEDIBUS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <string>

// The programming interface of TurboJedi on the system bus, see DBUS.md: every instance owns the name
// me.burnaway.TurboJedi.U<uid>.P<pid> and answers its own user, root, and the user of sudo of a root instance. It reads
// the windows, the journal and the watches, and changes only the screen: marks, the cursor, a time, a new window. Calls
// that change the screen wait while a dialog is open; queries and the history of a window are read by steps between the
// events, the answer goes when they are over

// Connects and takes the name; false without a bus or without the policy that lets it, TurboJedi goes on without
bool OpenBus();

// Calls and the steps of the ones going on, paused while a dialog is open: milliseconds until the next look, -1 for none
int PollBus(bool paused);

// What a caller of the bus has done on the screen last, for the status line, empty when the user has acted since
std::string DescribeBusNotice();
bool ClearBusNotice();   // The user has acted: true when there has been one
bool TakeBusChange();    // The notice has changed since the last time

void CloseBus();

#endif
