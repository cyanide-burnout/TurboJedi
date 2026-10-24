#ifndef JEDICLOCK_H
#define JEDICLOCK_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TView
#include <tvision/tv.h>

#include <time.h>

class JediClock : public TView
{
  public:

    JediClock(const TRect& bounds);

    void draw() override;
    void update();

  private:

    time_t second;
    char text[16];
};

#endif
