#ifndef JEDISCRIPT_H
#define JEDISCRIPT_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TPoint
#include <tvision/tv.h>

#include <vector>
#include <string>

#include "JournalTools.h"
#include "JournalWatch.h"

class JournalView;

// Plugins in Lua 5.4 make the Actions menu of an entry, see PLUGINS.md. The *.lua files of /usr/share/turbojedi/plugins,
// /usr/local/share/turbojedi/plugins and the directory of the user are run in the order of their names once at start, a file
// of the same name in a later directory takes the place of an earlier one and an empty one leaves it out. Each file registers
// its handlers, every handler adds the items it has for the entry when the menu is opened, an item runs its function when
// chosen. Lua is entered only for these two, never from the events that go on meanwhile

// Runs the plugins, problems of the files go to errors; the user directory may be empty
void LoadPlugins(const std::string& directory, std::vector<std::string>& errors);

// Menu of the actions for the entry of a cursor in a window at a point of the screen, the chosen one is run: the window
// tells where its journal comes from, opens windows like itself and finds and goes to times for the action. Opened by the
// keys it starts on its first item, by the mouse on none
void ShowActions(JournalView* view, const std::string& cursor, TPoint where, bool keyboard);

// Descriptors, timers, requests of fetch() and watches of journals of plugins, looked at between the events: milliseconds
// until the next look, -1 when there is nothing to look at; the ones that are due wait for HasPluginEvents() and
// DispatchPluginEvents() when no dialog is open. Paused while one is: the watches do not read then
int PollPlugins(bool paused);
bool HasPluginEvents();
void DispatchPluginEvents();

// A window is closed, the watches that have gone to their entries in it open another one next time
void ForgetPluginWindow(JournalView* view);

// Watches of journals for their windows: the ones there are, the one with the newest line first; one by its number
std::vector<JournalWatch*> ListPluginWatches();
JournalWatch* FindPluginWatch(int number);

void RemovePluginWatch(int number);                                            // Delete in the list of watches
void ShowPluginWatchEntry(JournalWatch* watch, const JournalPosition& entry);  // Enter on a line: the entry in a window

// Watches have changed since the last time: their windows and the status line are drawn again
void MarkPluginWatchesChanged();
bool TakePluginWatchChanges();

// A watch a line has raised the list of watches for, zero for none
int TakePluginWatchRaise();

// What a watch asks the application for with cmJournalOpenWatch: a new window of its filter, the view of it back
struct JournalWindowRequest
{
  const JournalFilter* filter;
  JournalView* view;
};

void ClosePlugins();

#endif
