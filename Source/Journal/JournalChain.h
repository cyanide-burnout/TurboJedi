#ifndef JOURNALCHAIN_H
#define JOURNALCHAIN_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <systemd/sd-journal.h>

#include <deque>
#include <vector>
#include <string>

#include "JournalTools.h"
#include "JournalMatcher.h"

// What of a filter the journal cannot apply, done on this side: the values of fields left out, FIELD!=VALUE of its
// matches, the fields an entry has to have or not to have, FIELD=* and FIELD!=*, and its texts matched against
// identifier[pid]: message. An entry passes when it has none of the values left out, by the first value of the field,
// has the fields asked for and not the others, and passes every text

class JournalChain
{
  public:

    // False when an expression of a text is broken, it is taken as a plain text then
    bool set(const JournalFilter& filter);
    bool empty() const;

    // The entry the journal is on passes; line is identifier[pid]: message when the one who asks has it already
    bool pass(sd_journal* journal, const std::string* line = nullptr);

  private:

    struct Text
    {
      JournalMatcher matcher;
      bool exclude;
    };

    std::deque<Text> texts;
    std::vector<std::pair<std::string, std::string>> exclusions;  // FIELD and FIELD=VALUE
    std::vector<std::pair<std::string, bool>> presences;           // FIELD and whether it has to be there
};

// A text of a broken expression of a filter becomes a plain one, as a window takes it, a layout may bring one
void RepairJournalTexts(JournalFilter& filter);

#endif
