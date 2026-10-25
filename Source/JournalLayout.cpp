// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalLayout.h"
#include "JournalExpression.h"
#include "JournalHighlighter.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <fstream>
#include <algorithm>

// Keys of the source fields in the order of JOURNAL_FIELD_*
static const char* const fields[] = { "unit", "identifier", "command", "transport", "boot", "user", "machine" };

// Column names in the order of the JOURNAL_COLUMN_* bits
static const char* const columns[] = { "date", "time", "level", "host", "unit", "identifier", "pid", "utc", "millisecond", "wrap" };

#define COLUMN_COUNT  10

static const char letters[] = "EACRWNID";

static std::string Trim(const std::string& text)
{
  size_t first;
  size_t last;

  first = text.find_first_not_of(" \t\r");
  last  = text.find_last_not_of(" \t\r");

  return (first != std::string::npos) ? text.substr(first, last - first + 1) : std::string();
}

static bool ParseNumber(const std::string& text, long minimum, long maximum, long* value)
{
  char* end;

  errno  = 0;
  *value = strtol(text.c_str(), &end, 10);

  // Digits only, the whole text and within the range
  return (!text.empty()) && (*end == '\0') && (errno == 0) && (*value >= minimum) && (*value <= maximum);
}

static bool ParseSwitch(const std::string& text, uint16_t flag, uint16_t* mode)
{
  *mode = (text == "yes") ? (*mode | flag) : (*mode & ~flag);

  return (text == "yes") || (text == "no");
}

static const char* SetValue(JournalLayout& window, const std::string& key, const std::string& value)
{
  int index;
  int found;
  long number;
  size_t start;
  size_t end;
  uint64_t stamp;
  std::string name;

  if (key == "bounds")
  {
    for (index = 0, start = 0; index < 4; index ++)
    {
      // Four numbers apart by spaces, nothing else
      start = value.find_first_not_of(' ', start);
      end   = std::min(value.find(' ', start), value.size());

      if ((start == std::string::npos) ||
          (!ParseNumber(value.substr(start, end - start), 0, SHRT_MAX, &number)))
      {
        return "bounds takes four numbers: left, top, right and bottom";
      }

      window.bounds[index] = number;
      start = end;
    }

    return (value.find_first_not_of(' ', start) == std::string::npos) ? nullptr : "bounds takes four numbers: left, top, right and bottom";
  }

  for (index = 0; index < JOURNAL_FIELD_COUNT; index ++)
  {
    if (key == fields[index])
    {
      // Source field, the journal tells about a value it does not know by an empty window
      window.filter.values[index] = value;
      return nullptr;
    }
  }

  if (key == "pid")
  {
    if (!ParseNumber(value, 0, INT_MAX, &number))
    {
      return "pid takes a process number";
    }

    window.filter.pid = number;
    return nullptr;
  }

  if (key == "match")
  {
    window.filter.matches.push_back(value);
    return CheckJournalMatch(value) ? nullptr : "match takes FIELD=VALUE, FIELD of A-Z, 0-9 and _";
  }

  if (key == "text")
  {
    window.filter.text = value;
    return nullptr;
  }

  if (key == "regex")    return ParseSwitch(value, JOURNAL_TEXT_EXPRESSION, &window.filter.mode) ? nullptr : "regex takes yes or no";
  if (key == "exclude")  return ParseSwitch(value, JOURNAL_TEXT_EXCLUDE,    &window.filter.mode) ? nullptr : "exclude takes yes or no";
  if (key == "case")     return ParseSwitch(value, JOURNAL_TEXT_SENSITIVE,  &window.filter.mode) ? nullptr : "case takes yes or no";

  if ((key == "since") ||
      (key == "until"))
  {
    ((key == "since") ? window.filter.since : window.filter.until) = value;
    return ParseJournalTime(value, &stamp) ? nullptr : "since and until take a time the way the command line does";
  }

  if (key == "highlight")
  {
    window.filter.highlight = value;
    return ((value == JOURNAL_HIGHLIGHT_OFF) || (JournalHighlighter::get(value) != nullptr)) ? nullptr : "highlight takes builtin, off or a rule file of grc";
  }

  if (key == "levels")
  {
    // Letter of a shown level, dash of a hidden one, the way the window title shows them
    window.filter.levels = 0;

    for (index = 0; (index < 8) && (index < (int)value.size()); index ++)
    {
      if ((value[index] != letters[index]) &&
          (value[index] != '-'))
      {
        break;
      }

      window.filter.levels |= (value[index] != '-') << index;
    }

    return ((index == 8) && (value.size() == 8)) ? nullptr : "levels takes EACRWNID with dashes for hidden levels";
  }

  if (key == "columns")
  {
    // Comma separated names, none at all leaves the message alone
    window.filter.columns = 0;

    for (start = 0; (!value.empty()) && (start <= value.size()); start = end + 1)
    {
      end   = std::min(value.find(',', start), value.size());
      name  = Trim(value.substr(start, end - start));
      found = std::find(columns, columns + COLUMN_COUNT, name) - columns;

      if (found == COLUMN_COUNT)
      {
        return "columns takes date, time, level, host, unit, identifier, pid, utc, millisecond and wrap";
      }

      window.filter.columns |= 1 << found;
    }

    return nullptr;
  }

  return "unknown key";
}

static bool CheckExpression(const JournalFilter& filter)
{
  JournalExpression expression;

  // The window would take a broken one as plain text
  return expression.compile(filter.text, !(filter.mode & JOURNAL_TEXT_SENSITIVE));
}

bool LoadJournalLayout(const std::string& path, std::vector<JournalLayout>& windows, std::string& error)
{
  int number;
  size_t index;
  size_t equal;
  std::string line;
  std::vector<int> sections;
  std::ifstream file(path);
  const char* problem;
  char buffer[160];

  if (!file.is_open())
  {
    error = strerror(errno);
    return false;
  }

  windows.clear();

  for (number = 1; std::getline(file, line); number ++)
  {
    line = Trim(line);

    if (line.empty() || (line[0] == '#') || (line[0] == ';'))
    {
      // Blank line or comment
      continue;
    }

    if (line == "[window]")
    {
      // Defaults of a window that the file does not mention
      windows.emplace_back();
      sections.push_back(number);
      memset(windows.back().bounds, 0, sizeof(windows.back().bounds));
      windows.back().filter.pid       = 0;
      windows.back().filter.mode      = 0;
      windows.back().filter.levels    = 0xff;
      windows.back().filter.columns   = JOURNAL_COLUMN_DEFAULT;
      windows.back().filter.highlight = JOURNAL_HIGHLIGHT_BUILTIN;
      continue;
    }

    // Only the first equal sign separates, a raw match has one of its own
    equal   = line.find('=');
    problem =
      (line[0] == '[')               ? "unknown section" :
      (equal == std::string::npos)   ? "neither a section nor key = value" :
      windows.empty()                ? "key = value outside of a [window] section" :
      SetValue(windows.back(), Trim(line.substr(0, equal)), Trim(line.substr(equal + 1)));

    if (problem != nullptr)
    {
      snprintf(buffer, sizeof(buffer), "line %d: %s", number, problem);
      error = buffer;
      return false;
    }
  }

  for (index = 0; index < windows.size(); index ++)
  {
    if ((windows[index].filter.mode & JOURNAL_TEXT_EXPRESSION) &&
        (!CheckExpression(windows[index].filter)))
    {
      // Keys of a section come in any order, the text is checked once all of them are there
      snprintf(buffer, sizeof(buffer), "line %d: broken regular expression in text", sections[index]);
      error = buffer;
      return false;
    }
  }

  error = windows.empty() ? "no [window] sections" : "";
  return !windows.empty();
}

bool SaveJournalLayout(const std::string& path, const std::vector<JournalLayout>& windows)
{
  int index;
  bool first;
  std::ofstream file(path);

  if (!file.is_open())
  {
    return false;
  }

  for (const JournalLayout& window : windows)
  {
    file << "[window]\n";
    file << "bounds = " << window.bounds[0] << ' ' << window.bounds[1] << ' ' << window.bounds[2] << ' ' << window.bounds[3] << '\n';

    for (index = 0; index < JOURNAL_FIELD_COUNT; index ++)
    {
      if (!window.filter.values[index].empty())
      {
        // Empty stands for any
        file << fields[index] << " = " << window.filter.values[index] << '\n';
      }
    }

    if (window.filter.pid != 0)        file << "pid = " << window.filter.pid << '\n';
    if (!window.filter.text.empty())   file << "text = " << window.filter.text << '\n';
    if (window.filter.mode & JOURNAL_TEXT_EXPRESSION)  file << "regex = yes\n";
    if (window.filter.mode & JOURNAL_TEXT_EXCLUDE)     file << "exclude = yes\n";
    if (window.filter.mode & JOURNAL_TEXT_SENSITIVE)   file << "case = yes\n";
    if (!window.filter.since.empty())  file << "since = " << window.filter.since << '\n';
    if (!window.filter.until.empty())  file << "until = " << window.filter.until << '\n';

    for (const std::string& match : window.filter.matches)
    {
      // Raw FIELD=VALUE
      file << "match = " << match << '\n';
    }

    file << "levels = ";

    for (index = 0; index < 8; index ++)
    {
      file << ((window.filter.levels & (1 << index)) ? letters[index] : '-');
    }

    file << "\nhighlight = " << window.filter.highlight;
    file << "\ncolumns = ";

    for (index = 0, first = true; index < COLUMN_COUNT; index ++)
    {
      if (window.filter.columns & (1 << index))
      {
        file << (first ? "" : ",") << columns[index];
        first = false;
      }
    }

    file << "\n\n";
  }

  return file.good();
}
