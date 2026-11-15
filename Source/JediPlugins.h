#ifndef JEDIPLUGINS_H
#define JEDIPLUGINS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TPoint
#include <tvision/tv.h>

#include <vector>
#include <string>

#include "JournalTools.h"

// Plugins in Lua 5.4 make the Actions menu of an entry, see PLUGINS.md. The *.lua files of /usr/share/turbojedi/plugins,
// /usr/local/share/turbojedi/plugins and the directory of the user are run in the order of their names once at start, a file
// of the same name in a later directory takes the place of an earlier one and an empty one leaves it out. Each file registers
// its handlers, every handler adds the items it has for the entry when the menu is opened, an item runs its function when
// chosen. Lua is entered only for these two, never from the events that go on meanwhile

// Runs the plugins, problems of the files go to errors; the user directory may be empty
void LoadPlugins(const std::string& directory, std::vector<std::string>& errors);

// Menu of the actions for the entry of a cursor at a point of the screen, the chosen one is run; the window tells where its
// journal comes from. Opened by the keys it starts on its first item, by the mouse on none
void ShowActions(const JournalFilter& filter, const std::string& cursor, TPoint where, bool keyboard);

void ClosePlugins();

#endif
