// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// JournalView marked: the selection, the marks, copy and save, the Actions menu and the details of an entry

#include "JournalViewPrivate.h"

void JournalView::select(int64_t index, bool extend)
{
  if (!lines.empty())
  {
    index  = shown(std::clamp<int64_t>(index, 0, (int64_t)lines.size() - 1));
    cursor = first + index;
    anchor = (extend && (anchor != NO_SELECTION)) ? anchor : cursor;

    // The cursor on the newest line follows the tail, anywhere else or a selection of more lines does not run away with it
    follow  = (anchor == cursor) && (index == (reversed() ? 0 : ((int64_t)lines.size() - 1))) && !only;
    unseen *= !follow;
    reveal(index);
    refresh();
  }
}

void JournalView::walk(int64_t index, bool extend)
{
  if (lines.empty())
  {
    return;
  }

  index = std::clamp<int64_t>(index, 0, (int64_t)lines.size() - 1);

  if ((anchor == NO_SELECTION) ||
      (cursor != first + index) ||
      ((!extend) && (anchor != cursor)))
  {
    // A step of the cursor by the keys or the mouse is a place of the history as a jump is, one that goes nowhere is not
    remember();
  }

  if ((anchor == NO_SELECTION) &&
      (extend))
  {
    // A range by Shift begins where the cursor comes first
    select(latest(), false);
  }

  select(index, extend);
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

  layout = 0;

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
    // The line under the cursor, before the keys have moved it the one it is drawn on
    index = std::min<size_t>(focus(), lines.size());
    limit = std::min<size_t>(index + 1, lines.size());

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

void JournalView::actions(TPoint where)
{
  size_t index;
  int64_t row;
  bool keyboard;

  if ((index = focus()) >= lines.size())
  {
    // No entry to act on
    return;
  }

  keyboard = (where.x < 0);

  if (keyboard)
  {
    // From the keys: under the line of the cursor, or at the top when it is off the screen
    row   = (int64_t)rowOf(index) - delta.y;
    row   = ((row >= 0) && (row < size.y)) ? row : 0;
    where = makeGlobal(TPoint { 2, (int)row });
  }

  ShowActions(this, lines[index].cursor, where, keyboard);
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
    // Rotated away meanwhile, or the journal cannot be opened
    messageBox(DescribeJournalLoss(filter).c_str(), mfInformation | mfOKButton);
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

size_t JournalView::mark(const std::set<std::string>& cursors, uint64_t oldest, bool clear)
{
  settle();

  // Older entries come in by batches until the oldest asked for is there, the journal, the range or the capacity ends;
  // a long way back gets a dialog, Cancel marks what has come so far
  RunSteps("Bringing older entries into the window", [this, oldest] (int64_t deadline) { return bring(oldest, deadline); }, 0);

  return markLoaded(cursors, clear);
}

size_t JournalView::markLoaded(const std::set<std::string>& cursors, bool clear)
{
  size_t index;
  std::set<std::string> found;

  if (clear)
  {
    marks.clear();
  }

  for (index = 0; index < lines.size(); index ++)
  {
    if ((!lines[index].cursor.empty()) &&
        (cursors.count(lines[index].cursor) != 0))
    {
      marks.insert(first + index);
      found.insert(lines[index].cursor);
    }
  }

  layout = 0;

  refresh();
  return found.size();
}

std::vector<std::string> JournalView::pickedCursors(size_t limit, size_t& total)
{
  size_t step;
  size_t index;
  std::set<std::string> seen;
  std::vector<std::string> cursors;

  total = 0;

  for (step = 0; step < lines.size(); step ++)
  {
    // The oldest first, the lines of a message once
    index = reversed() ? (lines.size() - 1 - step) : step;

    if ((!picked(first + index)) ||
        (lines[index].cursor.empty()) ||
        (!seen.insert(lines[index].cursor).second))
    {
      continue;
    }

    total ++;

    if (cursors.size() < limit)
    {
      cursors.push_back(lines[index].cursor);
    }
  }

  return cursors;
}
