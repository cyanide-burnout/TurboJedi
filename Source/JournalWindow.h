#ifndef JOURNALWINDOW_H
#define JOURNALWINDOW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TWindow
#include <tvision/tv.h>

#include <string>

#include "JournalView.h"

// Lines of the journal with the heading above them, the title tells the filter

class JournalWindow : public TWindow
{
  public:

    JournalWindow(const TRect& bounds, short number, const std::string& directory, const JournalFilter& filter, int history, size_t capacity);

    JournalView* view;

    const char* getTitle(short size) override;

  private:

    std::string caption;
};

#endif
