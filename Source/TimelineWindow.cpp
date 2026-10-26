// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#include "TimelineWindow.h"

// Helpers

static std::string GetTitle(const JournalFilter& filter)
{
  std::string name;

  // The filter the counts are of, like the title of a journal window
  name = GetJournalFilterName(filter);
  return name.empty() ? "Timeline" : ("Timeline " + name);
}

// TimelineWindow

TimelineWindow::TimelineWindow(const TRect& bounds, const JournalFilter& filter, int seconds, bool logarithmic) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, GetTitle(filter).c_str(), wnNoNumber)
{
  TRect extent;
  TScrollBar* horizontal;

  extent = prepare(horizontal);
  view   = new TimelineView(extent, horizontal, filter, seconds, logarithmic);

  insert(view);
}

TimelineWindow::TimelineWindow(const TRect& bounds, const TimelineView& source) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, GetTitle(source.getFilter()).c_str(), wnNoNumber)
{
  TRect extent;
  TScrollBar* horizontal;

  extent = prepare(horizontal);
  view   = new TimelineView(extent, horizontal, source);

  insert(view);
}

TRect TimelineWindow::prepare(TScrollBar*& horizontal)
{
  TRect extent;

  options  |= ofTileable;
  growMode  = gfGrowAll | gfGrowRel;

  horizontal = standardScrollBar(sbHorizontal);

  extent = getExtent();
  extent.grow(-1, -1);

  return extent;
}
