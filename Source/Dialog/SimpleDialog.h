#ifndef SIMPLEDIALOG_H
#define SIMPLEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TCheckBoxes
#include <tvision/tv.h>

#include <string>

#include "JournalTools.h"

#define PID_LENGTH  16

// The simple filter, as Source and Text have been before 0.20: a value of each field of Source and PID, with the lists of
// the values in the journal, and a text to show or to leave out. The levels and the times stay as they are, the line under
// the fields tells of them; Advanced... opens the window of the rules with what has been typed. A filter of more, a value
// left out, a value more of a field or a text more, opens in that window at once

class SimpleDialog : public TDialog
{
  public:

    SimpleDialog(const JournalFilter& filter);

    void handleEvent(TEvent& event) override;
    Boolean valid(ushort command) override;
    void apply(JournalFilter& filter) const;  // The fields of Source, PID and the text, the levels and the times untouched

  private:

    JournalFilter journals;  // Where the lists come from
    TInputLine* lines[JOURNAL_FIELD_COUNT + 1];  // The last one is PID
    TInputLine* line;
    TCheckBoxes* modes;  // Of the text: not, Regex, Case

    void pick(int field);
};

// The simple filter shows all of it: no raw matches or values left out, a text at most
bool FitsSimpleFilter(const JournalFilter& filter);

#endif
