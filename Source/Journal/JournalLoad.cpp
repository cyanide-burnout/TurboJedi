// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// JournalView read: the journal opened, the entries as lines, the history brought in front, what a window keeps

#include "JournalViewPrivate.h"

static size_t GetLineSize(const JournalLine& line)
{
  // Near enough to tell a few huge messages from many short ones
  return sizeof(JournalLine) + line.host.size() + line.unit.size() + line.identifier.size() + line.pid.size() + line.cursor.size() + line.text.size() + line.tints.size() * sizeof(JournalTint);
}

void JournalView::open(uint64_t at)
{
  int result;
  char* cursor;
  uint64_t stamp;
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
  wanted  = 0;
  past.clear();

  // A layout file may bring a broken expression, it goes as plain text then
  RepairJournalTexts(filter);

  // The filter of the lines to come, the places of the ones before go by their entries and filters
  applied = std::make_shared<const JournalFilter>(filter);
  chain.set(filter);

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

  if (at != 0)
  {
    // Around an entry far from the end, paused: the oldest lines stay, what comes after the room is counted
    follow = false;
  }

  if ((since != 0) &&
      (at <= since))
  {
    // Range starts at its time instead of the last entries
    sd_journal_seek_realtime_usec(journal, since);
  }
  else
  {
    // History first: the cursor lands on the oldest of the last entries, which next() would step over,
    // a step back is needed even without history, next() from the bare tail finds nothing at all; around an entry the
    // history is the one before it
    if (at != 0)         sd_journal_seek_realtime_usec(journal, ((until != 0) && (at > until)) ? (until + 1) : at);
    else if (until != 0) sd_journal_seek_realtime_usec(journal, until + 1);
    else                 sd_journal_seek_tail(journal);

    if (sd_journal_previous_skip(journal, ((history > 0) && chain.empty()) ? history : 1) <= 0)
    {
      // Nothing of the filter yet, an identifier that has written nothing for one: libsystemd never finds anything
      // after a bare tail, from the head the first entry to come is next
      (since != 0) ? sd_journal_seek_realtime_usec(journal, since) : sd_journal_seek_head(journal);
    }
    else if ((since != 0) &&
             (sd_journal_get_realtime_usec(journal, &stamp) >= 0) &&
             (stamp < since))
    {
      // The history before the entry is not all in the range
      sd_journal_seek_realtime_usec(journal, since);
    }
    else if (history > 0)
    {
      wanted = history;

      if (parse(journal, model, buffer))
      {
        // Show the entry the cursor is on before moving forward
        split(model, buffer, parts);
        push(parts);
        wanted --;
      }

      if ((!chain.empty()) &&
          (sd_journal_get_cursor(journal, &cursor) >= 0))
      {
        // A filter done on this side: the window follows from the last entry, its history is looked for back from it,
        // as much as there is time for now and the rest by steps of the polls
        past = cursor;
        free(cursor);
      }
      else
      {
        // The journal has stepped back over the history already
        wanted = 0;
      }
    }
  }

  read();
  dig(GetQueryMilliseconds() + HISTORY_SCAN);
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

size_t JournalView::back(int64_t deadline, int* entries)
{
  int count;
  bool ended;
  bool fresh;
  size_t size;
  int64_t stop;
  char* cursor;
  std::string from;
  uint64_t realtime;
  uint64_t before;
  sd_journal* source;
  JournalLine model;
  std::vector<JournalLine> parts;
  std::vector<JournalLine> batch;

  // On from the oldest entry looked at, the oldest line otherwise
  from = (!past.empty()) ? past : ((!lines.empty()) ? edge(false).cursor : std::string());

  if ((!older) ||
      (from.empty()) ||
      (OpenJournalHistory(&source, filter) < 0))
  {
    return 0;
  }

  // A journal of its own, the one of the window keeps following the tail, seeking lands on the entry looked at, the next
  // steps go older; one the history does not match lands on the one before, not looked at yet
  ended = (sd_journal_seek_cursor(source, from.c_str()) < 0) || (sd_journal_previous(source) <= 0);
  fresh = (!ended) && (sd_journal_test_cursor(source, from.c_str()) <= 0);

  // A filter done on this side counts the entries that pass it: without a deadline the step goes on until it has found
  // one, a rare one may be far back
  stop = (deadline != 0) ? deadline : (GetQueryMilliseconds() + HISTORY_SCAN);

  for (count = 0, size = bytes; (!ended) && (count < HISTORY_STEP) && ((lines.size() + batch.size()) < capacity) && (size < BYTE_CAPACITY) && ((GetQueryMilliseconds() < stop) || ((deadline == 0) && (count == 0))); )
  {
    if (fresh)
    {
      fresh = false;
    }
    else if (sd_journal_previous(source) <= 0)
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
      count ++;

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

  if ((!ended) &&
      (sd_journal_get_cursor(source, &cursor) >= 0))
  {
    past = cursor;
    free(cursor);
  }

  if (entries != nullptr)
  {
    *entries = count;
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
  std::string line;

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

  if (!chain.empty())
  {
    // What the journal cannot apply, the texts look at the syslog style source as well
    line = model.identifier + "[" + model.pid + "]: " + message;

    if (!chain.pass(source, &line))
    {
      return false;
    }
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

      if (rowed() &&
          (layout == span()) &&
          (rows.size() == lines.size()))
      {
        // Counted rows go on without counting them all again
        rows.push_back(rows.back() + height(lines.size() - 1));
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

void JournalView::trim()
{
  bool dropped;
  uint64_t number;
  uint64_t before;

  dropped = false;
  number  = first;
  before = follow ? 0 : total();

  while ((lines.size() > capacity) ||
         ((bytes > BYTE_CAPACITY) && (lines.size() > 1)))
  {
    // The oldest go first, from the bottom with the newest on top; rows are counted again
    bytes  -= GetLineSize(edge(false));
    layout  = 0;
    dropped = true;

    if (reversed())
    {
      lines.pop_back();
      continue;
    }

    lines.pop_front();
    first ++;
  }

  if (dropped)
  {
    // The history goes on from the oldest line kept
    past.clear();
    wanted = 0;
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

  // The numbers of the newest on top go on in front, marks of the lines gone could get them again; the places of the
  // jumps stay, of lines gone they go by their entries and filters
  marks.clear();
  lines.clear();
  generation ++;
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
    // The cursor goes on with the tail, also when following has come back by Space or by scrolling to it; Marked only
    // keeps it on the newest line shown
    anchor = first + shown(reversed() ? 0 : (lines.size() - 1));
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

void JournalView::dig(int64_t deadline)
{
  int entries;

  while ((wanted > 0) &&
         (older) &&
         (GetQueryMilliseconds() < deadline))
  {
    if ((back(deadline, &entries) == 0) &&
        (GetQueryMilliseconds() < deadline))
    {
      // The journal, the range or the room of the window has ended
      wanted = 0;
      break;
    }

    // The history is counted in entries, a message of several lines is one
    wanted -= std::min(entries, wanted);
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

bool JournalView::settle(int64_t deadline)
{
  if (journal == nullptr)
  {
    return true;
  }

  // A watch of a plugin or a caller of the bus may have read the entry before the window: what has come is read in first
  sd_journal_process(journal);

  do
  {
    read();
  }
  while ((pending) &&
         (GetQueryMilliseconds() < deadline));

  return !pending;
}

bool JournalView::bring(uint64_t oldest, int64_t deadline)
{
  while (GetQueryMilliseconds() < deadline)
  {
    if (lines.empty() || (edge(false).realtime <= oldest) || (back() == 0))
    {
      // There, or the journal, the range or the capacity has ended
      return true;
    }
  }

  return false;
}

uint64_t JournalView::earliest()
{
  return lines.empty() ? 0 : edge(false).realtime;
}
