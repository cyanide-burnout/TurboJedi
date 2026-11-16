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
#include "JournalSource.h"
#include "JournalMatcher.h"

// Entries of a filter counted by intervals of time: errors, warnings above them and the rest on top,
// a column per interval, or two in a cell by the dots of Braille; the journal of its own is read in batches on the polls
// and follows the tail afterwards

class TimelineView : public TScroller
{
  public:

    // Seconds of a step, zero chooses one that fits the range into the window
    TimelineView(const TRect& bounds, TScrollBar* horizontal, const JournalFilter& filter, int seconds, bool logarithmic, bool braille);
    TimelineView(const TRect& bounds, TScrollBar* horizontal, const TimelineView& source);  // A clone goes on counting from where the source is
    ~TimelineView();

    void draw() override;
    void handleEvent(TEvent& event) override;
    void changeBounds(const TRect& bounds) override;

    // What a layout keeps
    const JournalFilter& getFilter() const;
    int getStep();  // Seconds of the step chosen by + and -, zero when the window has chosen it
    bool getLogarithmic();
    bool getBraille();

  private:

    struct Range
    {
      std::string since;  // Of the filter, as typed or as a zoom has put it
      std::string until;
      uint64_t chosen;
    };

    struct Interval
    {
      std::array<uint64_t, 3> counts;  // Errors and worse, warnings, the rest
      uint64_t first;                  // Microseconds of the first entry counted, windows go there
    };

    sd_journal* journal;
    JournalSource* notifier;  // Wakes the application when the journal changes
    bool blind;               // Its file system tells nothing about the changes, it is read on every poll
    std::string files;        // Of the directory of a blind journal, a change of them opens the journal again
    unsigned faults;          // Faults of mapped files seen when it has been opened, a new one opens a blind journal again
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
    bool braille;      // Two intervals a cell, a quarter of a cell high a dot, instead of one in eighths of a block
    bool follow;       // The cursor stays on the newest interval

    std::vector<Interval> intervals;
    size_t cursor;
    int64_t anchor;            // The other end of the selection from the cursor, negative without one
    std::vector<Range> back;   // Ranges Ctrl-Z goes back to, the latest last
    std::vector<Range> ahead;  // And Ctrl-Y forward again

    void attach();
    void open();
    void begin(uint64_t fixed);
    void read();
    void refresh();
    bool add(uint64_t realtime, int priority);
    void move(int64_t index, bool extend);
    void point(int64_t cell, bool extend);
    void stretch(int64_t from, int64_t to);
    bool contains(int64_t cell);
    bool selection(size_t& lower, size_t& upper);
    Range selected();
    void narrow();
    void chooseTime();
    void change(const Range& range);
    void apply(const Range& range);
    void retreat();
    void advance();
    void reveal();
    void synchronize();
    void zoom(int direction);
    void restart(uint64_t fixed);
    int share();
    void heights(const Interval& interval, uint64_t peak, int units, int* levels);
    std::string label(uint64_t realtime, bool full);
};

#endif
