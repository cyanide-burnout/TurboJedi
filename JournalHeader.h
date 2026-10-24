#ifndef JOURNALHEADER_H
#define JOURNALHEADER_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TView
#include <tvision/tv.h>

class JournalView;

// Column titles above the lines of a journal window, they stay in place while the message scrolls

class JournalHeader : public TView
{
  public:

    JournalHeader(const TRect& bounds, JournalView* view);

    void draw() override;

  private:

    JournalView* view;
};

#endif
