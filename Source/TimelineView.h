#ifndef TIMELINEVIEW_H
#define TIMELINEVIEW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScroller
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <stdint.h>

#include <array>
#include <vector>
#include <string>

#include "Commands.h"
#include "JournalTools.h"
#include "JournalMatcher.h"

// Entries of a filter counted by intervals of time: errors, warnings above them and the rest on top,
// a column per interval; the journal of its own is read in batches on the polls and follows the tail afterwards

class TimelineView : public TScroller
{
  public:

    // Seconds of a step, zero chooses one that fits the range into the window
    TimelineView(const TRect& bounds, TScrollBar* horizontal, const JournalFilter& filter, int seconds, bool logarithmic);
    TimelineView(const TRect& bounds, TScrollBar* horizontal, const TimelineView& source);  // A clone goes on counting from where the source is
    ~TimelineView();

    void draw() override;
    void handleEvent(TEvent& event) override;
    void changeBounds(const TRect& bounds) override;

    // What a layout keeps
    const JournalFilter& getFilter() const;
    int getStep();  // Seconds of the step chosen by + and -, zero when the window has chosen it
    bool getLogarithmic();

  private:

    struct Interval
    {
      std::array<uint64_t, 3> counts;  // Errors and worse, warnings, the rest
      uint64_t first;                  // Microseconds of the first entry counted, windows go there
    };

    sd_journal* journal;
    JournalFilter filter;
    JournalMatcher matcher;  // Text of the filter, applied the way a journal window applies it
    std::string error;       // Why there is nothing to show

    uint64_t since;    // Microseconds, zero when the range is open
    uint64_t until;
    uint64_t start;    // Of the first interval, aligned to the step in local time
    uint64_t step;     // Microseconds of an interval
    uint64_t chosen;   // Microseconds of the step chosen by + and -, zero lets the width of the window choose
    uint64_t end;      // Time of the newest entry the step has been chosen for
    uint64_t done;     // Time of the entry read last, for the progress
    bool closed;       // An entry after the range has been met, nothing is read anymore
    bool pending;      // Reading has stopped at the end of its batch, the next poll goes on
    bool logarithmic;
    bool follow;       // The cursor stays on the newest interval

    std::vector<Interval> intervals;
    size_t cursor;

    void open();
    void begin(uint64_t fixed);
    void read();
    void refresh();
    bool add(uint64_t realtime, int priority);
    void move(int64_t index);
    void reveal();
    void synchronize();
    void zoom(int direction);
    void heights(const Interval& interval, uint64_t peak, int room, int* levels);
    std::string label(uint64_t realtime, bool full);
};

#endif
