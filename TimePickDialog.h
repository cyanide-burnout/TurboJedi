#ifndef TIMEPICKDIALOG_H
#define TIMEPICKDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#include <tvision/tv.h>

#include <string>

#include "CalendarView.h"

class TimePickDialog : public TDialog
{
  public:

    TimePickDialog(const char* title, const char* value);

    std::string result;  // What goes to the field on OK

    void handleEvent(TEvent& event) override;

  private:

    CalendarView* calendar;
    TInputLine* clock;
};

#endif
