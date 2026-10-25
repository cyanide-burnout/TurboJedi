// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#include "TimelineView.h"
#include "JournalView.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <iterator>
#include <algorithm>

#define READ_STEP       20000   // Entries counted at once, the rest comes with the next polls
#define INTERVAL_LIMIT  100000  // Columns the finest step may take, the reading stops there
#define LABEL_SPACING   12      // Columns between the times under the graph

#define GROUP_ERROR    0
#define GROUP_WARNING  1
#define GROUP_REST     2
#define GROUP_NONE     3

// Steps to choose from, in seconds
static const uint64_t steps[] =
{
  1, 2, 5, 10, 15, 30,
  60, 120, 300, 600, 900, 1800,
  3600, 7200, 10800, 21600, 43200,
  86400, 172800, 604800
};

// Eighths of a cell filled from the bottom
static const char* const blocks[] =
{
  " ", "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84", "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"
};

// Helpers

static uint64_t AlignTime(uint64_t realtime, uint64_t step)
{
  time_t second;
  struct tm date;
  int64_t offset;
  int64_t local;

  // Hours and days start where the clock on the wall says
  second = realtime / 1000000;
  localtime_r(&second, &date);
  offset = date.tm_gmtoff * 1000000LL;
  local  = (int64_t)realtime + offset;

  return local - (local % (int64_t)step) - offset;
}

static std::string FormatStep(uint64_t step)
{
  char text[16];

  step /= 1000000;

  if (step % 86400 == 0) snprintf(text, sizeof(text), "%llud", (unsigned long long)(step / 86400));
  else if (step % 3600 == 0) snprintf(text, sizeof(text), "%lluh", (unsigned long long)(step / 3600));
  else if (step % 60 == 0) snprintf(text, sizeof(text), "%llum", (unsigned long long)(step / 60));
  else snprintf(text, sizeof(text), "%llus", (unsigned long long)step);

  return text;
}

// TimelineView

TimelineView::TimelineView(const TRect& bounds, TScrollBar* horizontal, const JournalFilter& filter, int seconds, bool logarithmic) :
  TScroller(bounds, horizontal, nullptr)
{
  this->filter      = filter;
  this->logarithmic = logarithmic;

  growMode    = gfGrowHiX | gfGrowHiY;
  options    |= ofFirstClick;
  journal     = nullptr;
  since       = 0;
  until       = 0;
  start       = 0;
  step        = 0;
  chosen      = seconds * 1000000ULL;
  end         = 0;
  done        = 0;
  closed      = false;
  pending     = false;
  follow      = true;
  cursor      = 0;

  open();
}

TimelineView::~TimelineView()
{
  if (journal != nullptr)
  {
    // Window has been closed
    sd_journal_close(journal);
  }
}

void TimelineView::draw()
{
  static const uint8_t colors[] = { 0x0c, 0x0e, 0x07 };  // Errors light red, warnings yellow, the rest light gray

  int x;
  int y;
  int row;
  int room;
  int part;
  int lower;
  int upper;
  int levels[3];
  size_t index;
  uint64_t peak;
  uint64_t realtime;
  char text[160];
  std::string line;
  std::string time;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr heading;
  TColorAttr marked;
  TColorAttr stamp;
  TColorAttr color;
  std::vector<std::array<int, 3>> columns;

  normal  = mapColor(1);
  heading = TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));
  marked  = normal;
  stamp   = normal;
  room    = std::max(size.y - 2, 0);
  peak    = 0;
  marked.setBackground(TColorBIOS(0x3));
  stamp.setForeground(TColorBIOS(0x3));

  for (const Interval& interval : intervals)
  {
    // The tallest column of the whole range, so scrolling keeps the scale
    peak = std::max(peak, interval.counts[0] + interval.counts[1] + interval.counts[2]);
  }

  // Heading tells about the interval under the cursor and the state of the reading
  realtime = start + cursor * step;
  line     = " " + label(realtime, true) + " +" + FormatStep(step);

  if (cursor < intervals.size())
  {
    snprintf(text, sizeof(text), "  err %llu  warn %llu  other %llu",
      (unsigned long long)intervals[cursor].counts[GROUP_ERROR],
      (unsigned long long)intervals[cursor].counts[GROUP_WARNING],
      (unsigned long long)intervals[cursor].counts[GROUP_REST]);
    line += text;
  }

  line += logarithmic ? " \xe2\x94\x82 log" : " \xe2\x94\x82 linear";

  if (pending)
  {
    snprintf(text, sizeof(text), " \xe2\x94\x82 LOADING %d%%", (int)std::min<uint64_t>(99, (done > start) ? ((done - start) * 100 / std::max<uint64_t>(end - start, 1)) : 0));
    line += text;
  }
  else
  {
    line += closed ? " \xe2\x94\x82 STOPPED" : (follow ? " \xe2\x94\x82 FOLLOW" : "");
  }

  line += error.empty() ? std::string() : (" \xe2\x94\x82 " + error);

  buffer.moveChar(0, ' ', heading, size.x);
  buffer.moveStr(0, line, heading, size.x);
  writeLine(0, 0, size.x, 1, buffer);

  for (x = 0; x < size.x; x ++)
  {
    // Boundaries of the three parts of every column on the screen, in eighths of a cell
    index = delta.x + x;
    levels[0] = levels[1] = levels[2] = 0;

    if (index < intervals.size())
    {
      heights(intervals[index], peak, room, levels);
    }

    columns.push_back({ levels[0], levels[1], levels[2] });
  }

  for (y = 0; y < room; y ++)
  {
    // Rows go up from the bottom
    row = room - 1 - y;
    buffer.moveChar(0, ' ', normal, size.x);

    for (x = 0; x < size.x; x ++)
    {
      // A cell shows at most two parts: the lower one by the block, the one above it by the background
      lower = std::upper_bound(columns[x].begin(), columns[x].end(), row * 8) - columns[x].begin();
      color = ((size_t)(delta.x + x) == cursor) ? marked : normal;

      if (lower == GROUP_NONE)
      {
        // Above the column
        buffer.moveChar(x, ' ', color, 1);
        continue;
      }

      part  = std::min(columns[x][lower] - row * 8, 8);
      upper = (part < 8) ? (std::upper_bound(columns[x].begin(), columns[x].end(), row * 8 + part) - columns[x].begin()) : GROUP_NONE;

      color.setForeground(TColorBIOS(colors[lower]));

      if (part == 8)
      {
        // A full cell by its background, a full block leaves the gaps of a font with taller rows than its glyphs
        color.setBackground(TColorBIOS(colors[lower]));
        buffer.moveChar(x, ' ', color, 1);
        continue;
      }

      if (upper != GROUP_NONE)
      {
        // The next part goes on in the same cell
        color.setBackground(TColorBIOS(colors[upper]));
      }

      buffer.moveStr(x, blocks[part], color);
    }

    writeLine(0, y + 1, size.x, 1, buffer);
  }

  if (size.y > 1)
  {
    // Times under the graph at the same intervals whatever the scrolling, the cursor marked below its column
    buffer.moveChar(0, ' ', normal, size.x);

    for (x = 0; x < size.x; x ++)
    {
      if (((delta.x + x) % LABEL_SPACING) == 0)
      {
        time = label(start + (delta.x + x) * step, false);
        buffer.moveStr(x, time, stamp, std::min<int>(time.size(), size.x - x));
      }
    }

    if ((cursor >= (size_t)delta.x) &&
        (cursor < (size_t)(delta.x + size.x)))
    {
      // A time there stays readable in the color of the cursor, a blank cell gets a mark
      x     = cursor - delta.x;
      color = TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));

      if ((cursor % LABEL_SPACING) >= label(start, false).size())
      {
        buffer.moveStr(x, "\xe2\x96\xb2", color);
      }

      buffer.putAttribute(x, color);
    }

    writeLine(0, size.y - 1, size.x, 1, buffer);
  }
}

void TimelineView::handleEvent(TEvent& event)
{
  if (event.what == evKeyDown)
  {
    switch (event.keyDown.keyCode)
    {
      case kbLeft:   move((int64_t)cursor - 1);                      break;
      case kbRight:  move((int64_t)cursor + 1);                      break;
      case kbPgUp:   move((int64_t)cursor - size.x);                 break;
      case kbPgDn:   move((int64_t)cursor + size.x);                 break;
      case kbHome:   move(0);                                        break;
      case kbEnd:    move((int64_t)intervals.size() - 1);            break;
      case kbEnter:  synchronize();                                  break;

      default:
        switch (event.keyDown.charScan.charCode)
        {
          case '+':
          case '=':
            // Finer, the reading starts over
            zoom(-1);
            break;

          case '-':
            zoom(1);
            break;

          case 'l':
          case 'L':
            // Spikes of errors stay visible next to a steady flow of everything else
            logarithmic = !logarithmic;
            drawView();
            break;

          default:
            TScroller::handleEvent(event);
            return;
        }
    }

    clearEvent(event);
    return;
  }

  TScroller::handleEvent(event);

  if ((event.what == evMouseDown) &&
      (event.mouse.buttons & mbLeftButton))
  {
    if (event.mouse.eventFlags & meDoubleClick)
    {
      // Double click takes the windows to the interval under the mouse
      move(delta.x + makeLocal(event.mouse.where).x);
      synchronize();
      clearEvent(event);
      return;
    }

    do
    {
      // Dragging past an edge scrolls
      move(delta.x + makeLocal(event.mouse.where).x);
    }
    while (mouseEvent(event, evMouseMove | evMouseAuto));

    clearEvent(event);
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalPoll) &&
      (journal != nullptr) &&
      (!closed) &&
      (pending || (sd_journal_process(journal) != SD_JOURNAL_NOP)))
  {
    // Never clear the poll, every window has to get it
    read();
    refresh();
  }
}

void TimelineView::changeBounds(const TRect& bounds)
{
  TScroller::changeBounds(bounds);
  refresh();
}

const JournalFilter& TimelineView::getFilter()
{
  return filter;
}

int TimelineView::getStep()
{
  return chosen / 1000000;
}

bool TimelineView::getLogarithmic()
{
  return logarithmic;
}

void TimelineView::open()
{
  int result;
  char message[128];

  if (!matcher.set(filter.text, filter.mode))
  {
    // A layout file may bring a broken expression, it goes as plain text then
    filter.mode &= ~JOURNAL_TEXT_EXPRESSION;
    matcher.set(filter.text, filter.mode);
  }

  if ((result = OpenJournalQuery(&journal, filter)) < 0)
  {
    journal = nullptr;
    snprintf(message, sizeof(message), "Error opening the journal: %s", strerror(-result));
    error = message;
    return;
  }

  // Sets up change notification for sd_journal_process()
  sd_journal_get_fd(journal);

  if (!filter.until.empty())
  {
    // Validated by the dialog and the command line
    ParseJournalTime(filter.until, &until);
  }

  if (!filter.since.empty())
  {
    ParseJournalTime(filter.since, &since);
  }

  begin(chosen);
}

void TimelineView::begin(uint64_t fixed)
{
  size_t index;
  uint64_t first;
  uint64_t last;
  uint64_t width;

  intervals.clear();
  closed  = false;
  pending = false;
  error.clear();

  // The range goes from the oldest entry the matches give to the newest one, or to the edges of the time range
  if (since != 0) sd_journal_seek_realtime_usec(journal, since);
  else            sd_journal_seek_head(journal);

  if ((sd_journal_next(journal) <= 0) ||
      (sd_journal_get_realtime_usec(journal, &first) < 0))
  {
    // Nothing yet, entries to come start the graph
    first = (since != 0) ? since : (time(nullptr) * 1000000ULL);
  }

  if (until != 0) sd_journal_seek_realtime_usec(journal, until + 1);
  else            sd_journal_seek_tail(journal);

  if ((sd_journal_previous(journal) <= 0) ||
      (sd_journal_get_realtime_usec(journal, &last) < 0) ||
      (last < first))
  {
    last = first;
  }

  // The finest step that fits the range into the window, a chosen one otherwise
  width = std::max(size.x, 16);
  step  = fixed;

  for (index = 0; (fixed == 0) && (index < std::size(steps)); index ++)
  {
    step = steps[index] * 1000000ULL;

    if (((last - AlignTime(first, step)) / step) < width)
    {
      break;
    }
  }

  start = AlignTime(first, step);
  end   = last;
  done  = start;

  // Counting starts over from the beginning of the range
  if (since != 0) sd_journal_seek_realtime_usec(journal, since);
  else            sd_journal_seek_head(journal);

  read();
  refresh();
}

void TimelineView::read()
{
  int count;
  int priority;
  size_t length;
  uint64_t realtime;
  const char* data;
  char value[JOURNAL_VALUE_LENGTH];
  std::string identifier;
  std::string pid;

  for (count = 0; (!closed) && (count < READ_STEP) && (sd_journal_next(journal) > 0); count ++)
  {
    if (sd_journal_get_realtime_usec(journal, &realtime) < 0)
    {
      // A journal window leaves it out as well
      continue;
    }

    if ((until != 0) &&
        (realtime > until))
    {
      // Range is over, later entries never belong to it
      closed = true;
      break;
    }

    priority = LOG_INFO;

    if (GetJournalField(journal, "PRIORITY", value, sizeof(value)) &&
        (value[0] >= '0') &&
        (value[0] <= '7') &&
        (value[1] == '\0'))
    {
      // Anything else falls back to informational
      priority = value[0] - '0';
    }

    if (!matcher.empty())
    {
      // The text filter looks at the syslog style source as well, the way a journal window applies it
      identifier = (GetJournalField(journal, "SYSLOG_IDENTIFIER", value, sizeof(value)) || GetJournalField(journal, "_COMM", value, sizeof(value))) ? value : "?";
      pid        = GetJournalField(journal, "_PID", value, sizeof(value)) ? value : "";

      if ((sd_journal_get_data(journal, "MESSAGE", (const void**)&data, &length) < 0) ||
          (length < 8) ||
          (matcher.match(identifier + "[" + pid + "]: " + std::string(data + 8, length - 8)) == ((filter.mode & JOURNAL_TEXT_EXCLUDE) != 0)))
      {
        continue;
      }
    }

    if (!add(realtime, priority))
    {
      // Too many columns for this step
      error   = "the step is too fine, zoom out with -";
      closed  = true;
      break;
    }
  }

  // A long range comes in batches, the screen keeps going between them
  pending = (count == READ_STEP);
}

void TimelineView::refresh()
{
  setLimit(intervals.size(), size.y);

  if (follow &&
      (!intervals.empty()))
  {
    // The newest interval
    cursor = intervals.size() - 1;
  }

  reveal();
  drawView();
}

bool TimelineView::add(uint64_t realtime, int priority)
{
  size_t index;
  Interval* interval;

  // Entries of merged journals may come a bit out of order around the start
  index = (realtime > start) ? ((realtime - start) / step) : 0;

  if (index >= INTERVAL_LIMIT)
  {
    return false;
  }

  if (index >= intervals.size())
  {
    // Empty intervals in between
    intervals.resize(index + 1, { { 0, 0, 0 }, 0 });
  }

  interval = &intervals[index];

  if (interval->first == 0)
  {
    // Windows go to the first entry of the interval
    interval->first = realtime;
  }

  interval->counts[(priority <= LOG_ERR) ? GROUP_ERROR : ((priority == LOG_WARNING) ? GROUP_WARNING : GROUP_REST)] ++;
  done = realtime;

  return true;
}

void TimelineView::move(int64_t index)
{
  if (intervals.empty())
  {
    return;
  }

  cursor = std::clamp<int64_t>(index, 0, (int64_t)intervals.size() - 1);
  follow = (cursor == (intervals.size() - 1));

  reveal();
  drawView();
}

void TimelineView::reveal()
{
  if (cursor < (size_t)delta.x)                 scrollTo(cursor, 0);
  if (cursor >= (size_t)(delta.x + size.x))     scrollTo(cursor - size.x + 1, 0);
}

void TimelineView::synchronize()
{
  JournalMoment moment;

  // The first entry of the interval, or its start when it is empty, every journal window goes there
  moment.source   = nullptr;
  moment.realtime = ((cursor < intervals.size()) && (intervals[cursor].first != 0)) ? intervals[cursor].first : (start + cursor * step);

  message(TProgram::deskTop, evBroadcast, cmJournalSyncTo, &moment);
}

void TimelineView::zoom(int direction)
{
  int64_t index;
  uint64_t moment;
  uint64_t next;

  if (journal == nullptr)
  {
    return;
  }

  for (index = 0; (index < (int64_t)std::size(steps) - 1) && ((steps[index] * 1000000ULL) < step); index ++)
  {
    // Step of the graph in the table
  }

  index  = std::clamp<int64_t>(index + direction, 0, (int64_t)std::size(steps) - 1);
  next   = steps[index] * 1000000ULL;

  if ((next == step) ||
      (((end - start) / next) >= INTERVAL_LIMIT))
  {
    // At an end of the table or too many columns
    return;
  }

  // The cursor stays at its time unless it follows the tail
  moment = start + cursor * step;
  chosen = next;
  begin(chosen);

  if (!follow)
  {
    cursor = (moment > start) ? ((moment - start) / step) : 0;
    reveal();
    drawView();
  }
}

void TimelineView::heights(const Interval& interval, uint64_t peak, int room, int* levels)
{
  int index;
  int eighths;
  uint64_t value;

  eighths = room * 8;
  value   = 0;

  for (index = 0; index < 3; index ++)
  {
    // Parts are stacked, a part that has entries takes at least an eighth so a single error is still there
    value         += interval.counts[index];
    levels[index]  = (peak == 0) ? 0 : (int)lround(logarithmic ? (log1p(value) / log1p(peak) * eighths) : ((double)value * eighths / peak));
    levels[index]  = std::max(levels[index], ((index > 0) ? levels[index - 1] : 0) + ((interval.counts[index] != 0) ? 1 : 0));
    levels[index]  = std::min(levels[index], eighths);
  }
}

std::string TimelineView::label(uint64_t realtime, bool full)
{
  time_t second;
  struct tm date;
  char text[32];

  second = realtime / 1000000;
  localtime_r(&second, &date);

  // As much as the step tells apart, the times under the graph get the day once they are hours apart
  strftime(text, sizeof(text),
    full ? ((step < 60000000ULL) ? "%F %T" : "%F %H:%M") :
    (step < 60000000ULL)    ? "%T" :
    (step < 600000000ULL)   ? "%H:%M" :
    (step < 86400000000ULL) ? "%m-%d %H:%M" : "%m-%d", &date);

  return text;
}
