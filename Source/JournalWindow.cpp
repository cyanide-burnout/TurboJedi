// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TFrame
#include "JournalWindow.h"
#include "JournalHeader.h"

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
