#ifndef CALENDARVIEW_H
#define CALENDARVIEW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TView
#include <tvision/tv.h>

#include <time.h>

class CalendarView : public TView
{
  public:

    CalendarView(const TRect& bounds, const struct tm& date);

    struct tm date;  // Chosen day, its time of day is kept at noon away from DST shifts

    void draw() override;
    void handleEvent(TEvent& event) override;

  private:

    void move(int days, int months);
};

#endif
