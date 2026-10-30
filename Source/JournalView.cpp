// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TFrame
#define Uses_TWindow
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TStatusLine
#define Uses_TFileDialog
#define Uses_MsgBox
#include "JournalView.h"
#include "SourceDialog.h"
#include "TimeDialog.h"
#include "PageDialog.h"
#include "PickDialog.h"
#include "TextDialog.h"
#include "LevelsDialog.h"
#include "ColumnsDialog.h"
#include "JournalsDialog.h"
#include "JediClipboard.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <fstream>
#include <algorithm>

#define NO_SELECTION  UINT64_MAX
#define FIRST_LINE    (1ULL << 62)  // Number of the first line, history goes in front of it
#define READ_STEP     5000          // Entries read at once, the rest comes with the next polls
#define BYTE_CAPACITY (64 << 20)    // Text a window keeps whatever the count of its lines, one huge message stays
#define HISTORY_STEP  1000          // Entries brought in front when scrolling above the first line
#define PLACE_DEPTH   16            // Jumps Back remembers

#define HOST_WIDTH        24  // Limits of the columns as wide as their longest values
#define UNIT_WIDTH        32
#define IDENTIFIER_WIDTH  24
#define PID_WIDTH         8

static const char* const tags[] =
{
  "EMERG ",
  "ALERT ",
  "CRIT  ",
  "ERR   ",
  "WARN  ",
  "NOTICE",
  "INFO  ",
  "DEBUG "
};

// Helpers

static std::string GetSource(const JournalLine& line, uint16_t columns)
{
  std::string source;

  // Syslog style: identifier[pid]
  source  = (columns & JOURNAL_COLUMN_IDENTIFIER) ? line.identifier : std::string();
  source += ((columns & JOURNAL_COLUMN_PID) && (!line.pid.empty())) ? ("[" + line.pid + "]") : std::string();

  return source;
}

static int GetSourceWidth(const int* widths, uint16_t columns)
{
  return
    ((columns & JOURNAL_COLUMN_IDENTIFIER) ? widths[2] : 0) +
    ((columns & JOURNAL_COLUMN_PID) ? (widths[3] + 2) : 0);
}

static void PaintText(TDrawBuffer& buffer, int x, const std::string& text, size_t start, size_t end, int skip, int room, TColorAttr color)
{
  int column;
  int last;

  // Bytes of the text to columns of the screen, scrolled or wrapped by skip
  last = std::min(strwidth(TStringView(text.data(), end)) - skip, room);

  for (column = std::max(strwidth(TStringView(text.data(), start)) - skip, 0); column < last; column ++)
  {
    buffer.putAttribute(x + column, color);
  }
}

static size_t GetLineSize(const JournalLine& line)
{
  // Near enough to tell a few huge messages from many short ones
  return sizeof(JournalLine) + line.host.size() + line.unit.size() + line.identifier.size() + line.pid.size() + line.cursor.size() + line.text.size() + line.tints.size() * sizeof(JournalTint);
}

// JournalView

JournalView::JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalFilter& filter, int history, size_t capacity) :
  TScroller(bounds, horizontal, vertical)
{
  this->filter    = filter;
  this->history   = history;
  this->capacity  = capacity;
  bytes           = 0;

  growMode    = gfGrowHiX | gfGrowHiY;
  options    |= ofFirstClick;
  highlighter = JournalHighlighter::get(filter.highlight);
  journal     = nullptr;
  notifier    = nullptr;
  header   = nullptr;
  since    = 0;
  until    = 0;
  closed   = false;
  pending  = false;
  older    = false;
  layout   = 0;
  first    = FIRST_LINE;
  width    = 0;
  unseen   = 0;
  follow   = true;
  moving   = false;
  anchor   = NO_SELECTION;
  cursor   = NO_SELECTION;

  open();
}

JournalView::JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalView& source) :
  TScroller(bounds, horizontal, vertical)
{
  memcpy(counts, source.counts, sizeof(counts));
  memcpy(widths, source.widths, sizeof(widths));

  filter      = source.filter;
  history     = source.history;
  capacity    = source.capacity;
  lines       = source.lines;
  bytes       = source.bytes;
  first       = source.first;
  width       = source.width;
  highlighter = source.highlighter;
  since       = source.since;
  until       = source.until;
  closed      = source.closed;
  pending     = source.pending;
  older       = source.older;
  unseen      = source.unseen;
  follow      = source.follow;
  anchor      = source.anchor;
  cursor      = source.cursor;
  places      = source.places;

  growMode  = gfGrowHiX | gfGrowHiY;
  options  |= ofFirstClick;
  journal   = nullptr;
  notifier  = nullptr;
  header    = nullptr;
  layout    = 0;
  moving    = false;

  // Expressions are compiled again, they are not shared
  matcher.set(filter.text, filter.mode);
  finder.set(source.finder.getText(), source.finder.getMode());

  if ((source.journal != nullptr) &&
      (OpenJournalQuery(&journal, filter) < 0))
  {
    // Lines stay, nothing more comes
    journal = nullptr;
  }

  if (journal != nullptr)
  {
    // Change notification for sd_journal_process(), the wait of the application ends on it as well
    notifier = CreateJournalSource(sd_journal_get_fd(journal));

    if (!SeekJournalPlace(journal, source.journal))
    {
      // Nothing has been read there yet, the tail the way open() takes it without history
      sd_journal_seek_tail(journal);
      sd_journal_previous(journal);
    }
  }

  // The same rows at the top, the screen of another size keeps the selection on it in changeBounds()
  moving = true;
  setLimit((filter.columns & JOURNAL_COLUMN_WRAP) ? 0 : (indent() + width), total());
  scrollTo(source.delta.x, source.delta.y);
  moving = false;

  refresh();
}

JournalView::~JournalView()
{
  if (journal != nullptr)
  {
    // Window has been closed, the copy of inotify goes first
    ReleaseJournalSource(notifier);
    sd_journal_close(journal);
  }
}

void JournalView::draw()
{
  // Foreground per priority over the window background, the most severe ones get a red background of their own
  static const uint8_t colors[][2] =
  {
    { 0x0f, 0x04 },  // Emergency: white on red
    { 0x0e, 0x04 },  // Alert: yellow on red
    { 0x0d, 0xff },  // Critical: light magenta
    { 0x0c, 0xff },  // Error: light red
    { 0x0e, 0xff },  // Warning: yellow
    { 0x0b, 0xff },  // Notice: light cyan
    { 0x0f, 0xff },  // Informational: white
    { 0x07, 0xff }   // Debug: light gray
  };

  int x;
  int y;
  int room;
  int skip;
  bool chosen;
  bool head;
  size_t index;
  uint64_t row;
  uint64_t chunk;
  uint64_t lower;
  uint64_t upper;
  char date[16];
  char clock[16];
  std::string source;
  std::vector<std::pair<size_t, size_t>> ranges;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr marked;
  TColorAttr stamp;
  TColorAttr color;
  TColorAttr found;
  const JournalLine* line;

  // Background comes from the window palette like in any other scroller
  normal = mapColor(1);
  marked = mapColor(2);
  stamp  = normal;
  found  = TColorAttr(TColorBIOS(0x0), TColorBIOS(0xe));
  stamp.setForeground(TColorBIOS(0x03));

  lower = std::min(anchor, cursor);
  upper = (anchor != NO_SELECTION) ? std::max(anchor, cursor) : 0;

  for (y = 0; y < size.y; y ++)
  {
    row = delta.y + y;
    buffer.moveChar(0, ' ', normal, size.x);

    if (row < total())
    {
      index = lineOf(row);
      chunk = row - rowOf(index);
      line  = &lines[index];
      color = normal;
      color.setForeground(TColorBIOS(colors[line->priority][0]));

      if (colors[line->priority][1] != 0xff)
      {
        color.setBackground(TColorBIOS(colors[line->priority][1]));
      }

      chosen = ((first + index) >= lower) && ((first + index) <= upper);

      if (chosen)
      {
        // Selected rows look like a selection in any other scroller
        buffer.moveChar(0, ' ', marked, size.x);
        color = marked;
      }

      x    = 0;
      head = (chunk == 0) && (!line->continued);

      if (head)
      {
        // Columns go on the first row of an entry only, wrapped rows and the next lines of a message go on under it
        format(line->realtime, date, clock);

        if (filter.columns & JOURNAL_COLUMN_DATE)
        {
          buffer.moveStr(x, date, chosen ? marked : stamp);
        }

        if (filter.columns & JOURNAL_COLUMN_TIME)
        {
          buffer.moveStr(x + ((filter.columns & JOURNAL_COLUMN_DATE) ? 11 : 0), clock, chosen ? marked : stamp);
        }
      }

      x += (filter.columns & JOURNAL_COLUMN_DATE) ? 11 : 0;
      x += (filter.columns & JOURNAL_COLUMN_TIME) ? ((filter.columns & JOURNAL_COLUMN_MILLISECOND) ? 13 : 9) : 0;

      if (filter.columns & JOURNAL_COLUMN_LEVEL)
      {
        buffer.moveStr(x, head ? tags[line->priority] : "", color);
        x += 7;
      }

      if (filter.columns & JOURNAL_COLUMN_HOST)
      {
        buffer.moveStr(x, head ? line->host : std::string(), chosen ? marked : normal, widths[0]);
        x += widths[0] + 1;
      }

      if (filter.columns & JOURNAL_COLUMN_UNIT)
      {
        buffer.moveStr(x, head ? line->unit : std::string(), chosen ? marked : normal, widths[1]);
        x += widths[1] + 1;
      }

      if (filter.columns & (JOURNAL_COLUMN_IDENTIFIER | JOURNAL_COLUMN_PID))
      {
        source = head ? GetSource(*line, filter.columns) : std::string();
        buffer.moveStr(x, source, color, GetSourceWidth(widths, filter.columns));
        x += GetSourceWidth(widths, filter.columns) + 1;
      }

      // Horizontal scrolling moves the message only, wrapping shows its next part on every row
      room = (filter.columns & JOURNAL_COLUMN_WRAP) ? std::min(span(), size.x - x) : (size.x - x);
      skip = (filter.columns & JOURNAL_COLUMN_WRAP) ? (chunk * span()) : delta.x;

      if (room > 0)
      {
        buffer.moveStr(x, line->text, color, room, skip);

        if (!chosen)
        {
          for (const JournalTint& tint : line->tints)
          {
            // Colors the message has come with, the rules paint over them
            PaintText(buffer, x, line->text, tint.start, tint.end, skip, room, tint.paint.apply(color));
          }
        }

        if ((!chosen) &&
            (highlighter != nullptr))
        {
          // Rules paint over the color of the level, a selection stays plain
          highlighter->apply(line->text, strokes);

          for (const JournalStroke& stroke : strokes)
          {
            PaintText(buffer, x, line->text, stroke.start, stroke.end, skip, room, stroke.paint->apply(color));
          }
        }

        if (!finder.empty())
        {
          finder.locate(line->text, ranges);

          for (const std::pair<size_t, size_t>& range : ranges)
          {
            // Found text stands out over everything
            PaintText(buffer, x, line->text, range.first, range.second, skip, room, found);
          }
        }
      }
    }

    writeLine(0, y, size.x, 1, buffer);
  }
}

void JournalView::handleEvent(TEvent& event)
{
  int64_t index;
  uint64_t count;
  bool extend;
  JournalPoll* poll;

  if (event.what == evKeyDown)
  {
    if (event.keyDown.charScan.charCode == ' ')
    {
      pause();
      clearEvent(event);
      return;
    }

    if (event.keyDown.keyCode == kbEnter)
    {
      details();
      clearEvent(event);
      return;
    }

    if (event.keyDown.keyCode == kbAltBack)
    {
      // Terminals that keep Alt-Left for themselves
      restore();
      clearEvent(event);
      return;
    }

    if ((event.keyDown.keyCode == kbEsc) &&
        (anchor != NO_SELECTION))
    {
      anchor = NO_SELECTION;
      cursor = NO_SELECTION;
      refresh();
      clearEvent(event);
      return;
    }

    if ((delta.y == 0) &&
        ((event.keyDown.keyCode == kbUp)   ||
         (event.keyDown.keyCode == kbPgUp) ||
         (event.keyDown.keyCode == kbHome) ||
         (event.keyDown.keyCode == kbCtrlHome) ||
         (event.keyDown.keyCode == kbCtrlPgUp)))
    {
      // Above the first line comes the history, the key itself is handled as usual afterwards
      back();
    }

    if ((event.keyDown.controlKeyState & kbShift) &&
        ((event.keyDown.keyCode == kbUp)   ||
         (event.keyDown.keyCode == kbDown) ||
         (event.keyDown.keyCode == kbPgUp) ||
         (event.keyDown.keyCode == kbPgDn) ||
         (event.keyDown.keyCode == kbHome) ||
         (event.keyDown.keyCode == kbEnd)))
    {
      if (anchor == NO_SELECTION)
      {
        // Selection starts on the last row of the window
        select(lineOf(std::min<uint64_t>(delta.y + size.y, total()) - 1), false);
      }

      index = (cursor > first) ? (cursor - first) : 0;

      switch (event.keyDown.keyCode)
      {
        case kbUp:   index -= 1;                         break;
        case kbDown: index += 1;                         break;
        case kbPgUp: index -= size.y;                    break;
        case kbPgDn: index += size.y;                    break;
        case kbHome: index  = 0;                         break;
        case kbEnd:  index  = (int64_t)lines.size() - 1; break;
      }

      select(index, true);
      clearEvent(event);
      return;
    }
  }

  TScroller::handleEvent(event);

  if ((event.what == evMouseDown) &&
      (event.mouse.buttons & mbLeftButton))
  {
    if (event.mouse.eventFlags & meDoubleClick)
    {
      // Double click opens the entry under the mouse
      select(lineOf(delta.y + makeLocal(event.mouse.where).y), false);
      details();
      clearEvent(event);
      return;
    }

    // Click selects a row, Shift+click and dragging extend, dragging past an edge scrolls
    extend = (event.mouse.controlKeyState & kbShift) != 0;

    do
    {
      select(lineOf(std::max<int64_t>(0, delta.y + makeLocal(event.mouse.where).y)), extend);
      extend = true;
    }
    while (mouseEvent(event, evMouseMove | evMouseAuto));

    clearEvent(event);
    return;
  }

  switch (event.what)
  {
    case evCommand:
      switch (event.message.command)
      {
        case cmJournalLevels:        chooseLevels();   break;
        case cmJournalText:          chooseText();     break;
        case cmJournalSource:        chooseSource();   break;
        case cmJournalJournals:      chooseJournals(); break;
        case cmJournalTime:          chooseTime();     break;
        case cmJournalColumns:       chooseColumns();  break;
        case cmJournalHighlight:     chooseHighlight(); break;
        case cmJournalFind:          chooseFind();     break;
        case cmJournalFindNext:      find(1);          break;
        case cmJournalFindPrevious:  find(-1);         break;
        case cmJournalGoTo:          chooseMoment();   break;
        case cmJournalSync:          synchronize();    break;
        case cmJournalBack:          restore();        break;
        case cmJournalDetails:       details();        break;
        case cmJournalExport:        save();           break;
        case cmJournalPause:         pause();          break;
        case cmJournalCopy:          copy();           break;
        case cmJournalClear:         clear();          break;

        case cmJournalWrap:
          // Rows are counted again for the new mode
          filter.columns ^= JOURNAL_COLUMN_WRAP;
          layout = 0;
          refresh();
          break;

        default:
          return;
      }
      clearEvent(event);
      break;

    case evBroadcast:
      if ((event.message.command == cmJournalPoll) &&
          (journal != nullptr) &&
          (pending || (sd_journal_process(journal) != SD_JOURNAL_NOP)))
      {
        // Never clear the poll, every window has to get it
        count = first + lines.size();
        read();

        if ((pending) ||
            ((first + lines.size()) != count))
        {
          // Something has passed the filter or more is to come
          refresh();
        }
      }

      if ((event.message.command == cmJournalPoll) &&
          (journal != nullptr))
      {
        // The next batch is read at once, a journal inotify cannot tell everything about is asked again in time,
        // without a source of its own the wait cannot end on a change
        poll          = static_cast<JournalPoll*>(event.message.infoPtr);
        poll->timeout = (notifier != nullptr) ? poll->timeout : std::min(poll->timeout, JOURNAL_POLL_TIMEOUT);
        poll->timeout = pending ? 0 : GetJournalWait(journal, poll->timeout);
      }

      if ((event.message.command == cmJournalSyncTo) &&
          (static_cast<JournalMoment*>(event.message.infoPtr)->source != this))
      {
        // Never clear the moment either, every other window goes there
        seek(static_cast<JournalMoment*>(event.message.infoPtr)->realtime, true);
      }

      if ((event.message.command == cmScrollBarChanged) &&
          (event.message.infoPtr == vScrollBar) &&
          (!moving))
      {
        // Scrolling away from the tail pauses, scrolling back to it resumes
        follow  = delta.y >= limit.y - size.y;
        unseen *= !follow;

        if (delta.y == 0)
        {
          // Scrolled up to the first line, the history comes in front of it
          back();
        }

        refresh();
      }
      break;
  }
}

void JournalView::changeBounds(const TRect& bounds)
{
  TScroller::changeBounds(bounds);

  if ((anchor != NO_SELECTION) &&
      (cursor >= first))
  {
    // The selected line stays on the screen of another size, following takes the tail anyway
    reveal(cursor - first);
  }

  refresh();
}

const JournalFilter& JournalView::getFilter() const
{
  return filter;
}

void JournalView::open()
{
  int result;
  char message[128];
  JournalLine model;
  std::vector<JournalLine> parts;

  if (journal != nullptr)
  {
    // Filter has changed, the query starts over
    ReleaseJournalSource(notifier);
    sd_journal_close(journal);
    journal  = nullptr;
    notifier = nullptr;
  }

  clear();

  since   = 0;
  until   = 0;
  closed  = false;
  pending = false;
  older   = true;

  // Lines of another query
  places.clear();

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
    model.realtime = time(nullptr) * 1000000ULL;
    model.priority = LOG_ERR;
    model.text     = message;
    push(model);
    refresh();
    return;
  }

  // Change notification for sd_journal_process(), the wait of the application ends on it as well
  notifier = CreateJournalSource(sd_journal_get_fd(journal));

  if (!filter.until.empty())
  {
    // Validated by the dialog and the command line, a closed range stops reading at its end
    ParseJournalTime(filter.until, &until);
  }

  if (!filter.since.empty())
  {
    // History in front of the range is not a part of it
    ParseJournalTime(filter.since, &since);
  }

  if (since != 0)
  {
    // Range starts at its time instead of the last entries
    sd_journal_seek_realtime_usec(journal, since);
  }
  else
  {
    // History first: the cursor lands on the oldest of the last entries, which next() would step over,
    // a step back is needed even without history, next() from the bare tail finds nothing at all
    if (until != 0) sd_journal_seek_realtime_usec(journal, until + 1);
    else            sd_journal_seek_tail(journal);

    if ((sd_journal_previous_skip(journal, (history > 0) ? history : 1) > 0) &&
        (history > 0) &&
        (parse(journal, model, buffer)))
    {
      // Show the entry the cursor is on before moving forward
      split(model, buffer, parts);

      for (JournalLine& part : parts)
      {
        push(part);
      }
    }
  }

  read();
  refresh();

  if (owner != nullptr)
  {
    // Window title shows the filter
    static_cast<TWindow*>(owner)->frame->drawView();
  }
}

void JournalView::read()
{
  int count;
  uint64_t realtime;
  JournalLine model;
  std::vector<JournalLine> parts;

  for (count = 0; (!closed) && (count < READ_STEP) && (sd_journal_next(journal) > 0); count ++)
  {
    if ((until != 0) &&
        (sd_journal_get_realtime_usec(journal, &realtime) >= 0) &&
        (realtime > until))
    {
      // Range is over, later entries never belong to it
      closed = true;
      break;
    }

    if (parse(journal, model, buffer))
    {
      // Whatever has been appended meanwhile
      split(model, buffer, parts);

      for (JournalLine& part : parts)
      {
        push(part);
      }
    }
  }

  // A long range comes in batches, the screen keeps going between them
  pending = (count == READ_STEP);
  trim();
}

size_t JournalView::back()
{
  int count;
  bool ended;
  size_t size;
  uint64_t realtime;
  uint64_t before;
  sd_journal* source;
  JournalLine model;
  std::vector<JournalLine> parts;
  std::vector<JournalLine> batch;

  if ((!older) ||
      (lines.empty()) ||
      (lines.front().cursor.empty()) ||
      (OpenJournalQuery(&source, filter) < 0))
  {
    return 0;
  }

  // A journal of its own, the one of the window keeps following the tail,
  // seeking lands on the entry of the first line, the next steps go older
  ended = (sd_journal_seek_cursor(source, lines.front().cursor.c_str()) < 0) || (sd_journal_previous(source) <= 0);

  for (count = 0, size = bytes; (!ended) && (count < HISTORY_STEP) && ((lines.size() + batch.size()) < capacity) && (size < BYTE_CAPACITY); count ++)
  {
    if (sd_journal_previous(source) <= 0)
    {
      // The very first entry of the journal
      ended = true;
      break;
    }

    if ((since != 0) &&
        (sd_journal_get_realtime_usec(source, &realtime) >= 0) &&
        (realtime < since))
    {
      // Range starts here
      ended = true;
      break;
    }

    if (parse(source, model, buffer))
    {
      // Entries come newest first, their lines go in reverse so the batch can be put in front as it is
      split(model, buffer, parts);

      for (const JournalLine& part : parts)
      {
        // Bounded the way trim() bounds the tail
        size += GetLineSize(part);
      }

      batch.insert(batch.end(), std::make_move_iterator(parts.rbegin()), std::make_move_iterator(parts.rend()));
    }
  }

  sd_journal_close(source);
  older  = !ended;
  before = total();

  for (JournalLine& line : batch)
  {
    lines.push_front(std::move(line));
    bytes += GetLineSize(lines.front());
    measure(lines.front());
    first --;
  }

  if (!batch.empty())
  {
    // The rows on the screen stay where they are
    layout = 0;
    moving = true;
    setLimit(limit.x, total());
    scrollTo(delta.x, delta.y + (total() - before));
    moving = false;

    // The position may stay the same when everything fits on the screen
    refresh();
  }

  return batch.size();
}

bool JournalView::parse(sd_journal* source, JournalLine& model, std::string& message)
{
  size_t length;
  char* position;
  const char* data;
  char value[JOURNAL_VALUE_LENGTH];

  if (sd_journal_get_realtime_usec(source, &model.realtime) < 0)
  {
    // Entry without a time stamp would break the order of the history anyway
    return false;
  }

  model.priority = LOG_INFO;

  if (GetJournalField(source, "PRIORITY", value, sizeof(value)) &&
      (value[0] >= '0') &&
      (value[0] <= '7') &&
      (value[1] == '\0'))
  {
    // Anything else falls back to informational
    model.priority = value[0] - '0';
  }

  // Every field goes to the line, the window shows the columns it has chosen
  model.identifier = (GetJournalField(source, "SYSLOG_IDENTIFIER", value, sizeof(value)) || GetJournalField(source, "_COMM", value, sizeof(value))) ? value : "?";
  model.pid        = GetJournalField(source, "_PID",          value, sizeof(value)) ? value : "";
  model.host       = GetJournalField(source, "_HOSTNAME",     value, sizeof(value)) ? value : "";
  model.unit       = GetJournalField(source, "_SYSTEMD_UNIT", value, sizeof(value)) ? value : "";
  model.cursor.clear();

  if (sd_journal_get_cursor(source, &position) >= 0)
  {
    // Details of the entry are read by it
    model.cursor = position;
    free(position);
  }

  // Data of MESSAGE stays valid only until the next request to the journal, so it goes last
  if ((sd_journal_get_data(source, "MESSAGE", (const void**)&data, &length) < 0) ||
      (length < 8))
  {
    // Entry has no message
    return false;
  }

  message.assign(data + 8, length - 8);

  if ((!matcher.empty()) &&
      (matcher.match(model.identifier + "[" + model.pid + "]: " + message) == ((filter.mode & JOURNAL_TEXT_EXCLUDE) != 0)))
  {
    // The only filter the journal cannot apply, it looks at the syslog style source as well
    return false;
  }

  counts[model.priority] ++;
  return true;
}

void JournalView::split(const JournalLine& model, const std::string& message, std::vector<JournalLine>& parts)
{
  size_t start;
  size_t end;
  size_t size;
  JournalPaint paint;

  parts.clear();
  start = 0;
  paint = JournalPaint {};

  // Line breaks at the end would leave empty rows under the entry, a blank message stays one empty line
  size = message.find_last_not_of(" \t\r\n") + 1;

  do
  {
    // Multi-line message (e.g. a trace) becomes one line per row with the same fields
    end = std::min(message.find('\n', start), size);
    parts.push_back(model);
    parts.back().continued = (start != 0);

    TakeJournalEscapes(message, start, end, parts.back().text, parts.back().tints, paint);

    start = end + 1;
  }
  while (start < size);
}

void JournalView::push(JournalLine& line)
{
  lines.push_back(std::move(line));
  bytes += GetLineSize(lines.back());
  measure(lines.back());
  unseen += !follow;

  if ((filter.columns & JOURNAL_COLUMN_WRAP) &&
      (layout == span()) &&
      (rows.size() == lines.size()))
  {
    // Counted rows go on without counting them all again
    rows.push_back(rows.back() + std::max(1, (strwidth(lines.back().text) + layout - 1) / layout));
  }
  else
  {
    layout = 0;
  }
}

void JournalView::measure(const JournalLine& line)
{
  width     = std::max(width, strwidth(line.text));
  widths[0] = std::max(widths[0], std::min(strwidth(line.host),       HOST_WIDTH));
  widths[1] = std::max(widths[1], std::min(strwidth(line.unit),       UNIT_WIDTH));
  widths[2] = std::max(widths[2], std::min(strwidth(line.identifier), IDENTIFIER_WIDTH));
  widths[3] = std::max(widths[3], std::min(strwidth(line.pid),        PID_WIDTH));
}

int JournalView::indent()
{
  return
    ((filter.columns & JOURNAL_COLUMN_DATE)  ? 11 : 0) +
    ((filter.columns & JOURNAL_COLUMN_TIME)  ? ((filter.columns & JOURNAL_COLUMN_MILLISECOND) ? 13 : 9) : 0) +
    ((filter.columns & JOURNAL_COLUMN_LEVEL) ? 7  : 0) +
    ((filter.columns & JOURNAL_COLUMN_HOST)  ? (widths[0] + 1) : 0) +
    ((filter.columns & JOURNAL_COLUMN_UNIT)  ? (widths[1] + 1) : 0) +
    ((filter.columns & (JOURNAL_COLUMN_IDENTIFIER | JOURNAL_COLUMN_PID)) ? (GetSourceWidth(widths, filter.columns) + 1) : 0);
}

int JournalView::span()
{
  // Width a wrapped message gets, never too narrow to read
  return std::max(16, size.x - indent());
}

void JournalView::count()
{
  size_t index;

  layout = span();
  rows.resize(lines.size() + 1);
  rows[0] = 0;

  for (index = 0; index < lines.size(); index ++)
  {
    // Rows in front of every line, an empty message still takes one
    rows[index + 1] = rows[index] + std::max(1, (strwidth(lines[index].text) + layout - 1) / layout);
  }
}

uint64_t JournalView::total()
{
  if (!(filter.columns & JOURNAL_COLUMN_WRAP))
  {
    // A row per line
    return lines.size();
  }

  if ((layout != span()) ||
      (rows.size() != (lines.size() + 1)))
  {
    // Lines or the width have changed
    count();
  }

  return rows.back();
}

uint64_t JournalView::rowOf(size_t index)
{
  return (filter.columns & JOURNAL_COLUMN_WRAP) ? (total(), rows[std::min(index, lines.size())]) : index;
}

size_t JournalView::lineOf(uint64_t row)
{
  return (filter.columns & JOURNAL_COLUMN_WRAP) ? (total(), (std::upper_bound(rows.begin(), rows.end(), row) - rows.begin() - 1)) : row;
}

size_t JournalView::focus()
{
  if (anchor != NO_SELECTION)
  {
    // The line the selection ends on
    return (cursor > first) ? (cursor - first) : 0;
  }

  // The last line on the screen, which is the newest one while following
  return (total() > 0) ? lineOf(std::min<uint64_t>(delta.y + size.y, total()) - 1) : lines.size();
}

void JournalView::format(uint64_t realtime, char* date, char* clock)
{
  time_t second;
  struct tm moment;

  second = realtime / 1000000;

  if (filter.columns & JOURNAL_COLUMN_UTC) gmtime_r(&second, &moment);
  else                                     localtime_r(&second, &moment);

  strftime(date, 16, "%F", &moment);

  if (filter.columns & JOURNAL_COLUMN_MILLISECOND)
  {
    snprintf(clock, 16, "%02d:%02d:%02d.%03d", moment.tm_hour, moment.tm_min, moment.tm_sec, (int)((realtime % 1000000) / 1000));
    return;
  }

  snprintf(clock, 16, "%02d:%02d:%02d", moment.tm_hour, moment.tm_min, moment.tm_sec);
}

void JournalView::trim()
{
  uint64_t oldest;
  uint64_t before;

  oldest = first;
  before = follow ? 0 : total();

  while ((lines.size() > capacity) ||
         ((bytes > BYTE_CAPACITY) && (lines.size() > 1)))
  {
    // The oldest go first, rows are counted again
    bytes -= GetLineSize(lines.front());
    lines.pop_front();
    layout = 0;
    first ++;
  }

  if ((!follow) &&
      (first != oldest))
  {
    // The rows on the screen stay where they are while paused, the ones gone themselves leave the oldest on top
    moving = true;
    setLimit(limit.x, total());
    scrollTo(delta.x, std::max<int64_t>(0, (int64_t)delta.y - (int64_t)(before - total())));
    moving = false;
  }
}

void JournalView::clear()
{
  first += lines.size();
  width  = 0;
  unseen = 0;
  layout = 0;
  anchor = NO_SELECTION;
  cursor = NO_SELECTION;

  lines.clear();
  bytes = 0;
  rows.clear();
  memset(counts, 0, sizeof(counts));
  memset(widths, 0, sizeof(widths));
  refresh();
}

void JournalView::pause()
{
  follow = !follow;
  unseen = 0;
  refresh();
}

void JournalView::refresh()
{
  moving = true;
  setLimit((filter.columns & JOURNAL_COLUMN_WRAP) ? 0 : (indent() + width), total());

  if (follow)
  {
    // Stay on the tail
    scrollTo((filter.columns & JOURNAL_COLUMN_WRAP) ? 0 : delta.x, std::max<int64_t>(0, (int64_t)total() - size.y));
  }

  // Scroll bars tell the view about their values by a broadcast to the window, which misses a view not inserted yet
  scrollDraw();

  moving = false;
  drawView();

  if (header != nullptr)
  {
    // Columns may have changed or grown
    header->drawView();
  }

  if (TProgram::statusLine != nullptr)
  {
    // Follow state and counters of the focused window live in the status line hint
    TProgram::statusLine->drawView();
  }
}

void JournalView::select(int64_t index, bool extend)
{
  if (!lines.empty())
  {
    index  = std::clamp<int64_t>(index, 0, (int64_t)lines.size() - 1);
    cursor = first + index;
    anchor = (extend && (anchor != NO_SELECTION)) ? anchor : cursor;

    // Selection must not run away with the tail
    follow = false;
    reveal(index);
    refresh();
  }
}

void JournalView::reveal(size_t index)
{
  uint64_t row;

  row    = rowOf(index);
  moving = true;

  if (row < (uint64_t)delta.y)                 scrollTo(delta.x, row);
  if (row >= (uint64_t)(delta.y + size.y))     scrollTo(delta.x, row - size.y + 1);

  moving = false;
}

void JournalView::copy()
{
  size_t index;
  size_t limit;
  std::string text;

  if (anchor != NO_SELECTION)
  {
    index = std::max(std::min(anchor, cursor), first) - first;
    limit = std::min<uint64_t>(std::max(anchor, cursor) + 1 - first, lines.size());
  }
  else
  {
    // Nothing selected, take the lines on the screen
    index = std::min<size_t>(lineOf(delta.y), lines.size());
    limit = (total() > 0) ? (lineOf(std::min<uint64_t>(delta.y + size.y, total()) - 1) + 1) : 0;
  }

  for ( ; index < limit; index ++)
  {
    // Full lines with the columns of the window regardless of its width
    text += compose(lines[index]);
    text += '\n';
  }

  if (!text.empty())
  {
    // A command of the user or the clipboard of Turbo Vision, either may block for a while
    CopyClipboardText(text);
  }
}

void JournalView::save()
{
  char path[MAXPATH];
  size_t index;
  size_t limit;
  TFileDialog* dialog;
  std::ofstream file;

  dialog = new TFileDialog("*.log", "Export lines", "~N~ame", fdOKButton, 2);

  if (TProgram::deskTop->execView(dialog) != cmCancel)
  {
    dialog->getFileName(path);
    file.open(path);

    // Selected lines when there are some, all of them otherwise
    index = (anchor != NO_SELECTION) ? (std::max(std::min(anchor, cursor), first) - first) : 0;
    limit = (anchor != NO_SELECTION) ? std::min<uint64_t>(std::max(anchor, cursor) + 1 - first, lines.size()) : lines.size();

    for ( ; file.is_open() && (index < limit); index ++)
    {
      // Columns of the window, the way Copy gives them
      file << compose(lines[index]) << '\n';
    }

    if ((!file.is_open()) ||
        (!file.good()))
    {
      messageBox(mfError | mfOKButton, "Cannot write the lines to %s", path);
    }
  }

  TObject::destroy(dialog);
}

void JournalView::details()
{
  int field;
  int chosen;
  ushort result;
  size_t index;
  size_t equal;
  TRect bounds;
  PageDialog* dialog;
  std::string name;
  std::string value;
  JournalFilter other;
  std::vector<std::string> fields;
  std::vector<std::string> shown;

  if ((index = focus()) >= lines.size())
  {
    // Nothing to show
    return;
  }

  if ((lines[index].cursor.empty()) ||
      ((fields = ReadJournalEntry(filter, lines[index].cursor)).empty()))
  {
    // Rotated away meanwhile
    messageBox("The entry is not in the journal anymore", mfInformation | mfOKButton);
    return;
  }

  for (const std::string& line : fields)
  {
    // Escape sequences of a terminal would be drawn as glyphs, a filter takes the value as the journal has it
    shown.push_back(TakeJournalEscapes(line));
  }

  bounds = TProgram::deskTop->getExtent();
  bounds.grow(-2, -1);
  dialog = new PageDialog(bounds, "Entry", shown, true);
  result = TProgram::deskTop->execView(dialog);
  chosen = dialog->chosen();

  TObject::destroy(dialog);

  if ((result != cmPageFilter) ||
      (chosen < 0))
  {
    // Closed
    return;
  }

  if ((fields[chosen].compare(0, 2, "__") == 0) ||
      (!CheckJournalMatch(fields[chosen])))
  {
    // Addresses of the entry and values the details show in their own way
    messageBox("A window cannot be filtered by this field", mfInformation | mfOKButton);
    return;
  }

  equal = fields[chosen].find('=');
  name  = fields[chosen].substr(0, equal);
  value = fields[chosen].substr(equal + 1);
  other = filter;

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (name == JournalFieldNames[field])
    {
      // Source field shows in the Source dialog, a value of its own would be joined by OR, so it is replaced
      other.values[field] = value;
      break;
    }
  }

  if (name == "_PID")
  {
    other.pid = atoi(value.c_str());
  }
  else if (field == JOURNAL_FIELD_COUNT)
  {
    // Any other field as a raw match
    other.matches.push_back(fields[chosen]);
  }

  // The application makes the window, the filter of this one goes on unchanged
  message(TProgram::application, evBroadcast, cmJournalOpenFilter, &other);
}

void JournalView::find(int direction)
{
  size_t added;
  int64_t index;

  if (finder.empty())
  {
    // Nothing to look for yet
    chooseFind();
    return;
  }

  if (lines.empty())
  {
    return;
  }

  // From the selection, or from the edge of the screen the search goes away from
  index =
    (anchor != NO_SELECTION) ? (int64_t)((cursor > first) ? (cursor - first) : 0) :
    (direction > 0) ? ((int64_t)lineOf(delta.y) - 1) : (int64_t)lineOf(std::min<uint64_t>(delta.y + size.y, total()));

  for (index += direction; index < (int64_t)lines.size(); index += direction)
  {
    if (index < 0)
    {
      // Above the first line the history comes in by batches until the journal, the range or the capacity ends,
      // the tail must not take the screen meanwhile
      follow = false;

      if ((added = back()) == 0)
      {
        break;
      }

      index += added;
    }

    if (finder.match(lines[index].identifier + "[" + lines[index].pid + "]: " + lines[index].text))
    {
      remember();
      select(index, false);
      return;
    }
  }

  messageBox("Nothing more is found", mfInformation | mfOKButton);
}

void JournalView::seek(uint64_t realtime, bool nearest)
{
  size_t index;

  if (lines.empty())
  {
    return;
  }

  // Back goes here, before the history comes in and the tail lets the screen go
  remember();
  follow = false;

  while ((lines.front().realtime > realtime) &&
         (back() > 0))
  {
    // History before the moment, up to the start of the journal or the range, or the capacity
  }

  for (index = 0; (index < lines.size() - 1) && (lines[index].realtime < realtime); index ++)
  {
    // The first line at the moment or after it, the last one when the moment is still to come
  }

  if (nearest &&
      (index > 0) &&
      ((realtime - lines[index - 1].realtime) < (lines[index].realtime - realtime)))
  {
    // The one just before is closer to the moment
    index --;
  }

  select(index, false);
}

void JournalView::remember()
{
  places.push_back({ first + ((total() > 0) ? lineOf(delta.y) : 0), anchor, cursor, follow });

  if (places.size() > PLACE_DEPTH)
  {
    // The oldest jump is forgotten
    places.erase(places.begin());
  }
}

void JournalView::restore()
{
  JournalPlace place;

  while (!places.empty())
  {
    place = places.back();
    places.pop_back();

    if ((place.top < first) ||
        (place.top >= (first + lines.size())))
    {
      // Trimmed away meanwhile
      continue;
    }

    if ((place.anchor != NO_SELECTION) &&
        (std::min(place.anchor, place.cursor) < first))
    {
      // The selection has lost its beginning
      place.anchor = NO_SELECTION;
      place.cursor = NO_SELECTION;
    }

    anchor = place.anchor;
    cursor = place.cursor;
    follow = place.follow;
    unseen = 0;
    moving = true;
    scrollTo(delta.x, rowOf(place.top - first));
    moving = false;

    // Following goes to the tail again
    refresh();
    return;
  }
}

void JournalView::synchronize()
{
  size_t index;
  JournalMoment moment;

  if ((index = focus()) >= lines.size())
  {
    // Nothing to take the time from
    return;
  }

  // The line the others go to stays marked here
  select(index, false);

  moment.source   = this;
  moment.realtime = lines[index].realtime;

  message(TProgram::deskTop, evBroadcast, cmJournalSyncTo, &moment);
}

const char* JournalView::describe()
{
  char buffer[96];
  int length;
  uint64_t lower;
  uint64_t upper;

  length = snprintf(buffer, sizeof(buffer), unseen ? "%s +%llu" : "%s", pending ? "LOADING" : (follow ? "FOLLOW" : "PAUSED"), (unsigned long long)unseen);

  if (filter.columns & JOURNAL_COLUMN_WRAP)
  {
    // Nothing else on the screen tells wrapped lines from long ones cut at the edge
    length += snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 WRAP");
  }

  if (anchor != NO_SELECTION)
  {
    lower = std::max(std::min(anchor, cursor), first);
    upper = std::min<uint64_t>(std::max(anchor, cursor) + 1, first + lines.size());
    snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 %llu selected", (unsigned long long)((upper > lower) ? (upper - lower) : 0));
  }

  status = buffer;
  return status.c_str();
}

const char* JournalView::name()
{
  // Window title shows the filter
  caption = GetJournalFilterName(filter);
  return caption.c_str();
}

std::string JournalView::compose(const JournalLine& line)
{
  char date[16];
  char clock[16];
  char field[JOURNAL_VALUE_LENGTH * 2 + 8];
  std::string text;

  if (line.continued)
  {
    // Under the message of the entry, as journalctl puts it
    return std::string(indent(), ' ') + line.text;
  }

  format(line.realtime, date, clock);

  if (filter.columns & JOURNAL_COLUMN_DATE)
  {
    text += date;
    text += ' ';
  }

  if (filter.columns & JOURNAL_COLUMN_TIME)
  {
    text += clock;
    text += ' ';
  }

  if (filter.columns & JOURNAL_COLUMN_LEVEL)
  {
    text += tags[line.priority];
    text += ' ';
  }

  if (filter.columns & JOURNAL_COLUMN_HOST)
  {
    snprintf(field, sizeof(field), "%-*.*s ", widths[0], widths[0], line.host.c_str());
    text += field;
  }

  if (filter.columns & JOURNAL_COLUMN_UNIT)
  {
    snprintf(field, sizeof(field), "%-*.*s ", widths[1], widths[1], line.unit.c_str());
    text += field;
  }

  if (filter.columns & (JOURNAL_COLUMN_IDENTIFIER | JOURNAL_COLUMN_PID))
  {
    snprintf(field, sizeof(field), "%-*.*s ", GetSourceWidth(widths, filter.columns), GetSourceWidth(widths, filter.columns), GetSource(line, filter.columns).c_str());
    text += field;
  }

  text += line.text;
  return text;
}

std::string JournalView::heading()
{
  int width;
  char field[JOURNAL_VALUE_LENGTH];
  std::string text;

  // Same columns and widths as compose() gives the lines, a title longer than its column is cut to it,
  // UTC goes to the first of the time columns
  if (filter.columns & JOURNAL_COLUMN_DATE)
  {
    text += (filter.columns & JOURNAL_COLUMN_UTC) ? "Date UTC   " : "Date       ";
  }

  if (filter.columns & JOURNAL_COLUMN_TIME)
  {
    width = (filter.columns & JOURNAL_COLUMN_MILLISECOND) ? 12 : 8;
    snprintf(field, sizeof(field), "%-*s ", width, ((filter.columns & JOURNAL_COLUMN_UTC) && !(filter.columns & JOURNAL_COLUMN_DATE)) ? "Time UTC" : "Time");
    text += field;
  }

  if (filter.columns & JOURNAL_COLUMN_LEVEL)
  {
    text += "Level  ";
  }

  if (filter.columns & JOURNAL_COLUMN_HOST)
  {
    snprintf(field, sizeof(field), "%-*.*s ", widths[0], widths[0], "Host");
    text += field;
  }

  if (filter.columns & JOURNAL_COLUMN_UNIT)
  {
    snprintf(field, sizeof(field), "%-*.*s ", widths[1], widths[1], "Unit");
    text += field;
  }

  if (filter.columns & (JOURNAL_COLUMN_IDENTIFIER | JOURNAL_COLUMN_PID))
  {
    width = GetSourceWidth(widths, filter.columns);
    snprintf(field, sizeof(field), "%-*.*s ", width, width, (filter.columns & JOURNAL_COLUMN_IDENTIFIER) ? "Identifier" : "PID");
    text += field;
  }

  text += "Message";
  return text;
}

void JournalView::chooseLevels()
{
  ushort value;

  value = filter.levels;

  if (TProgram::application->executeDialog(new LevelsDialog(), &value) == cmOK)
  {
    filter.levels = value;
    open();
  }
}

void JournalView::chooseColumns()
{
  ushort value;

  value = filter.columns;

  if (TProgram::application->executeDialog(new ColumnsDialog(), &value) == cmOK)
  {
    // Lines keep every field, nothing to read again
    filter.columns = value;
    layout         = 0;
    arrange();
    refresh();
  }
}

void JournalView::arrange()
{
  TRect bounds;

  if (header == nullptr)
  {
    // Not in a window yet
    return;
  }

  bounds     = getBounds();
  bounds.a.y = header->origin.y + ((filter.columns & JOURNAL_COLUMN_HEADLESS) ? 0 : 1);

  if (filter.columns & JOURNAL_COLUMN_HEADLESS)
  {
    header->hide();
  }
  else
  {
    header->show();
  }

  locate(bounds);
}

void JournalView::chooseText()
{
  TextData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.text, filter.text.c_str(), sizeof(data.text) - 1);
  data.options = filter.mode;

  if (TProgram::application->executeDialog(new TextDialog("Text", "~T~ext of the entries to show", true), &data) == cmOK)
  {
    filter.text = data.text;
    filter.mode = data.options;
    open();
  }
}

void JournalView::chooseFind()
{
  TextData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.text, finder.getText().c_str(), sizeof(data.text) - 1);
  data.options = finder.getMode();

  if (TProgram::application->executeDialog(new TextDialog("Find", "~F~ind", false), &data) == cmOK)
  {
    // Checked by the dialog already
    finder.set(data.text, data.options);
    drawView();

    if (!finder.empty())
    {
      // The newest one from the selection or the bottom of the screen, then on into the history
      find(-1);
    }
  }
}

void JournalView::chooseMoment()
{
  size_t index;
  time_t second;
  uint64_t stamp;
  struct tm date;
  char text[64];

  text[0] = '\0';

  if ((index = focus()) < lines.size())
  {
    // The time of the line at hand to start from
    second = lines[index].realtime / 1000000;
    localtime_r(&second, &date);
    strftime(text, sizeof(text), "%F %T", &date);
  }

  while (inputBox("Go to time", "~T~ime", text, sizeof(text) - 1) == cmOK)
  {
    if (ParseJournalTime(text, &stamp))
    {
      seek(stamp, false);
      return;
    }

    // Back to the box with what has been typed
    messageBox("Unknown time format", mfError | mfOKButton);
  }
}

void JournalView::chooseHighlight()
{
  std::string error;
  const char* label;
  PickDialog* dialog;
  std::shared_ptr<JournalHighlighter> chosen;
  std::map<std::string, std::string> values;  // Label to name
  std::map<std::string, std::string>::iterator item;

  JournalHighlighter::list(values);
  dialog = new PickDialog("Highlight", values);

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    chosen = JournalHighlighter::get(item->second, &error);

    if (!error.empty())
    {
      messageBox(mfError | mfOKButton, "Cannot read the rules, %s", error.c_str());
    }
    else
    {
      // Lines keep their text, nothing to read again
      filter.highlight = item->second;
      highlighter      = chosen;
      drawView();
    }
  }

  TObject::destroy(dialog);
}

void JournalView::chooseSource()
{
  int field;
  SourceData data;

  memset(&data, 0, sizeof(data));

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    // Empty stands for any
    strncpy(data.values[field], filter.values[field].c_str(), JOURNAL_VALUE_LENGTH - 1);
  }

  if (filter.pid != 0)
  {
    snprintf(data.pid, sizeof(data.pid), "%d", filter.pid);
  }

  if (TProgram::application->executeDialog(new SourceDialog(filter), &data) == cmOK)
  {
    for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
    {
      filter.values[field] = data.values[field];
    }

    if ((!filter.values[JOURNAL_FIELD_UNIT].empty()) &&
        (filter.values[JOURNAL_FIELD_UNIT].find('.') == std::string::npos))
    {
      // Like systemctl and journalctl do
      filter.values[JOURNAL_FIELD_UNIT] += ".service";
    }

    filter.pid = atoi(data.pid);
    open();
  }
}

void JournalView::chooseJournals()
{
  JournalsData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.directory, filter.directory.c_str(), sizeof(data.directory) - 1);
  data.scope = !filter.directory.empty() ? JOURNALS_DIRECTORY : (filter.merge ? JOURNALS_ALL : JOURNALS_THIS);

  if (TProgram::application->executeDialog(new JournalsDialog(), &data) == cmOK)
  {
    // A directory is checked by the dialog already, the query starts over
    filter.merge     = data.scope == JOURNALS_ALL;
    filter.directory = (data.scope == JOURNALS_DIRECTORY) ? data.directory : "";
    open();
  }
}

void JournalView::chooseTime()
{
  bool valid;
  ushort result;
  uint64_t stamp;
  TimeData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.since, filter.since.c_str(), sizeof(data.since) - 1);
  strncpy(data.until, filter.until.c_str(), sizeof(data.until) - 1);

  do
  {
    if ((result = TProgram::application->executeDialog(new TimeDialog(), &data)) != cmOK)
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

  filter.since = data.since;
  filter.until = data.until;
  open();
}
