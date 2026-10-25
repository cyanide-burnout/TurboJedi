// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#include "TimelineWindow.h"

// TimelineWindow

TimelineWindow::TimelineWindow(const TRect& bounds, const JournalFilter& filter, const std::string& name) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, ("Timeline" + (name.empty() ? std::string() : (" " + name))).c_str(), wnNoNumber)
{
  TRect extent;
  TScrollBar* horizontal;

  options  |= ofTileable;
  growMode  = gfGrowAll | gfGrowRel;

  horizontal = standardScrollBar(sbHorizontal);

  extent = getExtent();
  extent.grow(-1, -1);
  view   = new TimelineView(extent, horizontal, filter);

  insert(view);
}
