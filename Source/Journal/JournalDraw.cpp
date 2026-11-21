// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// JournalView drawn: the lines, their columns and colors, the title of the frame and the line of the status

#include "JournalViewPrivate.h"

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

int GetSourceWidth(const int* widths, uint16_t columns)
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

std::string GetSign(bool follow, uint64_t unseen)
{
  // Following plays on, a pause tells what has come meanwhile
  return follow ? "\xe2\x96\xba" : (unseen ? ("\xe2\x80\x96 +" + std::to_string(unseen)) : "\xe2\x80\x96");
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
  uint64_t current;
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

  // Before the keys have moved it the cursor is on the line Enter and the actions take, the newest one while following;
  // it is drawn only in the window in front as in a panel of mc, the selection and the marks are drawn in every window
  current = ((owner == nullptr) || !(owner->state & sfSelected)) ? NO_SELECTION : (anchor != NO_SELECTION) ? cursor : (total() > 0) ? (first + latest()) : NO_SELECTION;

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
      chosen = picks || (current == first + index);
      bar    = marked;

      if (current == first + index)
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
    else if (only &&
             (y == 0) &&
             (total() == 0))
    {
      // Marked only with nothing marked would be an empty window that tells nothing
      buffer.moveStr(1, "Nothing is marked: M, Insert or Ctrl+click marks, Ctrl-D shows all the lines again", stamp);
    }

    writeLine(0, y, size.x, 1, buffer);
  }
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

const char* JournalView::describe()
{
  char buffer[128];
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

  if (only)
  {
    // The lines between the marked ones are there, not shown
    length += snprintf(buffer + length, sizeof(buffer) - length, " \xe2\x94\x82 MARKED");
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
