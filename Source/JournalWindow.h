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

    JournalWindow(const TRect& bounds, short number, const JournalFilter& filter, int history, size_t capacity);
    JournalWindow(const TRect& bounds, short number, const JournalView& source);  // Clone of the window of the source

    JournalView* view;

    const char* getTitle(short size) override;

  private:

    std::string caption;

    TRect prepare(TScrollBar*& horizontal, TScrollBar*& vertical);  // Scroll bars and the room for the lines
    void attach(TRect extent);                                      // Heading above the view and both inserted
};

#endif
