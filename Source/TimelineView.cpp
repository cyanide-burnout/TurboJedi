// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_MsgBox
#include "TimelineView.h"
#include "TimelineWindow.h"
#include "JournalView.h"
#include "TimeDialog.h"

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
#define RANGE_HISTORY   64      // Ranges Ctrl-Z goes back through

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

// Dots of Braille filled from the bottom, of the left and of the right column of a cell, added to U+2800
static const uint8_t dots[2][5] =
{
  { 0x00, 0x40, 0x44, 0x46, 0x47 },
  { 0x00, 0x80, 0xa0, 0xb0, 0xb8 }
};

// Helpers

static uint64_t AlignTime(uint64_t realtime, uint64_t step, bool utc)
{
  time_t second;
  struct tm date;
  int64_t offset;
  int64_t local;

  // Hours and days start where the clock on the wall says, or the one of UTC
  second = realtime / 1000000;
  localtime_r(&second, &date);
  offset = utc ? 0 : (date.tm_gmtoff * 1000000LL);
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

TimelineView::TimelineView(const TRect& bounds, TScrollBar* horizontal, const JournalFilter& filter, int seconds, bool logarithmic, bool braille) :
  TScroller(bounds, horizontal, nullptr)
{
  this->filter      = filter;
  this->logarithmic = logarithmic;
  this->braille     = braille;

  growMode    = gfGrowHiX | gfGrowHiY;
  options    |= ofFirstClick;
  helpCtx     = hcTimeline;
  journal     = nullptr;
  notifier    = nullptr;
  blind       = false;
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
  anchor      = -1;

  open();
}

TimelineView::TimelineView(const TRect& bounds, TScrollBar* horizontal, const TimelineView& source) :
  TScroller(bounds, horizontal, nullptr)
{
  filter      = source.filter;
  error       = source.error;
  since       = source.since;
  until       = source.until;
  start       = source.start;
  step        = source.step;
  chosen      = source.chosen;
  end         = source.end;
  done        = source.done;
  closed      = source.closed;
  pending     = source.pending;
  logarithmic = source.logarithmic;
  braille     = source.braille;
  follow      = source.follow;
  intervals   = source.intervals;
  cursor      = source.cursor;

  growMode    = gfGrowHiX | gfGrowHiY;
  options    |= ofFirstClick;
  helpCtx     = hcTimeline;
  journal     = nullptr;
  notifier    = nullptr;
  blind       = false;
  anchor      = -1;

  // The expression is compiled again, it is not shared
  matcher.set(filter.text, filter.mode);

  if ((source.journal != nullptr) &&
      (OpenJournalQuery(&journal, filter) < 0))
  {
    // Counts stay, nothing more comes
    journal = nullptr;
  }

  if (journal != nullptr)
  {
    attach();

    if (!SeekJournalPlace(journal, source.journal))
    {
      // Nothing has been counted there yet, the range starts the way begin() takes it
      if (since != 0) sd_journal_seek_realtime_usec(journal, since);
      else            sd_journal_seek_head(journal);
    }
  }

  // The same columns, refresh() keeps the cursor on the screen of another size
  setLimit(intervals.size(), size.y);
  scrollTo(source.delta.x, 0);
  refresh();
}

TimelineView::~TimelineView()
{
  if (journal != nullptr)
  {
    // Window has been closed, the copy of inotify goes first
    ReleaseJournalSource(notifier);
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
  int units;
  int group;
  int edge;
  int side;
  int bits;
  int cell;
  int levels[3];
  size_t index;
  size_t first;
  size_t last;
  std::array<uint64_t, 3> sums;
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
  units   = room * (braille ? 4 : 8);
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

  if (selection(first, last))
  {
    // The selection instead of the cursor: its bounds, how many intervals and the counts of them all
    sums = { 0, 0, 0 };
    line = " " + label(start + first * step, true) + " \xe2\x80\x93 " + label(start + (last + 1) * step, true) + " " +
      std::to_string(last - first + 1) + "\xc3\x97" + FormatStep(step);

    for (index = first; index <= last; index ++)
    {
      sums[0] += intervals[index].counts[GROUP_ERROR];
      sums[1] += intervals[index].counts[GROUP_WARNING];
      sums[2] += intervals[index].counts[GROUP_REST];
    }

    snprintf(text, sizeof(text), "  err %llu  warn %llu  other %llu", (unsigned long long)sums[0], (unsigned long long)sums[1], (unsigned long long)sums[2]);
    line += text;
  }
  else if (cursor < intervals.size())
  {
    snprintf(text, sizeof(text), "  err %llu  warn %llu  other %llu",
      (unsigned long long)intervals[cursor].counts[GROUP_ERROR],
      (unsigned long long)intervals[cursor].counts[GROUP_WARNING],
      (unsigned long long)intervals[cursor].counts[GROUP_REST]);
    line += text;
  }

  line += logarithmic ? " \xe2\x94\x82 log" : " \xe2\x94\x82 linear";
  line += braille ? " \xe2\x94\x82 braille" : " \xe2\x94\x82 blocks";
  line += universal() ? " \xe2\x94\x82 UTC" : " \xe2\x94\x82 local";

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

  for (x = 0; x < size.x * share(); x ++)
  {
    // Boundaries of the three parts of every column on the screen, in eighths of a cell or in dots
    index = delta.x * share() + x;
    levels[0] = levels[1] = levels[2] = 0;

    if (index < intervals.size())
    {
      heights(intervals[index], peak, units, levels);
    }

    columns.push_back({ levels[0], levels[1], levels[2] });
  }

  for (y = 0; y < room; y ++)
  {
    // Rows go up from the bottom
    row = room - 1 - y;
    buffer.moveChar(0, ' ', normal, size.x);

    for (x = 0; (x < size.x) && braille; x ++)
    {
      // Two columns of four dots, the color of the worst part a dot of the cell belongs to: one color a cell
      bits  = 0;
      group = GROUP_NONE;
      color = (((size_t)(delta.x + x) == (cursor / 2)) || contains(delta.x + x)) ? marked : normal;

      for (side = 0; side < 2; side ++)
      {
        bits |= dots[side][std::clamp(columns[x * 2 + side][2] - row * 4, 0, 4)];

        for (lower = 0; lower < 3; lower ++)
        {
          edge = (lower > 0) ? columns[x * 2 + side][lower - 1] : 0;

          if ((columns[x * 2 + side][lower] > std::max(edge, row * 4)) &&
              (edge < (row * 4 + 4)))
          {
            group = std::min(group, lower);
            break;
          }
        }
      }

      if (bits != 0)
      {
        color.setForeground(TColorBIOS(colors[group]));
        text[0] = '\xe2';
        text[1] = (char)(0xa0 | (bits >> 6));
        text[2] = (char)(0x80 | (bits & 0x3f));
        text[3] = '\0';
        buffer.moveStr(x, text, color);
        continue;
      }

      buffer.moveChar(x, ' ', color, 1);
    }

    for (x = 0; (x < size.x) && !braille; x ++)
    {
      // A cell shows at most two parts: the lower one by the block, the one above it by the background
      lower = std::upper_bound(columns[x].begin(), columns[x].end(), row * 8) - columns[x].begin();
      color = (((size_t)(delta.x + x) == cursor) || contains(delta.x + x)) ? marked : normal;

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
        time = label(start + (delta.x + x) * share() * step, false);
        buffer.moveStr(x, time, stamp, std::min<int>(time.size(), size.x - x));
      }
    }

    cell = cursor / share();

    if ((cell >= delta.x) &&
        (cell < (delta.x + size.x)))
    {
      // A time there stays readable in the color of the cursor, a blank cell gets a mark
      x     = cell - delta.x;
      color = TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));

      if ((size_t)(cell % LABEL_SPACING) >= label(start, false).size())
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
  bool extend;
  bool dragged;
  int64_t cell;
  int64_t pressed;
  JournalPoll* poll;

  if (event.what == evKeyDown)
  {
    // Shift extends the selection from the cursor, as in a journal window
    extend = (event.keyDown.controlKeyState & kbShift) != 0;

    switch (event.keyDown.keyCode)
    {
      case kbLeft:   move((int64_t)cursor - 1, extend);                      break;
      case kbRight:  move((int64_t)cursor + 1, extend);                      break;
      case kbPgUp:   move((int64_t)cursor - size.x * share(), extend);       break;
      case kbPgDn:   move((int64_t)cursor + size.x * share(), extend);       break;
      case kbHome:   move(0, extend);                                        break;
      case kbEnd:    move((int64_t)intervals.size() - 1, extend);            break;
      case kbEnter:  synchronize();                                          break;

      case kbEsc:
        if (anchor < 0)
        {
          // Nothing to drop
          TScroller::handleEvent(event);
          return;
        }

        anchor = -1;
        drawView();
        break;

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

          case 'z':
          case 'Z':
            // The selection becomes the range
            narrow();
            break;

          case 'l':
          case 'L':
            changeScale();
            break;

          case 'b':
          case 'B':
            changeBars();
            break;

          case 'u':
          case 'U':
            changeZone();
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
      (event.mouse.buttons & mbRightButton))
  {
    // Back to the range before, as Ctrl-Z: zoomed in by the left button, out by the right one
    retreat();
    clearEvent(event);
    return;
  }

  if ((event.what == evMouseDown) &&
      (event.mouse.buttons & mbLeftButton))
  {
    cell = delta.x + makeLocal(event.mouse.where).x;

    if (event.mouse.eventFlags & meDoubleClick)
    {
      // Double click in the selection zooms into it, elsewhere it takes the windows to the interval under the mouse
      if (contains(cell))
      {
        narrow();
      }
      else
      {
        point(cell, false);
        synchronize();
      }

      clearEvent(event);
      return;
    }

    // A click puts the cursor there and drops the selection, Shift+click extends it; a click in the selection keeps it
    // for a double click. Dragging selects from where it has started, past an edge it scrolls
    extend  = (event.mouse.controlKeyState & kbShift) != 0;
    pressed = cell;
    dragged = false;

    if (extend || !contains(cell))
    {
      point(cell, extend);
    }

    while (mouseEvent(event, evMouseMove | evMouseAuto))
    {
      cell    = delta.x + makeLocal(event.mouse.where).x;
      dragged = dragged || (cell != pressed);

      if (!dragged)
      {
        // Not away from the cell yet
        continue;
      }

      if (extend) point(cell, true);
      else        stretch(pressed, cell);
    }

    clearEvent(event);
    return;
  }

  if (event.what == evCommand)
  {
    switch (event.message.command)
    {
      case cmJournalTime:     chooseTime();   break;
      case cmTimelineBars:    changeBars();   break;
      case cmTimelineZone:    changeZone();   break;
      case cmTimelineScale:   changeScale();  break;
      case cmTimelineZoom:    narrow();       break;
      case cmJournalBack:     retreat();      break;
      case cmJournalForward:  advance();      break;

      default:
        return;
    }

    clearEvent(event);
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalPoll) &&
      (journal != nullptr))
  {
    if ((blind) &&
        (!closed) &&
        ((CheckJournalFiles(filter.directory, files)) || (faults != GetJournalFaults())) &&
        (ReopenJournal(&journal, filter, std::string())))
    {
      // A rotation on a file system without inotify, or its files gone bad under the journal after a reconnect: opened again
      ReleaseJournalSource(notifier);
      attach();
    }

    // Never clear the poll, every window has to get it; inotify is cleared after the range as well, it would wake the application all along
    if (((sd_journal_process(journal) != SD_JOURNAL_NOP) || pending || blind) &&
        (!closed))
    {
      read();
      refresh();
    }

    // The next batch is counted at once, a journal inotify cannot tell everything about is asked again in time,
    // without a source of its own the wait cannot end on a change
    poll          = static_cast<JournalPoll*>(event.message.infoPtr);
    poll->timeout = (notifier != nullptr) ? poll->timeout : std::min(poll->timeout, JOURNAL_POLL_TIMEOUT);
    poll->timeout = pending ? 0 : GetJournalWait(journal, poll->timeout);
  }
}

void TimelineView::changeBounds(const TRect& bounds)
{
  TScroller::changeBounds(bounds);
  refresh();
}

const JournalFilter& TimelineView::getFilter() const
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

bool TimelineView::getBraille()
{
  return braille;
}

bool TimelineView::getUniversal()
{
  return universal();
}

bool TimelineView::getCounted(uint64_t& first, uint64_t& last, uint64_t& interval)
{
  if (intervals.empty())
  {
    return false;
  }

  first    = start;
  last     = start + intervals.size() * step;
  interval = step;
  return true;
}

bool TimelineView::getFocus(uint64_t& first, uint64_t& last, std::array<uint64_t, 3>& counts, bool& selected)
{
  size_t lower;
  size_t upper;

  if (intervals.empty())
  {
    return false;
  }

  if (!(selected = selection(lower, upper)))
  {
    // The interval under the cursor without a selection
    lower = std::min(cursor, intervals.size() - 1);
    upper = lower;
  }

  first  = start + lower * step;
  last   = start + (upper + 1) * step;
  counts = { 0, 0, 0 };

  for (size_t index = lower; index <= upper; index ++)
  {
    counts[0] += intervals[index].counts[GROUP_ERROR];
    counts[1] += intervals[index].counts[GROUP_WARNING];
    counts[2] += intervals[index].counts[GROUP_REST];
  }

  return true;
}

void TimelineView::attach()
{
  // Change notification for sd_journal_process(), the wait of the application ends on it as well;
  // a file system without it is looked at on every poll, the files it has now tell a rotation later
  notifier = CreateJournalSource(sd_journal_get_fd(journal));
  blind    = IsJournalBlind(journal, filter);
  faults   = GetJournalFaults();
  files.clear();

  if (blind)
  {
    CheckJournalFiles(filter.directory, files);
  }
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

  attach();

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
  anchor  = -1;
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

  if ((until != 0) &&
      (first > until))
  {
    // The first entry after the start is after the end as well: the range has none, it starts where it is told to
    first = (since != 0) ? since : until;
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
  width = std::max(size.x, 16) * share();
  step  = fixed;

  for (index = 0; (fixed == 0) && (index < std::size(steps)); index ++)
  {
    step = steps[index] * 1000000ULL;

    if (((last - AlignTime(first, step, universal())) / step) < width)
    {
      break;
    }
  }

  start = AlignTime(first, step, universal());
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
  setLimit((intervals.size() + share() - 1) / share(), size.y);

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

void TimelineView::move(int64_t index, bool extend)
{
  if (intervals.empty())
  {
    return;
  }

  // Without Shift the selection goes, with it the place the cursor leaves is the other end
  if (!extend)          anchor = -1;
  else if (anchor < 0)  anchor = cursor;

  cursor = std::clamp<int64_t>(index, 0, (int64_t)intervals.size() - 1);
  follow = (cursor == (intervals.size() - 1));

  reveal();
  drawView();
}

void TimelineView::point(int64_t cell, bool extend)
{
  // A cell of Braille has two intervals: the cursor stays on the one it is on, a selection takes whole cells at both
  // ends, as dragging does
  if (extend)                                    stretch(((anchor >= 0) ? anchor : (int64_t)cursor) / share(), cell);
  else if ((int64_t)(cursor / share()) == cell)  move(cursor, false);
  else                                           move(cell * share(), false);
}

void TimelineView::stretch(int64_t from, int64_t to)
{
  int64_t last;

  if (intervals.empty())
  {
    return;
  }

  // Whole cells from the one the mouse has been pressed in to the one it is over, the cursor at the far end
  last   = (int64_t)intervals.size() - 1;
  anchor = std::clamp<int64_t>((to >= from) ? (from * share()) : (from * share() + share() - 1), 0, last);
  cursor = std::clamp<int64_t>((to >= from) ? (to * share() + share() - 1) : (to * share()), 0, last);
  follow = (cursor == (size_t)last);

  reveal();
  drawView();
}

bool TimelineView::selection(size_t& lower, size_t& upper)
{
  if ((anchor < 0) ||
      (intervals.empty()))
  {
    return false;
  }

  lower = std::min<size_t>(anchor, cursor);
  upper = std::min<size_t>(std::max<size_t>(anchor, cursor), intervals.size() - 1);
  return true;
}

bool TimelineView::contains(int64_t cell)
{
  size_t lower;
  size_t upper;

  return selection(lower, upper) &&
    (cell >= 0) &&
    ((size_t)(cell * share() + share() - 1) >= lower) &&
    ((size_t)(cell * share()) <= upper);
}

TimelineView::Range TimelineView::selected()
{
  size_t lower;
  size_t upper;
  Range range;

  // Whole intervals up to the last microsecond of the last one, until takes it in; one that reaches the newest of a
  // range open at the end keeps it open, the timeline follows on
  selection(lower, upper);
  range.since  = FormatJournalTime(start + lower * step, universal());
  range.until  = ((upper == (intervals.size() - 1)) && (until == 0)) ? std::string() : FormatJournalTime(start + (upper + 1) * step - 1, universal());
  range.chosen = 0;

  return range;
}

void TimelineView::narrow()
{
  if (anchor < 0)
  {
    // Nothing chosen to zoom into
    return;
  }

  change(selected());
}

void TimelineView::chooseTime()
{
  bool valid;
  uint64_t stamp;
  Range range;
  TimeData data;
  std::string since;
  std::string until;

  // The range of the timeline, or the one of its selection to correct before it is taken, in the zone of the timeline
  range = (anchor >= 0) ? selected() : Range { filter.since, filter.until, 0 };

  memset(&data, 0, sizeof(data));
  data.utc = universal() || IsJournalTimeUniversal(range.since) || IsJournalTimeUniversal(range.until);
  ConvertJournalTime(range.since, false, data.utc, since);
  ConvertJournalTime(range.until, false, data.utc, until);
  strncpy(data.since, since.c_str(), sizeof(data.since) - 1);
  strncpy(data.until, until.c_str(), sizeof(data.until) - 1);

  do
  {
    if (TProgram::application->executeDialog(new TimeDialog(), &data) != cmOK)
    {
      // Nothing changes
      return;
    }

    valid =
      ((data.since[0] == '\0') || ParseJournalTime(data.since, &stamp)) &&
      ((data.until[0] == '\0') || ParseJournalTime(data.until, &stamp));

    if (!valid)
    {
      // Back to the dialog with what has been typed
      messageBox("Unknown time format", mfError | mfOKButton);
    }
  }
  while (!valid);

  if ((anchor < 0) &&
      (since == data.since) &&
      (until == data.until))
  {
    // The same range, the counts stay
    return;
  }

  change(Range { data.since, data.until, 0 });
}

void TimelineView::change(const Range& range)
{
  // The range left goes into the history, a new one ends the way forward
  back.push_back(Range { filter.since, filter.until, chosen });
  ahead.clear();

  if (back.size() > RANGE_HISTORY)
  {
    back.erase(back.begin());
  }

  apply(range);
}

void TimelineView::apply(const Range& range)
{
  TimelineWindow* window;

  chosen = range.chosen;

  if ((range.since != filter.since) ||
      (range.until != filter.until))
  {
    // Checked when it has been taken, a relative time is taken from now again; a new step alone keeps the range
    filter.since = range.since;
    filter.until = range.until;
    since        = 0;
    until        = 0;

    if (!filter.since.empty()) ParseJournalTime(filter.since, &since);
    if (!filter.until.empty()) ParseJournalTime(filter.until, &until);
  }

  if ((window = dynamic_cast<TimelineWindow*>(owner)) != nullptr)
  {
    // The title tells the range
    window->retitle();
  }

  if (journal == nullptr)
  {
    // Nothing to count it with
    anchor = -1;
    drawView();
    return;
  }

  restart(chosen);
}

void TimelineView::retreat()
{
  Range range;

  if (back.empty())
  {
    return;
  }

  range = back.back();
  back.pop_back();
  ahead.push_back(Range { filter.since, filter.until, chosen });
  apply(range);
}

void TimelineView::advance()
{
  Range range;

  if (ahead.empty())
  {
    return;
  }

  range = ahead.back();
  ahead.pop_back();
  back.push_back(Range { filter.since, filter.until, chosen });
  apply(range);
}

void TimelineView::reveal()
{
  int cell;

  cell = cursor / share();

  if (cell < delta.x)                 scrollTo(cell, 0);
  if (cell >= (delta.x + size.x))     scrollTo(cell - size.x + 1, 0);
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

  // A step of the history as a range is, Ctrl-Z goes back to the step before
  change(Range { filter.since, filter.until, next });
}

void TimelineView::changeScale()
{
  // Spikes of errors stay visible next to a steady flow of everything else
  logarithmic = !logarithmic;
  drawView();
}

void TimelineView::changeBars()
{
  // Twice the intervals across, half the rows of a cell up; a step the window has chosen is chosen anew
  braille = !braille;
  scrollTo(braille ? (delta.x / 2) : (delta.x * 2), 0);

  if ((journal != nullptr) &&
      (chosen == 0))
  {
    restart(0);
    return;
  }

  refresh();
}

void TimelineView::changeZone()
{
  // Hours and days of the intervals start on the clock of the other zone, the range stays as it has been typed
  filter.columns ^= JOURNAL_COLUMN_UTC;

  if (journal != nullptr)
  {
    restart(chosen);
    return;
  }

  drawView();
}

bool TimelineView::universal()
{
  return (filter.columns & JOURNAL_COLUMN_UTC) != 0;
}

void TimelineView::restart(uint64_t fixed)
{
  uint64_t moment;

  // The cursor stays at its time unless it follows the tail
  moment = start + cursor * step;
  begin(fixed);

  if (!follow)
  {
    // Within the new range: up to the interval of its newest entry, those after it are read in later batches
    cursor = (moment > start) ? std::min((moment - start) / step, (end - start) / step) : 0;
    reveal();
    drawView();
  }
}

int TimelineView::share()
{
  // Intervals a cell
  return braille ? 2 : 1;
}

void TimelineView::heights(const Interval& interval, uint64_t peak, int units, int* levels)
{
  int index;
  uint64_t value;

  value = 0;

  for (index = 0; index < 3; index ++)
  {
    // Parts are stacked, a part that has entries takes at least an eighth or a dot so a single error is still there
    value         += interval.counts[index];
    levels[index]  = (peak == 0) ? 0 : (int)lround(logarithmic ? (log1p(value) / log1p(peak) * units) : ((double)value * units / peak));
    levels[index]  = std::max(levels[index], ((index > 0) ? levels[index - 1] : 0) + ((interval.counts[index] != 0) ? 1 : 0));
    levels[index]  = std::min(levels[index], units);
  }
}

std::string TimelineView::label(uint64_t realtime, bool full)
{
  time_t second;
  struct tm date;
  char text[32];

  second = realtime / 1000000;
  universal() ? gmtime_r(&second, &date) : localtime_r(&second, &date);

  // As much as the step tells apart, the times under the graph get the day once they are hours apart
  strftime(text, sizeof(text),
    full ? ((step < 60000000ULL) ? "%F %T" : "%F %H:%M") :
    (step < 60000000ULL)    ? "%T" :
    (step < 600000000ULL)   ? "%H:%M" :
    (step < 86400000000ULL) ? "%m-%d %H:%M" : "%m-%d", &date);

  return text;
}
