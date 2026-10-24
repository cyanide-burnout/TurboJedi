#ifndef JOURNALVIEW_H
#define JOURNALVIEW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScroller
#define Uses_TScrollBar
#define Uses_TWindow
#include <tvision/tv.h>

#include <deque>
#include <regex>
#include <vector>
#include <string>

#include "Commands.h"
#include "JournalTools.h"

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
};

class JournalView;

struct JournalMoment
{
  JournalView* source;  // Goes there already
  uint64_t realtime;
};

class JournalView : public TScroller
{
  public:

    JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const std::string& directory, const JournalFilter& filter, int history, size_t capacity);
    ~JournalView();

    uint64_t counts[8];  // Entries per priority since the window has been opened or cleared

    void draw() override;
    void handleEvent(TEvent& event) override;
    void changeBounds(const TRect& bounds) override;

    const JournalFilter& getFilter();
    const char* describe();
    const char* name();
    std::string heading();

    TView* header;  // Shows the heading above, redrawn along with the lines

  private:

    sd_journal* journal;
    std::string directory;
    JournalFilter filter;
    int history;
    size_t capacity;

    std::deque<JournalLine> lines;
    uint64_t first;  // Absolute number of lines.front(), far from zero so history can go in front
    int width;

    std::string buffer;

    int widths[4];   // Host, unit, identifier and PID columns, as wide as their longest values within limits

    std::regex expression;  // Of the text filter when it is a regular expression

    std::string search;     // What Find looks for, highlighted in the lines
    uint16_t mode;          // JOURNAL_TEXT_* of the search
    std::regex finder;

    uint64_t since;  // Microseconds, zero when the range is open
    uint64_t until;
    bool closed;     // An entry after the range has been met, nothing is read anymore
    bool pending;    // Reading has stopped at the end of its batch, the next poll goes on
    bool older;      // History in front of the first line may still be there

    std::vector<uint64_t> rows;  // Wrapped lines: rows in front of every line, the total last
    int layout;                  // Width the rows have been counted for, zero when they are stale

    uint64_t unseen;
    bool follow;
    bool moving;

    // Absolute line numbers, so selection survives trimming
    uint64_t anchor;
    uint64_t cursor;

    std::string status;
    std::string caption;

    void open();
    void read();
    size_t back();
    bool parse(sd_journal* source, JournalLine& model, std::string& message);
    void split(const JournalLine& model, const std::string& message, std::vector<JournalLine>& parts);
    void push(JournalLine& line);
    void measure(const JournalLine& line);
    int indent();
    int span();
    void count();
    uint64_t total();
    uint64_t rowOf(size_t index);
    size_t lineOf(uint64_t row);
    size_t focus();
    void format(uint64_t realtime, char* date, char* clock);
    std::string compose(const JournalLine& line);
    void trim();
    void clear();
    void pause();
    void refresh();
    void select(int64_t index, bool extend);
    void reveal(size_t index);
    void copy();
    void save();
    void details();
    void find(int direction);
    void seek(uint64_t realtime, bool nearest);  // The first line at the moment or after it, or the closest one
    void synchronize();
    void chooseLevels();
    void chooseText();
    void chooseSource();
    void chooseTime();
    void chooseColumns();
    void chooseFind();
    void chooseMoment();
};

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
