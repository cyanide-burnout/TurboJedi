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

static std::string GetSign(bool follow, uint64_t unseen)
{
  // Following plays on, a pause tells what has come meanwhile
  return follow ? "\xe2\x96\xba" : (unseen ? ("\xe2\x80\x96 +" + std::to_string(unseen)) : "\xe2\x80\x96");
}

static void KeepPlace(std::vector<JournalPlace>& list, const JournalPlace& place)
{
  list.push_back(place);

  if (list.size() > PLACE_DEPTH)
  {
    // The farthest one is forgotten
    list.erase(list.begin());
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
  blind       = false;
  header   = nullptr;
  since    = 0;
  until    = 0;
  closed   = false;
  pending  = false;
  held     = false;
  counted  = 0;
  older    = false;
  layout   = 0;
  first    = FIRST_LINE;
  width    = 0;
  unseen   = 0;
  follow   = true;
  moving   = false;
  anchor   = NO_SELECTION;
  cursor   = NO_SELECTION;
  marking  = false;
  unmark   = false;

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
  held        = false;
  counted     = 0;
  older       = source.older;
  unseen      = source.unseen - source.counted;
  follow      = source.follow;
  anchor      = source.anchor;
  cursor      = source.cursor;
  marking     = source.marking;
  unmark      = source.unmark;
  marks       = source.marks;
  places      = source.places;
  ahead       = source.ahead;

  growMode  = gfGrowHiX | gfGrowHiY;
  options  |= ofFirstClick;
  journal   = nullptr;
  notifier  = nullptr;
  blind     = false;
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
    attach();

    if ((source.held) &&
        (!lines.empty()))
    {
      // The source has only counted the entries after its last line, the clone counts them anew
      SeekJournalCursor(journal, edge(true).cursor.c_str());
    }
    else if (!SeekJournalPlace(journal, source.journal))
    {
      // Nothing has been read there yet, the tail the way open() takes it without history
      SeekJournalEnd(journal);
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
  bool extra;
  bool picks;
  size_t index;
  uint64_t row;
  uint64_t chunk;
  char date[16];
  char clock[16];
  std::string source;
  std::vector<std::pair<size_t, size_t>> ranges;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr marked;
  TColorAttr bar;
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

  // The cursor tells whether its line is a part of a selection
  extra = selected() > 0;

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

      picks  = picked(first + index);
      chosen = picks || ((anchor != NO_SELECTION) && (cursor == first + index));
      bar    = marked;

      if ((anchor != NO_SELECTION) &&
          (cursor == first + index))
      {
        // The cursor is a cyan bar as in a panel of mc, on a selected line its text is white, the selection keeps its own bar
        bar = (picks && extra) ? TColorAttr(TColorBIOS(0xf), TColorBIOS(0x3)) : TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));
      }

      if (chosen)
      {
        // Selected rows look like a selection in any other scroller
        buffer.moveChar(0, ' ', bar, size.x);
        color = bar;
      }

      x    = 0;
      head = (chunk == 0) && (!line->continued);

      if (head)
      {
        // Columns go on the first row of an entry only, wrapped rows and the next lines of a message go on under it
        format(line->realtime, date, clock);

        if (filter.columns & JOURNAL_COLUMN_DATE)
        {
          buffer.moveStr(x, date, chosen ? bar : stamp);
        }

        if (filter.columns & JOURNAL_COLUMN_TIME)
        {
          buffer.moveStr(x + ((filter.columns & JOURNAL_COLUMN_DATE) ? 11 : 0), clock, chosen ? bar : stamp);
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
        buffer.moveStr(x, head ? line->host : std::string(), chosen ? bar : normal, widths[0]);
        x += widths[0] + 1;
      }

      if (filter.columns & JOURNAL_COLUMN_UNIT)
      {
        buffer.moveStr(x, head ? line->unit : std::string(), chosen ? bar : normal, widths[1]);
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
  uint64_t number;
  uint64_t unread;
  bool extend;
  bool waiting;
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

    if ((event.keyDown.keyCode == kbIns) ||
        (event.keyDown.charScan.charCode == 'm') ||
        (event.keyDown.charScan.charCode == 'M'))
    {
      mark();
      clearEvent(event);
      return;
    }

    if ((event.keyDown.keyCode == kbEsc) &&
        (anchor != NO_SELECTION))
    {
      anchor  = NO_SELECTION;
      cursor  = NO_SELECTION;
      marking = false;
      unmark  = false;
      marks.clear();
      refresh();
      clearEvent(event);
      return;
    }

    if ((oldest()) &&
        ((reversed()) ?
         ((event.keyDown.keyCode == kbDown) ||
          (event.keyDown.keyCode == kbPgDn) ||
          (event.keyDown.keyCode == kbEnd)  ||
          (event.keyDown.keyCode == kbCtrlEnd) ||
          (event.keyDown.keyCode == kbCtrlPgDn)) :
         ((event.keyDown.keyCode == kbUp)   ||
          (event.keyDown.keyCode == kbPgUp) ||
          (event.keyDown.keyCode == kbHome) ||
          (event.keyDown.keyCode == kbCtrlHome) ||
          (event.keyDown.keyCode == kbCtrlPgUp))))
    {
      // Past the oldest line comes the history, above the first one or under the last one with the newest on top;
      // the key itself is handled as usual afterwards
      back();
    }

    if ((event.keyDown.keyCode == kbUp)   ||
        (event.keyDown.keyCode == kbDown) ||
        (event.keyDown.keyCode == kbPgUp) ||
        (event.keyDown.keyCode == kbPgDn) ||
        (event.keyDown.keyCode == kbHome) ||
        (event.keyDown.keyCode == kbEnd))
    {
      // The keys move the cursor, a line selected by itself, Shift or a mark extends the selection from it
      extend = ((event.keyDown.controlKeyState & kbShift) != 0) || marking;

      if (anchor == NO_SELECTION)
      {
        // The cursor comes on the row of the window nearest to the newest end, the newest line while following
        select(latest(), false);
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

      select(index, extend);
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
      // Double click opens the entry under the mouse, a stroke of M goes on to it
      select(lineOf(delta.y + makeLocal(event.mouse.where).y), marking);
      details();
      clearEvent(event);
      return;
    }

    if ((marking) &&
        (event.mouse.controlKeyState & kbCtrlShift))
    {
      // Ctrl+click is a mark of its own, the stroke ends first the way M ends it
      mark();
    }

    if (event.mouse.controlKeyState & kbCtrlShift)
    {
      // Ctrl+click puts the cursor on a line and marks it or takes its mark off, the other marks stay
      select(lineOf(std::max<int64_t>(0, delta.y + makeLocal(event.mouse.where).y)), false);

      if ((anchor != NO_SELECTION) &&
          (marks.erase(cursor) == 0))
      {
        marks.insert(cursor);
      }

      refresh();
      clearEvent(event);
      return;
    }

    // Click puts the cursor on a row and the marks stay as the keys leave them, Shift+click and dragging extend,
    // dragging past an edge scrolls; in a stroke of M a click goes on with it as the keys do; Esc drops the marks
    extend = ((event.mouse.controlKeyState & kbShift) != 0) || marking;

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
        case cmJournalForward:       advance();        break;
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

        case cmJournalReverse:
          reverse();
          break;

        default:
          return;
      }
      clearEvent(event);
      break;

    case evBroadcast:
      if ((event.message.command == cmJournalPoll) &&
          (journal != nullptr) &&
          (blind) &&
          ((CheckJournalFiles(filter.directory, files)) || (faults != GetJournalFaults())) &&
          (ReopenJournal(&journal, filter, lines.empty() ? std::string() : edge(true).cursor)))
      {
        // A rotation on a file system without inotify, or its files gone bad under the journal after a reconnect: opened again
        // after the last line, the entries only counted are counted anew
        ReleaseJournalSource(notifier);
        attach();
        unseen -= counted;
        counted = 0;
      }

      if ((event.message.command == cmJournalPoll) &&
          (journal != nullptr) &&
          (pending || (sd_journal_process(journal) != SD_JOURNAL_NOP) || blind || (held && follow)))
      {
        // Never clear the poll, every window has to get it; held entries come once the window follows again.
        // The newest on top come in front, the first number tells them as the count tells the ones at the end
        number  = first;
        count   = lines.size();
        waiting = held;
        unread  = unseen;
        read();

        if ((pending) ||
            (held != waiting) ||
            (unseen != unread) ||
            (first != number) ||
            (lines.size() != count))
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
        // Scrolling away from the tail pauses, scrolling back to it resumes; the tail is on top with the newest there
        follow  = reversed() ? (delta.y == 0) : (delta.y >= limit.y - size.y);
        unseen *= !follow;

        if (oldest())
        {
          // Scrolled to the oldest line, the history comes past it
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

void JournalView::attach()
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
  held    = false;
  counted = 0;
  older   = true;

  // Lines of another query
  places.clear();
  ahead.clear();

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
    parts.assign(1, model);
    push(parts);
    refresh();
    return;
  }

  attach();

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

    if (sd_journal_previous_skip(journal, (history > 0) ? history : 1) <= 0)
    {
      // Nothing of the filter yet, an identifier that has written nothing for one: libsystemd never finds anything
      // after a bare tail, from the head the first entry to come is next
      sd_journal_seek_head(journal);
    }
    else if ((history > 0) &&
             (parse(journal, model, buffer)))
    {
      // Show the entry the cursor is on before moving forward
      split(model, buffer, parts);
      push(parts);
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
  uint64_t before;
  uint64_t realtime;
  JournalLine model;
  std::vector<JournalLine> parts;

  if ((held) &&
      (follow))
  {
    // Following again: the entries only counted meanwhile are read for real after the last line kept
    held    = false;
    counted = 0;

    if ((!lines.empty()) &&
        (!edge(true).cursor.empty()))
    {
      SeekJournalCursor(journal, edge(true).cursor.c_str());
    }
  }

  // The newest on top come in front of the rows on the screen
  before = (reversed() && !follow) ? total() : 0;

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

      // Paused, an entry whose lines do not go in whole is counted, and so is everything after it to keep the order:
      // the oldest lines and their marks stay, the entries come in for real when the window follows again
      held = held || ((!follow) && (!fits(parts)));

      if (held)
      {
        // Counted the way push() counts the lines, the counts of the levels get the entry when it is read for real
        counts[model.priority] --;
        unseen  += parts.size();
        counted += parts.size();
        continue;
      }

      push(parts);
    }
  }

  if ((reversed()) &&
      (!follow) &&
      (total() != before))
  {
    // Paused with the newest on top, the rows on the screen stay where they are under the ones come in front of them
    moving = true;
    setLimit(limit.x, total());
    scrollTo(delta.x, delta.y + (total() - before));
    moving = false;
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
      (edge(false).cursor.empty()) ||
      (OpenJournalQuery(&source, filter) < 0))
  {
    return 0;
  }

  // A journal of its own, the one of the window keeps following the tail,
  // seeking lands on the entry of the oldest line, the next steps go older
  ended = (sd_journal_seek_cursor(source, edge(false).cursor.c_str()) < 0) || (sd_journal_previous(source) <= 0);

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
      // Entries come newest first: under the newest on top they go on in their order, otherwise their lines go
      // in reverse so the batch can be put in front as it is
      split(model, buffer, parts);

      for (const JournalLine& part : parts)
      {
        // Bounded the way trim() bounds the tail
        size += GetLineSize(part);
      }

      if (reversed()) batch.insert(batch.end(), std::make_move_iterator(parts.begin()),  std::make_move_iterator(parts.end()));
      else            batch.insert(batch.end(), std::make_move_iterator(parts.rbegin()), std::make_move_iterator(parts.rend()));
    }
  }

  sd_journal_close(source);
  older  = !ended;
  before = total();

  for (JournalLine& line : batch)
  {
    if (reversed())
    {
      // Under the last line, the numbers go on after it
      lines.push_back(std::move(line));
      bytes += GetLineSize(lines.back());
      measure(lines.back());
      continue;
    }

    lines.push_front(std::move(line));
    bytes += GetLineSize(lines.front());
    measure(lines.front());
    first --;
  }

  if ((!batch.empty()) &&
      (reversed()))
  {
    // Nothing on the screen moves
    layout = 0;
    refresh();
  }
  else if (!batch.empty())
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

void JournalView::push(std::vector<JournalLine>& parts)
{
  size_t index;
  const JournalLine* line;

  for (index = 0; index < parts.size(); index ++)
  {
    if (reversed())
    {
      // The newest entry goes on top with its lines in their order under it, the numbers go on in front;
      // the rows are counted again
      lines.push_front(std::move(parts[parts.size() - 1 - index]));
      line   = &lines.front();
      layout = 0;
      first --;
    }
    else
    {
      lines.push_back(std::move(parts[index]));
      line = &lines.back();

      if ((filter.columns & JOURNAL_COLUMN_WRAP) &&
          (layout == span()) &&
          (rows.size() == lines.size()))
      {
        // Counted rows go on without counting them all again
        rows.push_back(rows.back() + std::max(1, (strwidth(line->text) + layout - 1) / layout));
      }
      else
      {
        layout = 0;
      }
    }

    bytes  += GetLineSize(*line);
    unseen += !follow;
    measure(*line);
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

  // The line on the screen nearest to the newest end, which is the newest one while following
  return (total() > 0) ? latest() : lines.size();
}

bool JournalView::reversed()
{
  return (filter.columns & JOURNAL_COLUMN_REVERSE) != 0;
}

const JournalLine& JournalView::edge(bool newest)
{
  return (newest != reversed()) ? lines.back() : lines.front();
}

size_t JournalView::latest()
{
  // The first row with the newest on top, the last one otherwise
  return reversed() ? lineOf(delta.y) : lineOf(std::min<uint64_t>(delta.y + size.y, total()) - 1);
}

bool JournalView::oldest()
{
  return reversed() ? ((uint64_t)(delta.y + size.y) >= total()) : (delta.y == 0);
}

void JournalView::reverse()
{
  size_t top;
  size_t end;
  size_t index;
  size_t start;
  size_t block;
  std::set<uint64_t> chosen;
  std::vector<size_t> starts;
  std::vector<size_t> numbers;  // New index of every line
  std::deque<JournalLine> other;

  // An entry is a line with the next lines of its message under it, or what trimming has left of one
  for (start = 0; start < lines.size(); start = end)
  {
    starts.push_back(start);

    for (end = start + 1; (end < lines.size()) && (lines[end].continued) && (lines[end].cursor == lines[start].cursor); end ++)
    {
      // The lines of the same message
    }
  }

  // The entries go the other way, the lines of every one stay in their order; the top line of the screen is taken
  // while the rows still tell it
  top = (total() > 0) ? lineOf(delta.y) : 0;
  numbers.resize(lines.size());

  for (block = starts.size(); block > 0; block --)
  {
    start = starts[block - 1];
    end   = (block < starts.size()) ? starts[block] : lines.size();

    for (index = start; index < end; index ++)
    {
      numbers[index] = other.size();
      other.push_back(std::move(lines[index]));
    }
  }

  for (index = 0; index < numbers.size(); index ++)
  {
    if (picked(first + index))
    {
      // A range would not stay a range when it ends inside an entry, the selection stays as marks
      chosen.insert(first + numbers[index]);
    }
  }

  if ((anchor != NO_SELECTION) &&
      (cursor >= first) &&
      (cursor < first + numbers.size()))
  {
    // The cursor stays on its line, a stroke of M ends there as M would end it
    cursor = first + numbers[cursor - first];
    anchor = cursor;
  }
  else
  {
    anchor = NO_SELECTION;
    cursor = NO_SELECTION;
  }

  marks   = std::move(chosen);
  marking = false;
  unmark  = false;
  lines   = std::move(other);
  layout  = 0;

  for (JournalPlace& place : places)
  {
    remap(place, numbers);
  }

  for (JournalPlace& place : ahead)
  {
    remap(place, numbers);
  }

  filter.columns ^= JOURNAL_COLUMN_REVERSE;

  if (owner != nullptr)
  {
    // The title tells the order
    static_cast<TWindow*>(owner)->frame->drawView();
  }

  if ((!follow) &&
      (!lines.empty()))
  {
    // Paused, the lines of the screen stay on it the other way round: the top one goes to the bottom, the cursor stays seen
    moving = true;
    setLimit(limit.x, total());
    scrollTo(delta.x, std::max<int64_t>(0, (int64_t)rowOf(numbers[top]) - size.y + 1));
    moving = false;

    if (anchor != NO_SELECTION)
    {
      reveal(cursor - first);
    }
  }

  refresh();
}

void JournalView::remap(JournalPlace& place, const std::vector<size_t>& numbers)
{
  if ((place.top < first) ||
      (place.top >= first + numbers.size()))
  {
    // Trimmed away meanwhile, Back and Forward pass it by
    return;
  }

  // The lines of that screen the other way round as well, its top line at the bottom; a range goes to its cursor
  // the way the selection of the window does
  place.top = first + lineOf(std::max<int64_t>(0, (int64_t)rowOf(numbers[place.top - first]) - size.y + 1));

  if ((place.anchor != NO_SELECTION) &&
      (place.cursor >= first) &&
      (place.cursor < first + numbers.size()))
  {
    place.cursor = first + numbers[place.cursor - first];
    place.anchor = place.cursor;
    return;
  }

  place.anchor = NO_SELECTION;
  place.cursor = NO_SELECTION;
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
  uint64_t number;
  uint64_t before;

  number = first;
  before = follow ? 0 : total();

  while ((lines.size() > capacity) ||
         ((bytes > BYTE_CAPACITY) && (lines.size() > 1)))
  {
    // The oldest go first, from the bottom with the newest on top; rows are counted again
    bytes -= GetLineSize(edge(false));
    layout = 0;

    if (reversed())
    {
      lines.pop_back();
      continue;
    }

    lines.pop_front();
    first ++;
  }

  // Marks of the lines gone go with them
  marks.erase(marks.begin(), marks.lower_bound(first));
  marks.erase(marks.lower_bound(first + lines.size()), marks.end());

  if ((!follow) &&
      (first != number))
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
  first  += lines.size();
  width   = 0;
  unseen  = 0;
  counted = 0;
  held    = false;
  layout  = 0;
  anchor  = NO_SELECTION;
  cursor  = NO_SELECTION;
  marking = false;
  unmark  = false;

  // The numbers of the newest on top go on in front, places of the lines gone could get them again
  marks.clear();
  places.clear();
  ahead.clear();
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

  if ((follow) &&
      (!marking) &&
      (anchor == cursor) &&
      (anchor != NO_SELECTION) &&
      (!lines.empty()))
  {
    // The cursor goes on with the tail, also when following has come back by Space or by scrolling to it
    anchor = first + (reversed() ? 0 : (lines.size() - 1));
    cursor = anchor;
  }

  if (follow)
  {
    // Stay on the tail, on top with the newest there
    scrollTo((filter.columns & JOURNAL_COLUMN_WRAP) ? 0 : delta.x, reversed() ? 0 : std::max<int64_t>(0, (int64_t)total() - size.y));
  }

  // Scroll bars tell the view about their values by a broadcast to the window, which misses a view not inserted yet
  scrollDraw();

  moving = false;
  drawView();

  if ((owner != nullptr) &&
      (badge != GetSign(follow, unseen)))
  {
    // The title takes the sign of following or of the pause anew
    static_cast<TWindow*>(owner)->frame->drawView();
  }

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

bool JournalView::fits(const std::vector<JournalLine>& parts)
{
  size_t size;

  size = 0;

  for (const JournalLine& part : parts)
  {
    size += GetLineSize(part);
  }

  // An entry larger than the whole window goes into an empty one anyway, it could never be seen otherwise
  return lines.empty() || ((lines.size() + parts.size() <= capacity) && (bytes + size <= BYTE_CAPACITY));
}

void JournalView::select(int64_t index, bool extend)
{
  if (!lines.empty())
  {
    index  = std::clamp<int64_t>(index, 0, (int64_t)lines.size() - 1);
    cursor = first + index;
    anchor = (extend && (anchor != NO_SELECTION)) ? anchor : cursor;

    // The cursor on the newest line follows the tail, anywhere else or a selection of more lines does not run away with it
    follow  = (anchor == cursor) && (index == (reversed() ? 0 : ((int64_t)lines.size() - 1)));
    unseen *= !follow;
    reveal(index);
    refresh();
  }
}

void JournalView::mark()
{
  bool begin;
  bool was;
  uint64_t number;

  if (anchor == NO_SELECTION)
  {
    // The cursor comes on the row nearest to the newest end, as the keys bring it
    select(latest(), false);
  }

  if (anchor == NO_SELECTION)
  {
    // No lines at all
    return;
  }

  begin = !marking;
  was   = marks.count(cursor) != 0;

  if ((!begin) &&
      (unmark))
  {
    // A stroke begun on a marked line takes the marks off the lines it has passed, M twice there takes off one
    marks.erase(marks.lower_bound(std::min(anchor, cursor)), marks.upper_bound(std::max(anchor, cursor)));
  }
  else if (ranging())
  {
    // The stroke, or a range made by Shift before it, goes to the marks
    for (number = std::min(anchor, cursor); number <= std::max(anchor, cursor); number ++)
    {
      marks.insert(number);
    }
  }

  // The first M begins a stroke on the cursor, the second one ends it and its lines stay marked, or unmarked when it has begun
  // on a marked line; marking pauses
  marking = begin;
  unmark  = begin && was;
  anchor  = cursor;
  follow  = follow && !begin;
  refresh();
}

bool JournalView::ranging()
{
  return (anchor != NO_SELECTION) && (marking || (anchor != cursor));
}

bool JournalView::picked(uint64_t number)
{
  if (ranging() &&
      (number >= std::min(anchor, cursor)) &&
      (number <= std::max(anchor, cursor)))
  {
    // The range adds its lines, a stroke that takes marks off leaves them out
    return !(marking && unmark);
  }

  return marks.count(number) != 0;
}

uint64_t JournalView::selected()
{
  bool range;
  uint64_t lower;
  uint64_t upper;
  uint64_t amount;

  // The range and the marks out of it, within the lines there are; a stroke that takes marks off counts none of its lines
  range  = ranging();
  lower  = range ? std::max(std::min(anchor, cursor), first) : 0;
  upper  = range ? std::min<uint64_t>(std::max(anchor, cursor) + 1, first + lines.size()) : 0;
  amount = ((upper > lower) && !(marking && unmark)) ? (upper - lower) : 0;

  for (auto mark = marks.lower_bound(first); (mark != marks.end()) && (*mark < first + lines.size()); mark ++)
  {
    amount += !(range && (*mark >= lower) && (*mark < upper));
  }

  return amount;
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

  if (selected() > 0)
  {
    // The selection in the order of the lines, marks need not be next to each other
    for (index = 0; index < lines.size(); index ++)
    {
      text += picked(first + index) ? (compose(lines[index]) + '\n') : std::string();
    }
  }
  else
  {
    if ((anchor != NO_SELECTION) &&
        (cursor >= first))
    {
      // The line under the cursor
      index = std::min<size_t>(cursor - first, lines.size());
      limit = std::min<size_t>(index + 1, lines.size());
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
  bool whole;
  size_t index;
  TFileDialog* dialog;
  std::ofstream file;

  dialog = new TFileDialog("*.log", "Export lines", "~N~ame", fdOKButton, 2);

  if (TProgram::deskTop->execView(dialog) != cmCancel)
  {
    dialog->getFileName(path);
    file.open(path);

    // Selected lines when there are some, all of them otherwise; the cursor alone is no selection
    whole = selected() == 0;

    for (index = 0; file.is_open() && (index < lines.size()); index ++)
    {
      if (whole || picked(first + index))
      {
        // Columns of the window, the way Copy gives them
        file << compose(lines[index]) << '\n';
      }
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

  for (index += direction; ; index += direction)
  {
    if ((index < 0) ||
        (index >= (int64_t)lines.size()))
    {
      // Past the oldest line, above the first one or under the last one with the newest on top, the history comes in
      // by batches until the journal, the range or the capacity ends, the tail must not take the screen meanwhile;
      // past the newest there is nothing more
      if ((index < 0) == reversed())
      {
        break;
      }

      follow = false;

      if ((added = back()) == 0)
      {
        break;
      }

      // Lines in front move the ones there are
      index += reversed() ? 0 : added;
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
  size_t later;

  if (lines.empty())
  {
    return;
  }

  // Back goes here, before the history comes in and the tail lets the screen go
  remember();
  follow = false;

  while ((edge(false).realtime > realtime) &&
         (back() > 0))
  {
    // History before the moment, up to the start of the journal or the range, or the capacity
  }

  if (reversed())
  {
    // From the bottom up the time goes on: the entry at the moment or after it, the newest one when the moment is still
    // to come, on its first line
    for (index = lines.size() - 1; (index > 0) && (lines[index].realtime < realtime); index --)
    {
      // Older than the moment
    }

    for ( ; (index > 0) && (lines[index].continued); index --)
    {
      // Up to the first line of the entry
    }

    for (later = index + 1; (later < lines.size()) && (lines[later].continued); later ++)
    {
      // The entry under it is the one just before
    }

    if (nearest &&
        (later < lines.size()) &&
        (lines[index].realtime >= realtime) &&
        ((realtime - lines[later].realtime) < (lines[index].realtime - realtime)))
    {
      // The one just before is closer to the moment
      index = later;
    }

    select(index, false);
    return;
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

JournalPlace JournalView::here()
{
  return { first + ((total() > 0) ? lineOf(delta.y) : 0), anchor, cursor, follow };
}

void JournalView::remember()
{
  // A new jump is the end of the way, the places ahead of it are gone as in a browser
  KeepPlace(places, here());
  ahead.clear();
}

void JournalView::restore()
{
  travel(places, ahead);
}

void JournalView::advance()
{
  travel(ahead, places);
}

void JournalView::travel(std::vector<JournalPlace>& from, std::vector<JournalPlace>& to)
{
  JournalPlace place;

  while (!from.empty())
  {
    place = from.back();
    from.pop_back();

    if ((place.top < first) ||
        (place.top >= (first + lines.size())))
    {
      // Trimmed away meanwhile
      continue;
    }

    if ((place.anchor != NO_SELECTION) &&
        ((std::min(place.anchor, place.cursor) < first) ||
         (std::max(place.anchor, place.cursor) >= (first + lines.size()))))
    {
      // The selection has lost a part of it
      place.anchor = NO_SELECTION;
      place.cursor = NO_SELECTION;
    }

    // Where the window is now is the way back from there
    KeepPlace(to, here());

    anchor  = place.anchor;
    cursor  = place.cursor;
    marking = false;
    follow  = place.follow;
    unseen  = 0;
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

  length = snprintf(buffer, sizeof(buffer), unseen ? "%s +%llu" : "%s", pending ? "LOADING" : (follow ? "FOLLOW" : "PAUSED"), (unsigned long long)unseen);

  if (held)
  {
    // New entries are counted, not kept, until the window follows
    length += snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 FULL");
  }

  if (filter.columns & JOURNAL_COLUMN_WRAP)
  {
    // Nothing else on the screen tells wrapped lines from long ones cut at the edge
    length += snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 WRAP");
  }

  if (marking)
  {
    // The keys extend the selection, or take marks off from a marked line on
    length += snprintf(buffer + length, sizeof(buffer) - length, unmark ? " \xe2\x94\x82 UNMARK" : " \xe2\x94\x82 MARK");
  }

  if (anchor != NO_SELECTION)
  {
    // The cursor alone counts as the one line Copy takes
    snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 %llu selected", (unsigned long long)std::max<uint64_t>(selected(), 1));
  }

  status = buffer;
  return status.c_str();
}

const char* JournalView::sign()
{
  // Every window tells whether it follows and what has come while it is paused, without going there
  badge = GetSign(follow, unseen);
  return badge.c_str();
}

const char* JournalView::order()
{
  // Like the order of a column of a table by time, rising or falling
  return reversed() ? "\xe2\x96\xbc" : "\xe2\x96\xb2";
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
    // Lines keep every field, nothing to read again; the order is changed by turning the lines round
    filter.columns = (value & ~JOURNAL_COLUMN_REVERSE) | (filter.columns & JOURNAL_COLUMN_REVERSE);
    layout         = 0;
    arrange();

    if ((value ^ filter.columns) & JOURNAL_COLUMN_REVERSE)
    {
      reverse();
    }

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
      // The newest one from the selection or the newest end of the screen, then on into the history
      find(reversed() ? 1 : -1);
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
