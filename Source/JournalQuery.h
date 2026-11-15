#ifndef JOURNALQUERY_H
#define JOURNALQUERY_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <stdint.h>

#include <vector>
#include <string>

#include "JournalTools.h"
#include "JournalMatcher.h"

typedef std::vector<std::pair<std::string, std::string>> JournalRecord;

// Entries of a filter read for a plugin, the way a window reads them: the journal applies the matches, the text is matched
// here against identifier[pid]: message, the times bound the range. Bounded by the entries kept and the entries looked at,
// read in steps so that the screen goes on meanwhile

class JournalQuery
{
  public:

    JournalQuery();
    ~JournalQuery();

    // From the entry of a cursor, which is left out, or from the end the direction starts at: the newest one going older,
    // the oldest one going newer; false and the reason when the journal cannot be read
    bool start(const JournalFilter& filter, const std::string& cursor, bool older, size_t limit, size_t scan, std::string& error);

    // Reads up to the deadline in milliseconds of CLOCK_MONOTONIC, true once over
    bool step(int64_t deadline);
    void stop(const char* reason);

    std::vector<JournalRecord> records;
    const char* stopped;  // Why it has ended before the journal or the range: limit, scan, time, cancel; nullptr when it has not
    size_t scanned;       // Entries of the matches looked at
    bool over;

  private:

    sd_journal* journal;
    JournalMatcher matcher;
    bool exclude;
    bool older;
    bool held;            // The journal is on an entry not looked at yet
    size_t limit;
    size_t scan;
    uint64_t since;       // Microseconds, zero for an open end
    uint64_t until;
};

int64_t GetQueryMilliseconds();

// Reads it through for an action: without a word for a moment, then with a dialog and Cancel while the windows go on, until
// the timeout in seconds, zero for none. A handler making the menu calls step() with the deadline of the menu instead
void RunJournalQuery(JournalQuery& query, int timeout);

#endif
