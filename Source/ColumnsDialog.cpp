// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TSItem
#define Uses_TLabel
#define Uses_TButton
#include "ColumnsDialog.h"

// Columns take bits 0 to 6, UTC, milliseconds, wrapping and the hidden heading the next four
#define COLUMN_MASK    0x7f
#define SETTING_SHIFT  7
#define SETTING_MASK   0x0f

// ColumnsDialog

ColumnsDialog::ColumnsDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 54, 15), "Columns")
{
  options |= ofCentered;
  columns  = new TCheckBoxes(TRect(3, 3, 25, 10),
    new TSItem("~D~ate",
    new TSItem("~T~ime",
    new TSItem("~L~evel",
    new TSItem("~H~ost",
    new TSItem("~U~nit",
    new TSItem("~I~dentifier",
    new TSItem("~P~ID", nullptr))))))));
  settings = new TCheckBoxes(TRect(28, 3, 51, 7),
    new TSItem("UT~C~",
    new TSItem("~M~illiseconds",
    new TSItem("~W~rap lines",
    new TSItem("Hide headin~g~", nullptr)))));

  insert(columns);
  insert(new TLabel(TRect(2, 2, 20, 3), "~S~how columns", columns));
  insert(settings);
  insert(new TLabel(TRect(27, 2, 45, 3), "~O~ptions", settings));
  insert(new TButton(TRect(13, 11, 25, 13), "OK", cmOK, bfDefault));
  insert(new TButton(TRect(28, 11, 40, 13), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

void ColumnsDialog::getData(void* record)
{
  ushort shown;
  ushort chosen;

  columns->getData(&shown);
  settings->getData(&chosen);

  // The message is always there
  *static_cast<ushort*>(record) = (shown & COLUMN_MASK) | ((chosen & SETTING_MASK) << SETTING_SHIFT);
}

void ColumnsDialog::setData(void* record)
{
  ushort shown;
  ushort chosen;

  shown  = *static_cast<ushort*>(record) & COLUMN_MASK;
  chosen = (*static_cast<ushort*>(record) >> SETTING_SHIFT) & SETTING_MASK;

  columns->setData(&shown);
  settings->setData(&chosen);
}
