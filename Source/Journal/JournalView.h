#ifndef JOURNALVIEW_H
#define JOURNALVIEW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScroller
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <set>
#include <deque>
#include <vector>
#include <string>

#include "Commands.h"
#include "JournalTools.h"
#include "JournalSource.h"
#include "JournalMatcher.h"
#include "JournalChain.h"
#include "JournalHighlighter.h"
#include "JournalTinter.h"

#include <memory>

struct JournalLine
{
  uint64_t realtime;  // Microseconds, formatted the way the window wants when drawn
  int priority;
  std::string host;
  std::string unit;
  std::string identifier;
  std::string pid;
  std::string cursor;  // Of the journal entry, its details are read by it
  std::string text;
  std::vector<JournalTint> tints;  // Colors of the escape sequences the message has come with, taken out of the text
  bool continued = false;  // A line of a multi-line message after its first one, the columns are left out
};

class JournalView;

#define JOURNAL_PLACE_DEPTH  64  // Steps the history of a window remembers, every move of the cursor is one

struct JournalPlace
{
  uint64_t top;     // Absolute numbers of lines like the selection
  uint64_t anchor;
  uint64_t cursor;
  bool follow;
  uint64_t generation;                          // The query the numbers are of, each filter opens one of its own
  std::shared_ptr<const JournalFilter> filter;  // The filter there, a step back to another one takes it again
  std::string entry;                            // The cursor of the journal of the line the window has been on
  uint64_t realtime;                            // And its time, the nearest entry when the filter has left it out
};

struct JournalMoment
{
  JournalView* source;  // Goes there already
  uint64_t realtime;
};

class JournalView : public TScroller
{
  public:

    JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalFilter& filter, int history, size_t capacity);
    JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalView& source);  // A clone goes on from where the source is
    ~JournalView();

    uint64_t counts[8];  // Entries per priority since the window has been opened or cleared

    void draw() override;
    void handleEvent(TEvent& event) override;
    void changeBounds(const TRect& bounds) override;
    void setState(ushort state, Boolean enable) override;

    const JournalFilter& getFilter() const;

    // For the actions of plugins, what Find and Go to time do: false when the text is a broken expression or is not
    // found, older goes towards the older entries whatever the order of the window
    bool search(const std::string& text, uint16_t mode, bool older);
    void jump(uint64_t realtime);

    // The cursor on the entry of a cursor, what has come read in and the history brought in as for a time; near its time
    // when the window does not show it, false then
    bool reach(const std::string& cursor, uint64_t realtime);

    // Marks the lines of the entries of the cursors as M does, every line of a message; the history comes in down to the
    // oldest of them, in microseconds, as far as the window holds, by steps with a dialog and Cancel when it takes long.
    // Clear takes the other marks off first. The count of the entries marked, fewer when some are not in the window
    size_t mark(const std::set<std::string>& cursors, uint64_t oldest, bool clear);

    // The same by parts for a caller of the bus, without a dialog: what has come read in and the history brought in by
    // steps up to the deadline in milliseconds of CLOCK_MONOTONIC, true once all of it or the oldest is there; then the
    // marks of the lines the window has. settle() without a deadline reads all there is at once
    bool settle(int64_t deadline = INT64_MAX);
    bool bring(uint64_t oldest, int64_t deadline);
    uint64_t earliest();     // Microseconds of the oldest entry the window has, zero without one
    size_t markLoaded(const std::set<std::string>& cursors, bool clear);

    std::string current();   // Cursor of the entry Enter and the actions take, empty without one
    bool following() const;  // The window follows the end of its journal
    unsigned id() const;     // Of the window as long as it is open, the bus calls it so

    // Cursors of the entries of the selection and the marks, the oldest first whatever the order, up to the limit; total
    // tells how many entries there are. None when only the cursor is there
    std::vector<std::string> pickedCursors(size_t limit, size_t& total);
    const char* describe();
    const char* name();
    const char* sign();  // Of the title: ► following, ‖ paused, with +N entries come meanwhile
    const char* order();  // At the end of the title: ▲ the newest at the bottom, ▼ the newest on top
    const char* shows();  // In front of the order: ◇ all the lines, ◆ the marked ones only
    bool markedOnly() const;  // The window shows its marked lines only, Ctrl-D
    std::string heading();
    void arrange();  // Shows or hides the heading as the columns say, the lines take its row

    TView* header;  // Shows the heading above, redrawn along with the lines

  private:

    unsigned identity;
    sd_journal* journal;
    JournalSource* notifier;  // Wakes the application when the journal changes
    bool blind;               // Its file system tells nothing about the changes, it is read on every poll
    std::string files;        // Of the directory of a blind journal, a change of them opens the journal again
    unsigned faults;          // Faults of mapped files seen when it has been opened, a new one opens a blind journal again
    JournalFilter filter;
    int history;
    size_t capacity;

    // In the order of the screen: the newest entry last, or first with the newest on top; the lines of a message
    // go under it either way
    std::deque<JournalLine> lines;
    size_t bytes;    // Held by the lines, bounded like their count
    uint64_t first;  // Absolute number of lines.front(), far from zero so lines can go in front
    int width;

    std::string buffer;

    int widths[4];   // Host, unit, identifier and PID columns, as wide as their longest values within limits

    JournalChain chain;      // What of the filter is done on this side

    JournalMatcher finder;   // What Find looks for, highlighted in the lines

    std::shared_ptr<JournalHighlighter> highlighter;  // Of the window, nullptr when off
    std::vector<JournalStroke> strokes;

    uint64_t since;  // Microseconds, zero when the range is open
    uint64_t until;
    bool closed;     // An entry after the range has been met, nothing is read anymore
    bool pending;    // Reading has stopped at the end of its batch, the next poll goes on
    bool older;      // History in front of the first line may still be there
    std::string past; // Cursor of the oldest entry the history has looked at, it goes on from there
    sd_journal* elder; // The journal the history is read from, kept from one step to the next while the filter stays
    int wanted;       // Entries of the history a filter done on this side is still looking for, by steps of the polls

    std::vector<uint64_t> rows;  // Wrapped lines: rows in front of every line, the total last
    int layout;                  // Width the rows have been counted for, zero when they are stale

    uint64_t unseen;
    bool follow;
    bool moving;
    bool held;        // Paused with no room for an entry: what comes is counted, not kept, nothing on the screen or marked is dropped
    uint64_t counted; // Of unseen, the entries only counted while held, read again after the last line

    // Absolute line numbers, so selection survives trimming: the cursor, a range from the anchor to it made by Shift
    // or by a stroke of M, and the lines marked by the strokes before, which need not be next to each other
    uint64_t anchor;
    uint64_t cursor;
    bool marking;               // A stroke of M or Insert: the keys mark from where it has begun, Shift is kept by some terminals
    bool unmark;                // The stroke has begun on a marked line, it takes the marks off the lines it passes
    std::set<uint64_t> marks;
    bool only;                  // Marked only: a line not marked takes no row, drawing, scrolling and the mouse pass it by

    // Places of the jumps as in a browser, in a ring from the tail, the oldest, to the head, past the newest: Back and
    // Forward move the step between them, a new jump drops the ones after the step and goes on from there, a full ring
    // forgets its oldest; the place of the step is taken anew whenever the window leaves it. The numbers only grow,
    // the ring is indexed by them modulo its size
    JournalPlace trail[JOURNAL_PLACE_DEPTH];
    size_t tail;
    size_t head;
    size_t step;
    uint64_t generation;                         // Of the query open, the places of another one go by their entries
    std::shared_ptr<const JournalFilter> applied;  // The filter of the lines there are
    bool landing;                                // reach() as a part of a jump, not one of its own

    std::string status;
    std::string caption;
    std::string badge;   // Sign the title has been drawn with, a new one draws the frame again

    void attach();
    void open(uint64_t at = 0);  // From the end, or around the time of an entry, paused
    void reopen();  // open() with a new filter, back on the entry it has been on or the nearest one in time
    void land(const std::string& entry, uint64_t realtime);  // On the entry of a place, a part of the jump at hand
    void read();
    size_t back(int64_t deadline = 0, int* entries = nullptr);  // Lines in front, up to the deadline, without one until
                                                                // it has found an entry; entries tells their entries
    void dig(int64_t deadline);         // The history wanted, up to the deadline
    bool parse(sd_journal* source, JournalLine& model, std::string& message);
    void split(const JournalLine& model, const std::string& message, std::vector<JournalLine>& parts);
    void push(std::vector<JournalLine>& parts);  // The lines of an entry after the newest one
    void measure(const JournalLine& line);
    int indent();
    int span();
    void count();
    uint64_t total();
    bool rowed();                    // Rows are counted per line: lines wrap, or the ones not marked take none
    int height(size_t index);        // Rows of a line
    uint64_t rowOf(size_t index);
    size_t lineOf(uint64_t row);
    size_t shown(size_t index);      // The line itself, or the nearest one shown when it is hidden by Marked only
    void restrict();                 // Marked only on or off, Ctrl-D
    size_t focus();
    bool reversed();                 // The newest entries on top
    const JournalLine& edge(bool newest);  // The line at the newest or the oldest end, there has to be one
    size_t latest();                 // The line on the screen nearest to the newest end
    bool oldest();                   // The screen is at the end of the oldest lines, the history comes in there
    void reverse();                  // The other order, with the same lines selected and on the screen
    void format(uint64_t realtime, char* date, char* clock);
    std::string compose(const JournalLine& line);
    void trim();
    void clear();
    void pause();
    void refresh();
    bool fits(const std::vector<JournalLine>& parts);  // The lines of an entry go in whole without dropping any
    void select(int64_t index, bool extend);
    void walk(int64_t index, bool extend);  // select() by the keys or the mouse, a step of the history
    void mark();
    bool ranging();                  // The range from the anchor to the cursor is a part of the selection
    bool picked(uint64_t number);    // A line of the selection
    uint64_t selected();             // Lines of the selection there are, the cursor alone is none
    void reveal(size_t index);
    void copy();
    void save();
    void details();
    void actions(TPoint where);  // Of the plugins for the entry under the cursor, the menu there on the screen
    bool find(int direction, bool tell = true);  // False when nothing more is found, told unless asked not to
    void seek(uint64_t realtime, bool nearest);
    JournalPlace here();
    void remember();  // Before a jump
    void restore();   // Back
    void advance();   // Forward
    void travel(int direction);
    void remap(JournalPlace& place, const std::vector<size_t>& numbers, const std::vector<size_t>& bottoms);  // To the other order
    void synchronize();
    void chooseFilter(int start);
    void chooseSource();
    void chooseTime();
    void chooseColumns();
    void chooseFind();
    void chooseMoment();
    void chooseHighlight();
};

#endif
