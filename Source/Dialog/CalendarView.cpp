// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include "CalendarView.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

// CalendarView

static int GetMonthLength(const struct tm& date)
{
  struct tm last;

  // Day zero of the next month is the last one of this month
  memset(&last, 0, sizeof(last));
  last.tm_year  = date.tm_year;
  last.tm_mon   = date.tm_mon + 1;
  last.tm_mday  = 0;
  last.tm_hour  = 12;
  last.tm_isdst = -1;
  mktime(&last);

  return last.tm_mday;
}

static int GetMonthOffset(const struct tm& date)
{
  struct tm first;

  // Columns start with Monday
  memset(&first, 0, sizeof(first));
  first.tm_year  = date.tm_year;
  first.tm_mon   = date.tm_mon;
  first.tm_mday  = 1;
  first.tm_hour  = 12;
  first.tm_isdst = -1;
  mktime(&first);

  return (first.tm_wday + 6) % 7;
}

CalendarView::CalendarView(const TRect& bounds, const struct tm& date) :
  TView(bounds)
{
  this->date = date;
  options   |= ofSelectable | ofFirstClick;
  move(0, 0);
}

void CalendarView::draw()
{
  static const char* const months[] =
  {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
  };

  int row;
  int day;
  int column;
  int offset;
  int length;
  time_t now;
  char text[32];
  struct tm today;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr current;
  TColorAttr chosen;

  // Dialog colors, today in blue, the chosen day like a focused item of a list
  normal  = TColorAttr(TColorBIOS(0x0), TColorBIOS(0x7));
  current = TColorAttr(TColorBIOS(0x1), TColorBIOS(0x7));
  chosen  = (state & sfFocused) ? TColorAttr(TColorBIOS(0xf), TColorBIOS(0x2)) : TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));

  now = time(nullptr);
  localtime_r(&now, &today);

  offset = GetMonthOffset(date);
  length = GetMonthLength(date);

  for (row = 0; row < size.y; row ++)
  {
    buffer.moveChar(0, ' ', normal, size.x);

    if (row == 0)
    {
      // Month with the arrows that turn it
      snprintf(text, sizeof(text), "%s %d", months[date.tm_mon], date.tm_year + 1900);
      buffer.moveStr(1, "\xe2\x97\x84", normal);
      buffer.moveStr((size.x - strlen(text)) / 2, text, normal);
      buffer.moveStr(size.x - 2, "\xe2\x96\xba", normal);
    }

    if (row == 1)
    {
      // Weeks start with Monday
      buffer.moveStr(1, "Mo Tu We Th Fr Sa Su", normal);
    }

    for (column = 0; (row >= 2) && (column < 7); column ++)
    {
      day = (row - 2) * 7 + column - offset + 1;

      if ((day >= 1) &&
          (day <= length))
      {
        snprintf(text, sizeof(text), "%2d", day);
        buffer.moveStr(1 + column * 3, text,
          (day == date.tm_mday) ? chosen :
          (((day == today.tm_mday) && (date.tm_mon == today.tm_mon) && (date.tm_year == today.tm_year)) ? current : normal));
      }
    }

    writeLine(0, row, size.x, 1, buffer);
  }
}

void CalendarView::handleEvent(TEvent& event)
{
  int day;
  int column;
  TEvent command;
  TPoint point;

  TView::handleEvent(event);

  if (event.what == evKeyDown)
  {
    switch (event.keyDown.keyCode)
    {
      case kbLeft:  move(-1,  0);  break;
      case kbRight: move( 1,  0);  break;
      case kbUp:    move(-7,  0);  break;
      case kbDown:  move( 7,  0);  break;
      case kbPgUp:  move( 0, -1);  break;
      case kbPgDn:  move( 0,  1);  break;
      default:                     return;
    }

    clearEvent(event);
  }

  if (event.what == evMouseDown)
  {
    point = makeLocal(event.mouse.where);

    if ((point.y == 0) && (point.x <= 2))           move(0, -1);
    if ((point.y == 0) && (point.x >= size.x - 3))  move(0,  1);

    column = (point.x - 1) / 3;
    day    = (point.y - 2) * 7 + column - GetMonthOffset(date) + 1;

    if ((point.y >= 2) &&
        (point.x >= 1) &&
        (column < 7) &&
        (day >= 1) &&
        (day <= GetMonthLength(date)))
    {
      date.tm_mday = day;
      move(0, 0);

      if (event.mouse.eventFlags & meDoubleClick)
      {
        // Double click on a day is OK
        command.what            = evCommand;
        command.message.command = cmOK;
        command.message.infoPtr = nullptr;
        putEvent(command);
      }
    }

    clearEvent(event);
  }
}

void CalendarView::move(int days, int months)
{
  int day;

  if (months != 0)
  {
    // Turning a month keeps the day within the new one
    day          = date.tm_mday;
    date.tm_mday = 1;
    date.tm_mon += months;
    date.tm_hour = 12;
    mktime(&date);
    date.tm_mday = std::min(day, GetMonthLength(date));
  }

  date.tm_mday  += days;
  date.tm_hour   = 12;
  date.tm_min    = 0;
  date.tm_sec    = 0;
  date.tm_isdst  = -1;
  mktime(&date);
  drawView();
}
