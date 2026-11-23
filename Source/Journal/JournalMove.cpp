// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// JournalView moved through: the rows of the lines, the order, Find, a moment, the cursor and its history

#include "JournalViewPrivate.h"

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
    // Rows in front of every line
    rows[index + 1] = rows[index] + height(index);
  }
}

size_t JournalView::shown(size_t index)
{
  size_t step;

  if ((!only) ||
      (index >= lines.size()) ||
      (marks.count(first + index) != 0))
  {
    return index;
  }

  for (step = 1; step < lines.size(); step ++)
  {
    // The nearest marked line, the later one first
    if ((index + step < lines.size()) && (marks.count(first + index + step) != 0)) return index + step;
    if ((index >= step) && (marks.count(first + index - step) != 0))             return index - step;
  }

  // Nothing is marked, the cursor stays where it is
  return index;
}

void JournalView::restrict()
{
  uint64_t number;

  if ((!only) &&
      (ranging()) &&
      !(marking && unmark))
  {
    // The selection becomes the marks it shows: a range of Shift or of a stroke goes to them as M would put it
    for (number = std::min(anchor, cursor); number <= std::max(anchor, cursor); number ++)
    {
      marks.insert(number);
    }
  }

  marking = false;
  unmark  = false;
  only    = !only;
  layout  = 0;

  if (only)
  {
    // What comes is counted as in a pause, the lines shown stay where they are
    follow = false;
  }

  if (!lines.empty())
  {
    // The cursor on its line, on the nearest one shown when that one is hidden
    select((anchor != NO_SELECTION) ? ((cursor > first) ? (cursor - first) : 0) : latest(), false);
  }

  refresh();

  if (owner != nullptr)
  {
    // The title tells it by ◇ and ◆
    static_cast<TWindow*>(owner)->frame->drawView();
  }
}

bool JournalView::markedOnly() const
{
  return only;
}

const char* JournalView::shows()
{
  // A diamond as a mark, empty for all the lines, filled for the marked ones only
  return only ? "\xe2\x97\x86" : "\xe2\x97\x87";
}

bool JournalView::rowed()
{
  return (filter.columns & JOURNAL_COLUMN_WRAP) || only;
}

int JournalView::height(size_t index)
{
  if (only &&
      (marks.count(first + index) == 0))
  {
    // Marked only passes it by
    return 0;
  }

  // An empty message still takes a row, a wrapped one as many as it needs
  return (filter.columns & JOURNAL_COLUMN_WRAP) ? std::max(1, (strwidth(lines[index].text) + layout - 1) / layout) : 1;
}

uint64_t JournalView::total()
{
  if (!rowed())
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
  return rowed() ? (total(), rows[std::min(index, lines.size())]) : index;
}

size_t JournalView::lineOf(uint64_t row)
{
  return rowed() ? (total(), (std::upper_bound(rows.begin(), rows.end(), row) - rows.begin() - 1)) : row;
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
  size_t entry;
  int64_t row;
  size_t index;
  size_t start;
  size_t block;
  std::set<uint64_t> chosen;
  std::vector<size_t> starts;
  std::vector<size_t> numbers;  // New index of every line
  std::vector<size_t> bottoms;  // New index of the last line of the entry of every line
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

  // The entries go the other way, the lines of every one stay in their order; the top line of the screen and the row
  // of the cursor on it are taken while the rows still tell them
  top   = (total() > 0) ? lineOf(delta.y) : 0;
  row   = -1;
  entry = 0;

  if ((anchor != NO_SELECTION) &&
      (cursor >= first) &&
      (cursor < first + lines.size()))
  {
    // The entry of the cursor and the last row of it on the screen
    block = std::upper_bound(starts.begin(), starts.end(), cursor - first) - starts.begin();
    entry = starts[block - 1];
    row   = (int64_t)rowOf((block < starts.size()) ? starts[block] : lines.size()) - 1 - delta.y;
  }

  numbers.resize(lines.size());
  bottoms.resize(lines.size());

  for (block = starts.size(); block > 0; block --)
  {
    start = starts[block - 1];
    end   = (block < starts.size()) ? starts[block] : lines.size();

    for (index = start; index < end; index ++)
    {
      numbers[index] = other.size();
      other.push_back(std::move(lines[index]));
    }

    for (index = start; index < end; index ++)
    {
      bottoms[index] = other.size() - 1;
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

  for (index = tail; index < head; index ++)
  {
    remap(trail[index % JOURNAL_PLACE_DEPTH], numbers, bottoms);
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
    // Paused, the lines of the screen stay on it the other way round: the entry of the cursor on the screen goes to the
    // rows mirrored from its own with the entries around it, the selection and the marks near it; without one the entry
    // of the top line goes to the bottom whole
    moving = true;
    setLimit(limit.x, total());

    if ((row >= 0) &&
        (row < size.y))
    {
      scrollTo(delta.x, std::max<int64_t>(0, (int64_t)rowOf(numbers[entry]) - (size.y - 1 - row)));
    }
    else
    {
      scrollTo(delta.x, std::max<int64_t>(0, (int64_t)rowOf(bottoms[top] + 1) - size.y));
    }

    moving = false;

    if (anchor != NO_SELECTION)
    {
      reveal(cursor - first);
    }
  }

  refresh();
}

void JournalView::remap(JournalPlace& place, const std::vector<size_t>& numbers, const std::vector<size_t>& bottoms)
{
  if ((place.top < first) ||
      (place.top >= first + numbers.size()))
  {
    // Trimmed away meanwhile, Back and Forward pass it by
    return;
  }

  // The lines of that screen the other way round as well, the entry of its top line at the bottom whole; a range goes
  // to its cursor the way the selection of the window does
  place.top = first + lineOf(std::max<int64_t>(0, (int64_t)rowOf(bottoms[place.top - first] + 1) - size.y));

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

bool JournalView::search(const std::string& text, uint16_t mode, bool older)
{
  if (!finder.set(text, mode))
  {
    return false;
  }

  if ((anchor == NO_SELECTION) &&
      (focus() < lines.size()))
  {
    // From the entry of the menu, not from the edge of the screen where that very entry may be found
    select(focus(), false);
  }

  // Highlighted as a find of the user is, Ctrl-N and Ctrl-P go on with it
  drawView();
  return find((older != reversed()) ? -1 : 1, false);
}

void JournalView::jump(uint64_t realtime)
{
  seek(realtime, false);
}

bool JournalView::reach(const std::string& cursor, uint64_t realtime)
{
  settle();
  seek(realtime, true);

  for (size_t index = 0; index < lines.size(); index ++)
  {
    if ((!lines[index].continued) &&
        (lines[index].cursor == cursor))
    {
      // The first line of the entry itself
      select(index, false);
      return true;
    }
  }

  return false;
}

void JournalView::reopen()
{
  size_t index;
  uint64_t realtime;
  std::string place;

  // Ctrl-Z goes back to the filter before and to the place there
  remember();

  // Following goes on following; otherwise the selection, or the newest line on the screen while paused
  index = follow ? lines.size() : focus();

  if (index >= lines.size())
  {
    open();
    return;
  }

  place    = lines[index].cursor;
  realtime = lines[index].realtime;

  if (place.empty())
  {
    open();
    return;
  }

  // Around it, a wider filter may have more entries after it than the window has room for
  open(realtime);
  land(place, realtime);
}

void JournalView::land(const std::string& entry, uint64_t realtime)
{
  // The entry itself when the filter has kept it, the nearest one in time when it has left it out; the lines around it
  // are there already, reading on to the end is up to the polls
  landing = true;
  seek(realtime, true);
  landing = false;

  for (size_t index = 0; index < lines.size(); index ++)
  {
    if ((!lines[index].continued) &&
        (lines[index].cursor == entry))
    {
      select(index, false);
      return;
    }
  }
}

bool JournalView::find(int direction, bool tell)
{
  size_t added;
  int64_t index;

  if (finder.empty())
  {
    // Nothing to look for yet
    chooseFind();
    return false;
  }

  if (lines.empty())
  {
    return false;
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
      return true;
    }
  }

  if (tell)
  {
    messageBox("Nothing more is found", mfInformation | mfOKButton);
  }

  return false;
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
      (lines[index].realtime >= realtime) &&
      ((realtime - lines[index - 1].realtime) < (lines[index].realtime - realtime)))
  {
    // The entry just before is closer to the moment, on its first line; a moment after the last line stays on it
    for (index --; (index > 0) && (lines[index].continued); index --)
    {
      // Up to the first line of the entry
    }
  }

  select(index, false);
}

JournalPlace JournalView::here()
{
  size_t index;
  JournalPlace place;

  place.top        = first + ((total() > 0) ? lineOf(delta.y) : 0);
  place.anchor     = anchor;
  place.cursor     = cursor;
  place.follow     = follow;
  place.generation = generation;
  place.filter     = applied;
  place.realtime   = 0;

  // The entry of the selection, of the top of the screen without one
  index = (anchor != NO_SELECTION) ? focus() : ((total() > 0) ? lineOf(delta.y) : lines.size());

  if (index < lines.size())
  {
    place.entry    = lines[index].cursor;
    place.realtime = lines[index].realtime;
  }

  return place;
}

void JournalView::remember()
{
  if (landing)
  {
    // A part of the jump remembered already
    return;
  }

  // The places after the step are gone and the step takes where the window is; the place the jump lands on comes next,
  // it is taken anew when the window leaves it
  step = (head > tail) ? step : head;
  head = step + 1;

  trail[step % JOURNAL_PLACE_DEPTH] = here();
  trail[head % JOURNAL_PLACE_DEPTH] = here();

  step = head ++;
  tail = std::max(tail, head - std::min<size_t>(head, JOURNAL_PLACE_DEPTH));
}

void JournalView::restore()
{
  travel(-1);
}

void JournalView::advance()
{
  travel(1);
}

void JournalView::travel(int direction)
{
  size_t next;
  JournalPlace place;

  if (head == tail)
  {
    // No jump yet
    return;
  }

  // The place of the step is where the window is now, it may have moved since it came there
  trail[step % JOURNAL_PLACE_DEPTH] = here();

  next = step;

  while ((direction < 0) ? (next > tail) : (next + 1 < head))
  {
    // One place farther, the bound is checked first so that the numbers never go below the tail
    next  = (direction < 0) ? (next - 1) : (next + 1);
    place = trail[next % JOURNAL_PLACE_DEPTH];

    if ((place.generation != generation) &&
        (place.filter != nullptr))
    {
      // Of a filter changed since: the window takes it again and goes to the entry, the nearest one in time when it is
      // gone; following goes on following
      step   = next;
      filter = *place.filter;
      follow = place.follow;

      if ((place.follow) ||
          (place.entry.empty()))
      {
        open();
        return;
      }

      open(place.realtime);
      land(place.entry, place.realtime);
      return;
    }

    if ((place.top < first) ||
        (place.top >= (first + lines.size())))
    {
      // Trimmed away meanwhile, passed by
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

    step    = next;
    anchor  = place.anchor;
    cursor  = place.cursor;
    marking = false;
    follow  = place.follow;
    unseen  = 0;
    moving = true;
    scrollTo(delta.x, rowOf(place.top - first));
    moving = false;

    if (anchor != NO_SELECTION)
    {
      // A window of another size since then, a clone or a tiled one, still shows the cursor
      reveal(cursor - first);
    }

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
