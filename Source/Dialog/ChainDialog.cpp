// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TSItem
#define Uses_TEvent
#define Uses_MsgBox
#include "ChainDialog.h"
#include "TextDialog.h"
#include "Commands.h"
#include "JournalMatcher.h"

#include <string.h>

// ChainList

ChainList::ChainList(const TRect& bounds, TScrollBar* bar, const std::vector<std::string>& items) :
  TListViewer(bounds, 1, nullptr, bar),
  items(items)
{
}

void ChainList::getText(char* dest, short item, short limit)
{
  if ((item < 0) ||
      ((size_t)item >= items.size()))
  {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, items[item].c_str(), limit - 1);
  dest[limit - 1] = '\0';
}

// ChainDialog

ChainDialog::ChainDialog(const JournalFilter& filter) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 64, 19), "Text")
{
  TScrollBar* bar;

  matches  = filter.matches;
  texts    = filter.texts;
  options |= ofCentered;
  line     = new TInputLine(TRect(3, 3, 61, 4), TEXT_LENGTH);
  boxes    = new TCheckBoxes(TRect(3, 5, 61, 6),
    new TSItem("~R~egular expression",
    new TSItem("~C~ase sensitive", nullptr)));
  bar      = new TScrollBar(TRect(60, 9, 61, 15));
  list     = new ChainList(TRect(3, 9, 60, 15), bar, items);

  insert(line);
  insert(new TLabel(TRect(2, 2, 61, 3), "~T~ext of the entries", line));
  insert(boxes);
  insert(bar);
  insert(list);
  insert(new TLabel(TRect(2, 8, 61, 9), "Ch~a~in of the window, an entry has to pass all of it", list));
  insert(new TButton(TRect(3,  16, 15, 18), "~S~how",   cmChainShow,   bfDefault));
  insert(new TButton(TRect(16, 16, 28, 18), "~H~ide",   cmChainHide,   bfNormal));
  insert(new TButton(TRect(29, 16, 41, 18), "Re~m~ove", cmChainRemove, bfNormal));
  insert(new TButton(TRect(49, 16, 61, 18), "Cancel",   cmCancel,      bfNormal));

  update();
  line->select();
}

void ChainDialog::handleEvent(TEvent& event)
{
  short focused;

  if ((event.what == evCommand) &&
      ((event.message.command == cmChainShow) || (event.message.command == cmChainHide)))
  {
    if (add(event.message.command == cmChainHide))
    {
      endModal(cmOK);
    }

    clearEvent(event);
    return;
  }

  if ((event.what == evCommand) &&
      (event.message.command == cmChainRemove))
  {
    focused = list->focused;

    if ((focused >= 0) &&
        ((size_t)focused < items.size()))
    {
      // The matches come first in the list, the texts after them
      if ((size_t)focused < matches.size())
      {
        matches.erase(matches.begin() + focused);
      }
      else
      {
        texts.erase(texts.begin() + (focused - matches.size()));
      }

      update();
      list->focusItem(std::min<short>(focused, std::max<short>(0, (short)items.size() - 1)));
    }

    clearEvent(event);
    return;
  }

  TDialog::handleEvent(event);
}

void ChainDialog::apply(JournalFilter& filter) const
{
  filter.matches = matches;
  filter.texts   = texts;
}

void ChainDialog::update()
{
  items.clear();

  for (const std::string& match : matches)
  {
    items.push_back(match);
  }

  for (const JournalText& text : texts)
  {
    items.push_back(FormatJournalText(text));
  }

  list->setRange(items.size());
  list->drawView();
}

bool ChainDialog::add(bool exclude)
{
  ushort value;
  JournalText text;
  JournalMatcher probe;
  char data[TEXT_LENGTH];

  line->getData(data);
  boxes->getData(&value);

  if (data[0] == '\0')
  {
    // Nothing to add, the chain as it is
    return true;
  }

  text.text = data;
  text.mode =
    ((value & 1) ? JOURNAL_TEXT_EXPRESSION : 0) |
    ((value & 2) ? JOURNAL_TEXT_SENSITIVE  : 0) |
    (exclude     ? JOURNAL_TEXT_EXCLUDE    : 0);

  if (!probe.set(text.text, text.mode))
  {
    // Stays open with what has been typed
    messageBox("Broken regular expression", mfError | mfOKButton);
    return false;
  }

  texts.push_back(text);
  return true;
}
