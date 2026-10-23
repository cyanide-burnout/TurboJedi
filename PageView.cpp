#define Uses_TDrawBuffer
#include "PageView.h"

#include <algorithm>

// PageView

PageView::PageView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const std::vector<std::string>& lines) :
  TScroller(bounds, horizontal, vertical)
{
  size_t start;
  size_t end;
  int width;

  growMode = gfGrowHiX | gfGrowHiY;
  width    = 0;

  for (const std::string& line : lines)
  {
    for (start = 0; start <= line.size(); start = end + 1)
    {
      // A value of several lines goes on indented under its name
      end = std::min(line.find('\n', start), line.size());
      this->lines.push_back(((start == 0) ? std::string() : std::string("    ")) + line.substr(start, end - start));
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

  color = mapColor(1);

  for (y = 0; y < size.y; y ++)
  {
    index = delta.y + y;
    buffer.moveChar(0, ' ', color, size.x);

    if (index < lines.size())
    {
      // Horizontal scrolling as in any other scroller
      buffer.moveStr(0, lines[index], color, size.x, delta.x);
    }

    writeLine(0, y, size.x, 1, buffer);
  }
}
