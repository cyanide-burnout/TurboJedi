// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TSItem
#define Uses_TLabel
#define Uses_TButton
#define Uses_MsgBox
#include "TextDialog.h"
#include "JournalMatcher.h"

#include <string.h>

// TextDialog

TextDialog::TextDialog(const char* title, const char* label, bool exclude) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 52, exclude ? 13 : 12), title)
{
  int y;

  this->exclude = exclude;
  options      |= ofCentered;
  y             = exclude ? 9 : 8;
  line          = new TInputLine(TRect(3, 3, 49, 4), TEXT_LENGTH);

  if (exclude)
  {
    boxes = new TCheckBoxes(TRect(3, 5, 49, 8),
      new TSItem("~R~egular expression",
      new TSItem("~E~xclude matching entries",
      new TSItem("~C~ase sensitive", nullptr))));
  }
  else
  {
    boxes = new TCheckBoxes(TRect(3, 5, 49, 7),
      new TSItem("~R~egular expression",
      new TSItem("~C~ase sensitive", nullptr)));
  }

  insert(line);
  insert(new TLabel(TRect(2, 2, 49, 3), label, line));
  insert(boxes);
  insert(new TButton(TRect(13, y, 25, y + 2), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(27, y, 39, y + 2), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

void TextDialog::getData(void* record)
{
  ushort value;
  TextData* data;

  data = static_cast<TextData*>(record);
  line->getData(data->text);
  boxes->getData(&value);

  // Bit N of three boxes is the JOURNAL_TEXT_* bit N, two boxes leave Exclude out
  data->options = exclude ? value : (((value & 1) ? JOURNAL_TEXT_EXPRESSION : 0) | ((value & 2) ? JOURNAL_TEXT_SENSITIVE : 0));
}

void TextDialog::setData(void* record)
{
  ushort value;
  TextData* data;

  data  = static_cast<TextData*>(record);
  value = exclude ? data->options : (((data->options & JOURNAL_TEXT_EXPRESSION) ? 1 : 0) | ((data->options & JOURNAL_TEXT_SENSITIVE) ? 2 : 0));

  line->setData(data->text);
  boxes->setData(&value);
}

Boolean TextDialog::valid(ushort command)
{
  TextData data;
  JournalMatcher probe;

  if (command != cmOK)
  {
    // Cancel goes as it is
    return TDialog::valid(command);
  }

  getData(&data);

  if (!probe.set(data.text, data.options))
  {
    // Stays open with what has been typed
    messageBox("Broken regular expression", mfError | mfOKButton);
    return False;
  }

  return TDialog::valid(command);
}
