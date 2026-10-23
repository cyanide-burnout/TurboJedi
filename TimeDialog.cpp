#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include "TimeDialog.h"
#include "TimePickDialog.h"
#include "Commands.h"

#include <string.h>

// TimeDialog

TimeDialog::TimeDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 58, 16), "Time")
{
  options |= ofCentered;

  lines[0] = new TInputLine(TRect(12, 2, 43, 3), TIME_LENGTH);
  insert(lines[0]);
  insert(new TLabel(TRect(2, 2, 12, 3), "~S~ince", lines[0]));
  insert(new TButton(TRect(44, 2, 56, 4), "Pick...", cmJournalTimePick, bfNormal));

  lines[1] = new TInputLine(TRect(12, 5, 43, 6), TIME_LENGTH);
  insert(lines[1]);
  insert(new TLabel(TRect(2, 5, 12, 6), "~U~ntil", lines[1]));
  insert(new TButton(TRect(44, 5, 56, 7), "Pick...", cmJournalTimePick + 1, bfNormal));

  insert(new TStaticText(TRect(3, 8, 55, 11),
    "2026-10-23 19:00, 19:00, now, today, yesterday,\n"
    "-1h, -30m, -2d. Empty Since starts with the last\n"
    "entries, empty Until follows the journal"));
  insert(new TButton(TRect(14, 12, 26, 14), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(30, 12, 42, 14), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

void TimeDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

  if ((event.what == evCommand) &&
      (event.message.command >= cmJournalTimePick) &&
      (event.message.command <= (cmJournalTimePick + 1)))
  {
    pick(event.message.command - cmJournalTimePick);
    clearEvent(event);
  }
}

void TimeDialog::pick(int index)
{
  char value[TIME_LENGTH];
  TimePickDialog* dialog;

  dialog = new TimePickDialog((index == 0) ? "Since" : "Until", lines[index]->data);

  if (TProgram::deskTop->execView(dialog) == cmOK)
  {
    memset(value, 0, sizeof(value));
    strncpy(value, dialog->result.c_str(), sizeof(value) - 1);
    lines[index]->setData(value);

    // A focused button takes Enter over, back on the field Enter means OK
    lines[index]->select();
  }

  TObject::destroy(dialog);
}
