// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TSItem
#include "TimeDialog.h"
#include "TimePickDialog.h"
#include "JournalTools.h"
#include "Commands.h"

#include <time.h>
#include <string.h>

// TimeDialog

TimeDialog::TimeDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 58, 18), "Time")
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

  zone = new TCheckBoxes(TRect(12, 7, 21, 8), new TSItem("U~T~C", nullptr));
  told = new TParamText(TRect(22, 7, 56, 8));
  insert(zone);
  insert(told);

  insert(new TStaticText(TRect(3, 9, 55, 13),
    "2026-10-23 19:00, 19:00, now, today, yesterday,\n"
    "-1h, -30m, -2d. Empty Since starts with the last\n"
    "entries, empty Until follows the journal; UTC\n"
    "turns the dates and times over to it and back"));
  insert(new TButton(TRect(14, 14, 26, 16), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(30, 14, 42, 16), "Cancel", cmCancel, bfNormal));
  selectNext(False);

  utc = false;
  tell();
}

void TimeDialog::setData(void* record)
{
  TDialog::setData(record);
  utc = zone->mark(0);
  tell();
}

Boolean TimeDialog::valid(ushort command)
{
  int index;
  std::string text;
  char value[TIME_LENGTH];

  if (command == cmOK)
  {
    for (index = 0; index < 2; index ++)
    {
      // A date or a day typed with UTC checked is one of UTC, the field says so
      if (!IsJournalTimeUniversal(lines[index]->data) &&
          ConvertJournalTime(lines[index]->data, utc, utc, text))
      {
        memset(value, 0, sizeof(value));
        strncpy(value, text.c_str(), sizeof(value) - 1);
        lines[index]->setData(value);
      }
    }
  }

  return TDialog::valid(command);
}

void TimeDialog::convert()
{
  int index;
  bool now;
  std::string text;
  char value[TIME_LENGTH];

  now = zone->mark(0);

  for (index = 0; index < 2; index ++)
  {
    if ((after[index] == lines[index]->data) &&
        (!before[index].empty()))
    {
      // Turned back with nothing typed meanwhile: today stays today, not the date it has become
      text = before[index];
    }
    else if (!ConvertJournalTime(lines[index]->data, utc, now, text))
    {
      // What is no time stays as typed, OK tells about it
      text = lines[index]->data;
    }

    before[index] = lines[index]->data;
    after[index]  = text;
    memset(value, 0, sizeof(value));
    strncpy(value, text.c_str(), sizeof(value) - 1);
    lines[index]->setData(value);
  }

  utc = now;
  tell();
}

void TimeDialog::tell()
{
  time_t now;
  struct tm date;

  if (utc)
  {
    told->setText("dates and times of UTC");
    return;
  }

  now = time(nullptr);
  localtime_r(&now, &date);
  told->setText("local time, %s, UTC%+03ld:%02ld", date.tm_zone, date.tm_gmtoff / 3600, labs(date.tm_gmtoff / 60) % 60);
}

void TimeDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

  if (zone->mark(0) != utc)
  {
    // The check box has been turned: the fields go over to the other zone
    convert();
  }

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
  std::string text;
  char value[TIME_LENGTH];
  TimePickDialog* dialog;

  // The picker starts from the time of the field in the zone of the dialog
  ConvertJournalTime(lines[index]->data, utc, utc, text);
  dialog = new TimePickDialog((index == 0) ? "Since" : "Until", text.c_str(), utc);

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
