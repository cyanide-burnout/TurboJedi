// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TFrame
#include "JournalWindow.h"
#include "JournalHeader.h"

// JournalWindow

JournalWindow::JournalWindow(const TRect& bounds, short number, const std::string& directory, const JournalFilter& filter, int history, size_t capacity) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, "Journal", number)
{
  TRect extent;
  TScrollBar* horizontal;
  TScrollBar* vertical;

  options  |= ofTileable;
  growMode  = gfGrowAll | gfGrowRel;

  horizontal = standardScrollBar(sbHorizontal | sbHandleKeyboard);
  vertical   = standardScrollBar(sbVertical   | sbHandleKeyboard);

  extent = getExtent();
  extent.grow(-1, -1);
  extent.a.y ++;
  view = new JournalView(extent, horizontal, vertical, directory, filter, history, capacity);

  // Heading takes the first row inside the frame
  extent.b.y    = extent.a.y;
  extent.a.y   -= 1;
  view->header  = new JournalHeader(extent, view);

  insert(view->header);
  insert(view);
}

const char* JournalWindow::getTitle(short size)
{
  const char* name;

  name    = view->name();
  caption = title;

  if (*name != '\0')
  {
    caption += ' ';
    caption += name;
  }

  return caption.c_str();
}
