#ifndef CHAINDIALOG_H
#define CHAINDIALOG_H

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

// The links of a chain in the order given, as the title writes them

class ChainList : public TListViewer
{
  public:

    ChainList(const TRect& bounds, TScrollBar* bar, const std::vector<std::string>& items);

    void getText(char* dest, short item, short limit) override;

  private:

    const std::vector<std::string>& items;
};

// The chain of a filter, its matches and texts, to look at and to change. Show adds the text of the line to show the
// entries with it, Hide to leave them out, either applies the chain then; with an empty line it applies what Remove has
// done. Remove takes the link under the cursor out. A broken expression keeps the dialog open

class ChainDialog : public TDialog
{
  public:

    ChainDialog(const JournalFilter& filter);

    void handleEvent(TEvent& event) override;
    void apply(JournalFilter& filter) const;  // The matches and the texts as the dialog has left them

  private:

    std::vector<std::string> matches;
    std::vector<JournalText> texts;
    std::vector<std::string> items;
    TInputLine* line;
    TCheckBoxes* boxes;
    ChainList* list;

    void update();
    bool add(bool exclude);
};

#endif
