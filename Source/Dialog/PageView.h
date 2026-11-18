#ifndef PAGEVIEW_H
#define PAGEVIEW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScroller
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <vector>
#include <string>

// Read-only lines of text that scroll both ways, a cursor marks a line to choose when asked for

class PageView : public TScroller
{
  public:

    PageView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const std::vector<std::string>& lines, bool choosing);

    std::vector<std::string> lines;
    std::vector<size_t> origins;  // Index of the given line every shown one comes from
    int cursor;                   // Shown line, -1 without choosing

    void draw() override;
    void handleEvent(TEvent& event) override;
    TPalette& getPalette() const override;

  private:

    void move(int line);
};

#endif
