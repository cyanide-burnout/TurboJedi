#ifndef TIMEDIALOG_H
#define TIMEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#include <tvision/tv.h>

#define TIME_LENGTH  64

struct TimeData
{
  char since[TIME_LENGTH];
  char until[TIME_LENGTH];
};

class TimeDialog : public TDialog
{
  public:

    TimeDialog();

    void handleEvent(TEvent& event) override;

  private:

    TInputLine* lines[2];  // Since and Until, in the order of TimeData

    void pick(int index);
};

#endif
