// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#include "JediClock.h"

#include <time.h>

// JediClock

JediClock::JediClock(const TRect& bounds) :
  TView(bounds)
{
  growMode = gfGrowLoX | gfGrowHiX;
  second   = 0;
  text[0]  = '\0';
}

void JediClock::draw()
{
  TDrawBuffer buffer;
  TColorAttr color;

  // Same colors as the menu bar it sits on
  color = getColor(2);

  buffer.moveChar(0, ' ', color, size.x);
  buffer.moveStr(1, text, color);
  writeLine(0, 0, size.x, 1, buffer);
}

void JediClock::update()
{
  struct tm date;
  time_t now;

  now = time(nullptr);

  if (now != second)
  {
    localtime_r(&now, &date);
    strftime(text, sizeof(text), "%T", &date);
    second = now;
    drawView();
  }
}
