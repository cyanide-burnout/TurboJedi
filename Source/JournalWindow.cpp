// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TFrame
#define Uses_TEvent
#include "JournalWindow.h"
#include "JournalHeader.h"
#include "Commands.h"

#include <algorithm>

// JournalWindow

JournalWindow::JournalWindow(const TRect& bounds, short number, const JournalFilter& filter, int history, size_t capacity) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, "Journal", number)
{
  TRect extent;
  TScrollBar* horizontal;
  TScrollBar* vertical;

  extent = prepare(horizontal, vertical);
  view   = new JournalView(extent, horizontal, vertical, filter, history, capacity);

  attach(extent);
}

JournalWindow::JournalWindow(const TRect& bounds, short number, const JournalView& source) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, "Journal", number)
{
  TRect extent;
  TScrollBar* horizontal;
  TScrollBar* vertical;

  extent = prepare(horizontal, vertical);
  view   = new JournalView(extent, horizontal, vertical, source);

  attach(extent);
}

TRect JournalWindow::prepare(TScrollBar*& horizontal, TScrollBar*& vertical)
{
  TRect extent;

  options  |= ofTileable;
  growMode  = gfGrowAll | gfGrowRel;

  horizontal = standardScrollBar(sbHorizontal | sbHandleKeyboard);
  vertical   = standardScrollBar(sbVertical   | sbHandleKeyboard);

  extent = getExtent();
  extent.grow(-1, -1);
  extent.a.y ++;

  return extent;
}

void JournalWindow::attach(TRect extent)
{
  // Heading takes the first row inside the frame
  extent.b.y    = extent.a.y;
  extent.a.y   -= 1;
  view->header  = new JournalHeader(extent, view);

  insert(view->header);
  insert(view);

  view->arrange();
}

const char* JournalWindow::getTitle(short size)
{
  const char* name;
  std::string order;

  // The sign goes first, the order last; a long filter is cut in front of the order so that it stays
  caption  = view->sign();
  caption += ' ';
  caption += title;
  name     = view->name();
  order    = std::string(" ") + view->order();

  if (*name != '\0')
  {
    caption += ' ';
    caption += name;
  }

  while ((!caption.empty()) &&
         (strwidth(caption) + strwidth(order) > size))
  {
    // A character at a time, its continuation bytes first
    while ((caption.size() > 1) && ((caption.back() & 0xc0) == 0x80))
    {
      caption.pop_back();
    }

    caption.pop_back();
  }

  caption += order;
  return caption.c_str();
}

void JournalWindow::handleEvent(TEvent& event)
{
  int x;
  int room;
  int span;
  int start;
  TPoint mouse;

  if ((event.what == evMouseDown) &&
      (state & sfSelected) &&
      (frame != nullptr) &&
      ((mouse = frame->makeLocal(event.mouse.where)).y == 0))
  {
    // The frame centers the title in the room its icons and the number leave, the same way here; a window that is not
    // focused yet takes the click to be focused as any other
    room  = size.x - 10 - ((flags & (wfClose | wfZoom)) ? 6 : 0) - (((number != wnNoNumber) && (number < 10)) ? 4 : 0);
    span  = std::max(std::min(strwidth(getTitle(room)), size.x - 10), 0);
    start = (size.x - span) >> 1;
    x     = mouse.x - start;

    if ((x >= 0) &&
        (x < strwidth(view->sign())))
    {
      // ► or ‖ with its count, as Space
      message(view, evCommand, cmJournalPause, nullptr);
      clearEvent(event);
      return;
    }

    if ((x < span) &&
        (x >= span - strwidth(view->order())))
    {
      // ▲ or ▼, as Ctrl-R
      message(view, evCommand, cmJournalReverse, nullptr);
      clearEvent(event);
      return;
    }
  }

  // The rest of the title moves the window, a double click zooms it
  TWindow::handleEvent(event);
}
