#define Uses_TLabel
#define Uses_TButton
#define Uses_TFilterValidator
#define Uses_TEvent
#define Uses_MsgBox
#include "TimePickDialog.h"
#include "JournalTools.h"
#include "Commands.h"

#include <time.h>
#include <string.h>

// TimePickDialog

TimePickDialog::TimePickDialog(const char* title, const char* value) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 57, 20), title)
{
  static const char* const labels[] = { "~N~ow", "-15m", "-1~h~", "~T~oday", "~Y~esterday" };
  static const short spans[][2] = { { 2, 11 }, { 12, 21 }, { 22, 31 }, { 32, 42 }, { 43, 55 } };

  int index;
  time_t second;
  uint64_t stamp;
  char text[16];
  struct tm date;

  if (!ParseJournalTime(value, &stamp))
  {
    // Nothing to start with, so now
    stamp = time(nullptr) * 1000000ULL;
  }

  second = stamp / 1000000;
  localtime_r(&second, &date);
  strftime(text, sizeof(text), "%H:%M:%S", &date);

  options |= ofCentered;
  calendar = new CalendarView(TRect(17, 2, 39, 10), date);
  clock    = new TInputLine(TRect(24, 11, 34, 12), 9, new TFilterValidator("0123456789:"));

  insert(calendar);
  insert(clock);
  insert(new TLabel(TRect(17, 11, 24, 12), "T~i~me", clock));

  for (index = 0; index < 5; index ++)
  {
    // Relative ones count from each opening of the window
    insert(new TButton(TRect(spans[index][0], 13, spans[index][1], 15), labels[index], cmJournalTimePreset + index, bfNormal));
  }

  insert(new TButton(TRect(16, 16, 27, 18), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(30, 16, 41, 18), "Cancel", cmCancel, bfNormal));

  clock->setData(text);
  calendar->select();
}

void TimePickDialog::handleEvent(TEvent& event)
{
  static const char* const presets[] = { "now", "-15m", "-1h", "today", "yesterday" };

  char text[32];
  struct tm moment;
  const char* rest;

  if ((event.what == evCommand) &&
      (event.message.command >= cmJournalTimePreset) &&
      (event.message.command <  (cmJournalTimePreset + 5)))
  {
    result = presets[event.message.command - cmJournalTimePreset];
    endModal(cmOK);
    clearEvent(event);
    return;
  }

  if ((event.what == evCommand) &&
      (event.message.command == cmOK))
  {
    moment        = calendar->date;
    moment.tm_sec = 0;

    if ((((rest = strptime(clock->data, "%H:%M:%S", &moment)) == nullptr) || (*rest != '\0')) &&
        (((rest = strptime(clock->data, "%H:%M",    &moment)) == nullptr) || (*rest != '\0')))
    {
      // Stay in the picker
      messageBox("Unknown time format", mfError | mfOKButton);
      clearEvent(event);
      return;
    }

    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &moment);
    result = text;
  }

  TDialog::handleEvent(event);
}
