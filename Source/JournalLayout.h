#ifndef JOURNALLAYOUT_H
#define JOURNALLAYOUT_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

#include "JournalTools.h"

// One window of a layout file: an INI section [window] with the bounds on the desktop, the filter and the columns,
// or [timeline] with the bounds, the filter it counts, its step and scale

struct JournalLayout
{
  int bounds[4];  // Left, top, right, bottom
  JournalFilter filter;
  bool timeline;
  int step;       // Seconds of an interval of a timeline, zero lets the window choose
  bool logarithmic;
};

// Takes a whole file or nothing, the error tells the line and what is wrong there
bool LoadJournalLayout(const std::string& path, std::vector<JournalLayout>& windows, std::string& error);
bool SaveJournalLayout(const std::string& path, const std::vector<JournalLayout>& windows);

#endif
