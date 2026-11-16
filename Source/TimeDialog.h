#ifndef TIMEDIALOG_H
#define TIMEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TCheckBoxes
#define Uses_TParamText
#include <tvision/tv.h>

#define TIME_LENGTH  64

struct TimeData
{
  char since[TIME_LENGTH];
  char until[TIME_LENGTH];
  ushort utc;  // The times are typed in UTC, not in the local zone
};

class TimeDialog : public TDialog
{
  public:

    TimeDialog();

    void handleEvent(TEvent& event) override;
    void setData(void* record) override;
    Boolean valid(ushort command) override;

  private:

    TInputLine* lines[2];  // Since and Until, in the order of TimeData
    TCheckBoxes* zone;
    TParamText* told;      // The zone the times are typed in
    bool utc;              // As the times are written now

    void pick(int index);
    void convert();
    void tell();
};

#endif
