#ifndef TIMELINEWINDOW_H
#define TIMELINEWINDOW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TWindow
#include <tvision/tv.h>

#include <string>

#include "TimelineView.h"

// Counts of the entries a journal window shows, by intervals of time; the title tells the filter it has been opened with

class TimelineWindow : public TWindow
{
  public:

    TimelineWindow(const TRect& bounds, const JournalFilter& filter, const std::string& name);

    TimelineView* view;
};

#endif
