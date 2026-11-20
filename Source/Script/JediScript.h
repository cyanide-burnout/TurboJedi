#ifndef JEDISCRIPT_H
#define JEDISCRIPT_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TPoint
#include <tvision/tv.h>

#include <systemd/sd-bus.h>

#include <vector>
#include <string>

#include "JournalTools.h"
#include "JournalWatch.h"

class JournalView;

// Plugins in Lua 5.4 make the Actions menu of an entry, see LUA.md. The *.lua files of /usr/share/turbojedi/plugins,
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

// The Plugins menu of the menu bar, items of on('menu'): title and plugin in the order the plugins have given them, a
// title cut to the width with its plugin, made once the plugins are loaded. An item runs by its index as an action does, in the window of the journal on top, nullptr
// when none is open: open() and query() start from the filter then, find(), seek() and mark() tell there is no window
#define PLUGIN_MENU_LIMIT  100
std::vector<std::pair<std::string, std::string>> ListPluginMenu(int width);
void RunPluginMenu(int index, JournalView* view, const JournalFilter& filter);

// Descriptors, timers, requests of fetch() and watches of journals of plugins, looked at between the events: milliseconds
// until the next look, -1 when there is nothing to look at; the ones that are due wait for HasPluginEvents() and
// DispatchPluginEvents() when no dialog is open. Paused while one is: the watches do not read then
int PollPlugins(bool paused);
bool HasPluginEvents();
void DispatchPluginEvents();

// Lua runs nothing now: the bus may make a menu
bool IsPluginIdle();

// The console of TurboJedi: a watch of its own, plugin turbojedi, key console, made before the plugins and never
// removed; print() and log() of plugins, their errors, the problems of the start and what the bus has done go to its
// lines, a problem unread
void LogConsole(const std::string& source, const std::string& text, bool problem);
int GetConsoleWatch();

// A method of on('invoke') as its plugin has told it: what it does for a caller to know, an agent of MCP first. A method
// registered without options has none of it and is taken as one that changes the system
struct PluginArgument
{
  std::string name;
  std::string type;         // s u i t x b d as
  std::string description;
  bool required;
};

struct PluginMethod
{
  std::string plugin;
  std::string method;
  std::string description;  // Empty when the plugin has given none
  std::string effect;       // none, screen or system
  bool declared;            // Its arguments are told and checked, any values go to it otherwise
  std::vector<PluginArgument> arguments;
};

// The methods of the plugins loaded, by plugin and name; nullptr for one there is not
std::vector<const PluginMethod*> ListPluginMethods();
const PluginMethod* FindPluginMethod(const std::string& name, const std::string& method);

// Invoke() of the bus: the method a plugin has registered by on('invoke'), with the a{sv} at the place of the call
// as its values, the keys of TurboJedi left out, its answer appended to the reply as a{sv}; the error tells why it has
// failed. It runs as an action of the window, of the journal of this machine without one, until the deadline in
// milliseconds of CLOCK_MONOTONIC: queries stop there. Only while Lua is idle
#define PLUGIN_METHOD_DONE    0
#define PLUGIN_METHOD_NONE    1  // No such plugin or method
#define PLUGIN_METHOD_FAILED  2
int InvokePluginMethod(const std::string& name, const std::string& method, sd_bus_message* call, sd_bus_message* reply, JournalView* view, int64_t deadline, std::string& error);

// An action of the Actions menu: its title, its plugin, and the titles of the submenus it is in, the outer one first
struct PluginAction
{
  std::string title;
  std::string plugin;
  std::vector<std::string> path;
};

// The actions of the Actions menu of the entry of a cursor in a window in the order of the menu, nothing run; false when
// Lua is busy or the entry is gone
bool ListPluginActions(JournalView* view, const std::string& cursor, std::vector<PluginAction>& actions);

// A window is closed, the watches that have gone to their entries in it open another one next time
void ForgetPluginWindow(JournalView* view);

// Watches of journals for their windows: the ones there are, the one with the newest line first; one by its number
std::vector<JournalWatch*> ListPluginWatches();
JournalWatch* FindPluginWatch(int number);
// As a layout names one: its plugin, its title, and its index among the ones of the same plugin and title by the order
// they have been set, as a title need not be unique
JournalWatch* FindPluginWatch(const std::string& plugin, const std::string& title, int index);
JournalWatch* FindPluginWatch(const std::string& plugin, const std::string& key);  // By the key its plugin has given
int IndexPluginWatch(const JournalWatch* watch);

bool RemovePluginWatch(int number);                                            // Delete in the list of watches, false for the console
void ShowPluginWatchEntry(JournalWatch* watch, const JournalPosition& entry);  // Enter on a line: the entry in a window
bool RunPluginWatchLine(JournalWatch* watch, uint64_t line);                    // Enter on a line: its action, false without one
void ErasePluginWatchLine(JournalWatch* watch, uint64_t line);                  // Delete on a line: it goes with its action
void ClearPluginWatchLines(JournalWatch* watch);                                // F8: all of them with their actions

// Watches have changed since the last time: their windows and the status line are drawn again
void MarkPluginWatchesChanged();
bool TakePluginWatchChanges();

// A watch a line has raised the list of watches for, zero for none
int TakePluginWatchRaise();

// The times of the watches and of turbojedi.date() in UTC or in the local zone, U in a window of watches turns it over
bool GetPluginUniversal();
void SetPluginUniversal(bool value);

// Watches of a layout: the ones of the layout that goes are removed when another one opens, the global ones of the plugins
// stay. Lines of a [watches] section are given to the functions of on('restore') of their plugins, a window of the
// layout by its number among the journals; the lines of the plugins not there are kept as they are
void DropWorkspaceWatches();
std::vector<std::string> SavePluginWatches(const std::vector<JournalView*>& windows);
void RestorePluginWatches(const std::vector<std::string>& lines, const std::vector<JournalView*>& windows, std::vector<std::string>& errors);

// What a watch asks the application for with cmJournalOpenWatch: a new window of its filter, the view of it back
struct JournalWindowRequest
{
  const JournalFilter* filter;
  JournalView* view;
};

void ClosePlugins();

#endif
