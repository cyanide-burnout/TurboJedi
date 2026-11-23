#ifndef FILTERDIALOG_H
#define FILTERDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TCheckBoxes
#define Uses_TListViewer
#include <tvision/tv.h>

#include <vector>
#include <string>

#include "JournalTools.h"

#define FILTER_FIELD   0  // Where the window starts: the line of a field, the levels
#define FILTER_LEVELS  1

// The rules of a filter in the order given, as the title writes them; Delete takes one out, a double click or Space puts
// it back into its line to be changed

class RuleList : public TListViewer
{
  public:

    RuleList(const TRect& bounds, TScrollBar* bar, const std::vector<std::string>& items);

    void getText(char* dest, short item, short limit) override;
    void handleEvent(TEvent& event) override;  // Delete takes the rule out
    void selectItem(short item) override;      // A double click or Space puts it back into its line, Enter is OK

  private:

    const std::vector<std::string>& items;
};

// The whole filter of a window but its journals: rules made as a firewall makes them, a field or a text with a check box
// of negation before the value, Add puts it into the rules; the levels and the times. OK takes what is typed and not
// added as well; a wrong time, a field the journal does not take or a broken expression keep the window open. Simple...
// goes back to the simple filter with the rules as they are, what is typed and not added is left

class FilterDialog : public TDialog
{
  public:

    FilterDialog(const JournalFilter& filter, int start);

    void handleEvent(TEvent& event) override;
    Boolean valid(ushort command) override;
    void apply(JournalFilter& filter) const;  // The filter as the window has left it, its journals untouched

  private:

    struct Rule
    {
      int field;         // A field of Source, JOURNAL_FIELD_COUNT for PID, -1 for a raw match, -2 for a text
      bool exclude;
      std::string value; // Of a field of Source or of PID, FIELD=VALUE or FIELD!=VALUE of a raw match
      JournalText text;
    };

    JournalFilter journals;  // Where the lists come from
    std::vector<Rule> rules;
    std::vector<std::string> items;
    TInputLine* name;
    TCheckBoxes* negation;
    TInputLine* value;
    TInputLine* line;
    TCheckBoxes* modes;     // Of the text: not, Regex, Case
    RuleList* list;
    TCheckBoxes* levels;
    TInputLine* since;
    TInputLine* until;

    void update();
    bool addField(bool quiet);
    bool addText(bool quiet);
    void remove();
    void edit();
    void clear();
    void pickField();
    void pickValue();
    void pickTime();
    void fail(const char* text);
    std::string describe(const Rule& rule) const;
};

#endif
