#ifndef PAGEVIEW_H
#define PAGEVIEW_H

#define Uses_TScroller
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <vector>
#include <string>

// Read-only lines of text that scroll both ways

class PageView : public TScroller
{
  public:

    PageView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const std::vector<std::string>& lines);

    std::vector<std::string> lines;

    void draw() override;
};

#endif
