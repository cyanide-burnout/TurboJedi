#ifndef JOURNALLAYOUT_H
#define JOURNALLAYOUT_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

#include "JournalTools.h"

// One window of a layout file: an INI section [journal] with the bounds on the desktop, the filter and the columns,
// [timeline] with the bounds, the filter it counts, its step and scale, [watchlist] of the list of watches with its bounds,
// or [watchlines] of the lines of a watch with its bounds and the plugin and the title the watch is found by

#define LAYOUT_WATCH_LIST   1
#define LAYOUT_WATCH_LINES  2

struct JournalLayout
{
  int bounds[4];  // Left, top, right, bottom
  JournalFilter filter;
  bool timeline;
  int step;       // Seconds of an interval of a timeline, zero lets the window choose
  bool logarithmic;
  int watches;          // LAYOUT_WATCH_LIST or LAYOUT_WATCH_LINES for a window of watches, zero for a journal or a timeline
  std::string plugin;   // Of the watch of the lines
  std::string title;
};

// Takes a whole file or nothing, the error tells the line and what is wrong there. The watches of plugins come from the
// lines of a [watches] section as they are, the engine of the plugins reads them
bool LoadJournalLayout(const std::string& path, std::vector<JournalLayout>& windows, std::vector<std::string>& watches, std::string& error);
bool SaveJournalLayout(const std::string& path, const std::vector<JournalLayout>& windows, const std::vector<std::string>& watches);

#endif
