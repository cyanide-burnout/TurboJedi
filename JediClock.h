#ifndef JEDICLOCK_H
#define JEDICLOCK_H

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
