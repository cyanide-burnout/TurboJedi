// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TPalette
#define Uses_TGroup
#include "PageView.h"
#include "Commands.h"

#include <algorithm>

// Normal and highlighted text of a scroller, then the focused item of a list viewer for the cursor
#define cpPageView  "\x06\x07\x1B"

// PageView

PageView::PageView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const std::vector<std::string>& lines, bool choosing) :
  TScroller(bounds, horizontal, vertical)
{
  size_t index;
  size_t start;
  size_t end;
  int width;

  growMode = gfGrowHiX | gfGrowHiY;
  options |= ofFirstClick;
  cursor   = choosing ? 0 : -1;
  width    = 0;

  for (index = 0; index < lines.size(); index ++)
  {
    for (start = 0; start <= lines[index].size(); start = end + 1)
    {
      // A value of several lines goes on indented under its name
      end = std::min(lines[index].find('\n', start), lines[index].size());
      this->lines.push_back(((start == 0) ? std::string() : std::string("    ")) + lines[index].substr(start, end - start));
      origins.push_back(index);
      width = std::max(width, strwidth(this->lines.back()));
    }
  }

  setLimit(width, this->lines.size());
}

void PageView::draw()
{
  int y;
  size_t index;
  TDrawBuffer buffer;
  TColorAttr color;

  for (y = 0; y < size.y; y ++)
  {
    index = delta.y + y;
    color = ((int)index == cursor) ? mapColor(3) : mapColor(1);
    buffer.moveChar(0, ' ', color, size.x);

    if (index < lines.size())
    {
      // Horizontal scrolling as in any other scroller
      buffer.moveStr(0, lines[index], color, size.x, delta.x);
    }

    writeLine(0, y, size.x, 1, buffer);
  }
}

void PageView::handleEvent(TEvent& event)
{
  if ((cursor >= 0) &&
      (event.what == evKeyDown))
  {
    switch (event.keyDown.keyCode)
    {
      case kbUp:        move(cursor - 1);                 break;
      case kbDown:      move(cursor + 1);                 break;
      case kbPgUp:      move(cursor - (size.y - 1));      break;
      case kbPgDn:      move(cursor + (size.y - 1));      break;
      case kbCtrlPgUp:  move(0);                          break;
      case kbCtrlPgDn:  move((int)lines.size() - 1);      break;
      default:          TScroller::handleEvent(event);    return;
    }

    clearEvent(event);
    return;
  }

  TScroller::handleEvent(event);

  if ((cursor >= 0) &&
      (event.what == evMouseDown) &&
      (event.mouse.buttons & mbLeftButton))
  {
    // Click puts the cursor on a line, double click chooses it
    move(delta.y + makeLocal(event.mouse.where).y);

    if (event.mouse.eventFlags & meDoubleClick)
    {
      message(owner, evCommand, cmPageFilter, this);
    }

    clearEvent(event);
  }
}

TPalette& PageView::getPalette() const
{
  static TPalette palette(cpPageView, sizeof(cpPageView) - 1);

  return palette;
}

void PageView::move(int line)
{
  cursor = std::clamp(line, 0, std::max(0, (int)lines.size() - 1));

  // The cursor stays on the screen
  if (cursor < delta.y)            scrollTo(delta.x, cursor);
  if (cursor >= delta.y + size.y)  scrollTo(delta.x, cursor - size.y + 1);

  drawView();
}
