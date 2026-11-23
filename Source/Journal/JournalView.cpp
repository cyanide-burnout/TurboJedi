// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// The view of the lines of a journal: made and closed, the keys and the mouse, the place among the windows

#include "JournalViewPrivate.h"

// JournalView

// The identities of the windows only grow, a closed one is never taken for another
static unsigned identities;

JournalView::JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalFilter& filter, int history, size_t capacity) :
  TScroller(bounds, horizontal, vertical)
{
  identity = ++ identities;

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
  only     = false;

  open();
}

JournalView::JournalView(const TRect& bounds, TScrollBar* horizontal, TScrollBar* vertical, const JournalView& source) :
  TScroller(bounds, horizontal, vertical)
{
  size_t index;

  identity = ++ identities;

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
  only        = source.only;
  marks       = source.marks;
  tail        = source.tail;
  head        = source.head;
  step        = source.step;

  for (index = tail; index < head; index ++)
  {
    // Only the places there are, the rest of the ring has never been written
    trail[index % JOURNAL_PLACE_DEPTH] = source.trail[index % JOURNAL_PLACE_DEPTH];
  }

  growMode  = gfGrowHiX | gfGrowHiY;
  options  |= ofFirstClick;
  journal   = nullptr;
  notifier  = nullptr;
  blind     = false;
  header    = nullptr;
  layout    = 0;
  moving    = false;

  // Expressions are compiled again, they are not shared
  chain.set(filter);
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
  // Watches of plugins that go to their entries here take another window
  ForgetPluginWindow(this);

  if (journal != nullptr)
  {
    // Window has been closed, the copy of inotify goes first
    ReleaseJournalSource(notifier);
    sd_journal_close(journal);
  }
}

void JournalView::handleEvent(TEvent& event)
{
  int64_t index;
  uint64_t row;
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

    if ((event.keyDown.keyCode == kbAltLeft) ||
        (event.keyDown.keyCode == kbAltBack))
    {
      // Back the way browsers have it as well, Ctrl-Z of the menu is the main one
      restore();
      clearEvent(event);
      return;
    }

    if (event.keyDown.keyCode == kbAltRight)
    {
      // Forward the way browsers have it, Ctrl-Y of the menu is the main one
      advance();
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

    if ((event.keyDown.charScan.charCode == 'a') ||
        (event.keyDown.charScan.charCode == 'A'))
    {
      // Actions by a plain key as M marks: Terminal of macOS takes F10 and F11, Option is not Alt there by default
      actions(TPoint { -1, -1 });
      clearEvent(event);
      return;
    }

    if ((event.keyDown.keyCode == kbEsc) &&
        (anchor != NO_SELECTION))
    {
      // The selection and the marks go, the cursor stays on its line
      anchor  = cursor;
      marking = false;
      unmark  = false;
      marks.clear();
      layout  = 0;  // Marked only shows the lines of the marks
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

      // The cursor comes on the row of the window nearest to the newest end, the newest line while following
      index = (anchor == NO_SELECTION) ? (int64_t)latest() : (int64_t)((cursor > first) ? (cursor - first) : 0);

      if (only)
      {
        // Marked only moves over the lines shown, by their rows: a hidden one has none
        row = rowOf(shown(index));

        switch (event.keyDown.keyCode)
        {
          case kbUp:   row = (row > 0) ? (row - 1) : 0;                                   break;
          case kbDown: row = rowOf(shown(index) + 1);                                    break;
          case kbPgUp: row = (row > (uint64_t)size.y) ? (row - size.y) : 0;             break;
          case kbPgDn: row = row + size.y;                                               break;
          case kbHome: row = 0;                                                          break;
          case kbEnd:  row = (total() > 0) ? (total() - 1) : 0;                          break;
        }

        index = (total() > 0) ? (int64_t)lineOf(std::min<uint64_t>(row, total() - 1)) : index;
      }
      else
      {
        switch (event.keyDown.keyCode)
        {
          case kbUp:   index -= 1;                         break;
          case kbDown: index += 1;                         break;
          case kbPgUp: index -= size.y;                    break;
          case kbPgDn: index += size.y;                    break;
          case kbHome: index  = 0;                         break;
          case kbEnd:  index  = (int64_t)lines.size() - 1; break;
        }
      }

      walk(index, extend);
      clearEvent(event);
      return;
    }
  }

  TScroller::handleEvent(event);

  if ((event.what == evMouseDown) &&
      (event.mouse.buttons & mbRightButton) &&
      (!lines.empty()))
  {
    // Actions of the entry under the mouse, the cursor goes there first as a click puts it
    walk(lineOf(delta.y + makeLocal(event.mouse.where).y), marking);
    actions(event.mouse.where);
    clearEvent(event);
    return;
  }

  if ((event.what == evMouseDown) &&
      (event.mouse.buttons & mbLeftButton))
  {
    if (event.mouse.eventFlags & meDoubleClick)
    {
      // Double click opens the entry under the mouse, a stroke of M goes on to it
      walk(lineOf(delta.y + makeLocal(event.mouse.where).y), marking);
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
      walk(lineOf(std::max<int64_t>(0, delta.y + makeLocal(event.mouse.where).y)), false);

      if ((anchor != NO_SELECTION) &&
          (marks.erase(cursor) == 0))
      {
        marks.insert(cursor);
      }

      layout = 0;
      refresh();
      clearEvent(event);
      return;
    }

    // Click puts the cursor on a row and the marks stay as the keys leave them, Shift+click and dragging extend,
    // dragging past an edge scrolls; in a stroke of M a click goes on with it as the keys do; Esc drops the marks
    extend = ((event.mouse.controlKeyState & kbShift) != 0) || marking;

    // The click is a step of the history, dragging after it is not
    walk(lineOf(std::max<int64_t>(0, delta.y + makeLocal(event.mouse.where).y)), extend);

    while (mouseEvent(event, evMouseMove | evMouseAuto))
    {
      select(lineOf(std::max<int64_t>(0, delta.y + makeLocal(event.mouse.where).y)), true);
    }

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
        case cmJournalActions:       actions(TPoint { -1, -1 }); break;
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

        case cmJournalMarked:
          restrict();
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

void JournalView::setState(ushort state, Boolean enable)
{
  TScroller::setState(state, enable);

  if (state & sfActive)
  {
    // The window has come to the front or left it, the cursor is drawn only there
    drawView();
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

std::string JournalView::current()
{
  size_t index;

  // The entry Enter and the actions take
  return ((index = focus()) < lines.size()) ? lines[index].cursor : std::string();
}

bool JournalView::following() const
{
  return follow;
}

unsigned JournalView::id() const
{
  return identity;
}
