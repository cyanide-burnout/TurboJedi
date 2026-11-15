#ifndef JOURNALWATCH_H
#define JOURNALWATCH_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <stdint.h>

#include <deque>
#include <string>

#include "JournalTools.h"
#include "JournalQuery.h"
#include "JournalSource.h"
#include "JournalMatcher.h"

#define WATCH_LINE_LIMIT  1000   // Lines of a watch, the oldest go
#define WATCH_QUEUE       1000   // Entries read and not given to the plugin yet, the reading waits beyond them
#define WATCH_LAG         60000  // Milliseconds a watch may stay behind the end of its journal before it jumps there
#define WATCH_RETRY       1000   // Milliseconds after a change of the files the entry read last is looked for
#define WATCH_ATTEMPTS    5      // Looks for it in that time

class JournalView;

enum JournalWatchState
{
  WATCH_ACTIVE,   // Reads its journal
  WATCH_STOPPED,  // Its journal is gone, or its plugin is not there: kept with its lines and the reason
  WATCH_FAILED,   // Its plugin has failed in a callback: kept the same way
  WATCH_REMOVED   // Gone, kept only until the callbacks running now are over
};

// A part of the journal the watch has not seen, microseconds of CLOCK_REALTIME of the entries around it: the range is
// rough, the clocks of a merged journal go their own ways. Uncertain when the part may as well have nothing missing
struct JournalGap
{
  uint64_t since;
  uint64_t till;
  bool uncertain;
};

// The place of a watch in its journal: the entry given to the plugin last and its time, kept apart as the cursor is
// opaque. Empty before the first one
struct JournalPosition
{
  std::string cursor;
  uint64_t realtime;
};

// What the reading has for the plugin, in the order of the journal: an entry, or a gap before the entries after it
struct JournalWatchEvent
{
  bool gap;
  JournalGap range;
  JournalRecord fields;
  JournalPosition position;
};

// A line the plugin has added to the list of its watch
struct JournalWatchLine
{
  uint64_t number;  // Given back by add(), only grows
  uint64_t moment;  // Microseconds of CLOCK_REALTIME it has been added at
  std::string text;
  JournalPosition entry;  // Empty without an entry
  bool unread;
};

// A watch of a plugin: a journal of its own read from its end for the entries of a filter as they come, and what the
// plugin tells about them, a status and a list of lines. Lua is not known here, the engine of the plugins gives the
// entries to it

class JournalWatch
{
  public:

    JournalWatch();
    ~JournalWatch();

    // Opens the journal of the filter: after the entry of the position, or at its end without one; false and the reason
    // when the journal cannot be opened
    bool start(const JournalFilter& filter, const JournalPosition* from, std::string& error);

    // Reads what has come, up to the limit of entries and the deadline in milliseconds of CLOCK_MONOTONIC. Paused while a
    // dialog is open: nothing is read and the time behind does not count
    void poll(size_t limit, int64_t deadline, bool paused);

    // Milliseconds up to the limit before it has to be polled again
    int wait(int limit);

    // Gives up the journal and keeps the lines with the reason
    void stop(JournalWatchState state, const std::string& reason);

    uint64_t add(const std::string& text, const JournalPosition& entry);
    void erase(uint64_t number);
    void clear();
    size_t unread() const;
    bool restoring() const;  // Waits for an entry to tell a gap with, the journal has nothing yet

    int number;            // Of the engine, its handle in Lua tells it
    std::string plugin;
    std::string title;
    std::string status;    // Told by the plugin
    std::string reason;    // Why it is stopped or failed
    JournalWatchState state;
    bool global;           // Set while its plugin has been loaded, a layout leaves it alone
    bool fresh;            // Lines have come since the list of watches has been seen
    bool raised;           // A line has raised the list, shown red until it is read
    JournalView* window;   // The window it goes to an entry in, nullptr without one; it is told when the window goes
    JournalFilter filter;  // Its journal and its matches, a window opened for it takes them

    std::deque<JournalWatchLine> lines;
    uint64_t serial;       // The number of the next line
    uint64_t latest;       // Microseconds of the last line added, zero before
    uint64_t order;        // Of its making, watches without lines go in it
    unsigned gaps;         // Told so far, the last one kept
    JournalGap gap;

    std::deque<JournalWatchEvent> events;  // Read and not given to the plugin yet
    JournalPosition delivered;             // The entry given to the plugin last
    int64_t lag;                           // Milliseconds behind the end before the jump, WATCH_LAG unless a test says else

    std::string context;   // Of the plugin in the notation of a layout, empty when the watch is not kept there
    std::string saved;     // The line of a layout a watch of a plugin that is not there keeps as it is
    int callback;          // Of the engine in the registry of Lua, LUA_NOREF without
    int self;              // Its table there, the same in every callback

  private:

    sd_journal* journal;
    JournalSource* notifier;
    bool blind;              // Its file system tells nothing about the changes, see JournalView
    std::string files;
    unsigned faults;
    JournalMatcher matcher;  // Text of the filter
    bool exclude;

    JournalPosition place;   // The entry read last, the journal goes on after it
    uint64_t begun;          // Microseconds of CLOCK_REALTIME the watch has started at
    int64_t reached;         // Milliseconds of CLOCK_MONOTONIC the end of the journal has been reached at last
    bool more;               // The last poll has stopped at its limits before the end

    bool checking;           // The files have changed, the entry read last is looked for
    int attempts;
    int64_t invalidated;     // Milliseconds of the last change of the files

    bool gapping;            // A gap is told along with the next entry read, it ends there
    uint64_t since;
    bool uncertain;

    void attach();
    void close();
    void remember();          // Takes the entry the journal is on as the place
    bool find();              // Puts the journal back on the place, true when its entry is there
    void recover(bool wait);  // After a change of the files or a start from a position
    void fall();              // The entry of the place is gone for good: on after its time, with a gap
    void jump();              // Behind for too long: to the end, with a gap
};

#endif
