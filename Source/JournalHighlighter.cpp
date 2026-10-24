// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalHighlighter.h"

#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <fstream>
#include <sstream>

// Rule files of grc, the first one of a name wins
static const char* const directories[] = { "~/.grc", "/usr/local/share/grc", "/usr/share/grc" };

// What most messages carry: times, addresses, identifiers, paths, quoted text, key=value and the words that matter
static const char builtin[] = R"RULES(
# Date and time within the message
regexp=\b\d{4}-\d{2}-\d{2}(?:[T ]\d{2}:\d{2}(?::\d{2}(?:[.,]\d+)?)?(?:Z|[+-]\d{2}:?\d{2})?)?\b
colours=magenta
-
# Key of key=value
regexp=(?<![\w.-])([A-Za-z_][\w.-]*)=(?=\S)
colours=unchanged, cyan
-
# Text in double quotes
regexp="[^"\n]*"
colours=yellow
-
# Path
regexp=(?<![\w./~:-])(?:~|\.{1,2})?/[\w.@+~-]+(?:/[\w.@+~-]*)*
colours=cyan
-
# URL
regexp=\b[a-z][a-z0-9+.-]*://[^\s"'<>()]+
colours=underline cyan
-
# UUID and other 128-bit identifiers
regexp=\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b|\b[0-9a-f]{32}\b
colours=magenta
-
# IPv4 with a port
regexp=\b(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?\b
colours=green
-
# IPv6, with at least one :: or all eight groups
regexp=(?<![\w:])(?:(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){0,6}:(?::?[0-9a-fA-F]{1,4}){0,7})(?![\w:])
colours=green
-
# MAC
regexp=\b[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}\b
colours=green
-
# Bad news
regexp=(?i)\b(?:errors?|fail(?:s|ed|ure)?|fatal|panic|denied|refused|unreachable|timed out|time-?outs?|abort(?:s|ed)?|crash(?:es|ed)?|segfault|killed|exceptions?|invalid|corrupt(?:ed)?)\b
colours=bold red
-
# Warnings
regexp=(?i)\b(?:warn(?:ing)?s?|deprecated|retry(?:ing)?|degraded)\b
colours=bold yellow
-
# Good news
regexp=(?i)\b(?:ok|success(?:ful(?:ly)?)?|succeeded|started|listening|connected|accepted|ready|done|enabled)\b
colours=bold green
)RULES";

// JournalPaint

TColorAttr JournalPaint::apply(TColorAttr base) const
{
  if (reset)
  {
    // Line color as it is
    return base;
  }

  if (foreground) base.setForeground(fore);
  if (background) base.setBackground(back);

  base.setStyle(base.getStyle() | style);
  return base;
}

// Helpers

static std::string Trim(const std::string& text)
{
  size_t first;
  size_t last;

  first = text.find_first_not_of(" \t\r");
  last  = text.find_last_not_of(" \t\r");

  return (first != std::string::npos) ? text.substr(first, last - first + 1) : std::string();
}

static std::string ExpandHome(const std::string& path)
{
  const char* home;

  return ((path[0] == '~') && ((home = getenv("HOME")) != nullptr)) ? (home + path.substr(1)) : path;
}

static JournalPaint ParsePaint(const std::string& text)
{
  // Bright foregrounds over the blue window, grc has the dark ones for a black terminal, backgrounds stay dark
  static const char* const names[] = { "black", "blue", "green", "cyan", "red", "magenta", "yellow", "white" };
  static const uint8_t lights[]    = { 0x08,    0x09,   0x0a,    0x0b,   0x0c,  0x0d,      0x0e,     0x0f    };
  static const uint8_t darks[]     = { 0x00,    0x01,   0x02,    0x03,   0x04,  0x05,      0x06,     0x07    };

  int index;
  size_t position;
  std::string word;
  std::string name;
  std::istringstream stream(text);
  JournalPaint paint {};

  while (stream >> word)
  {
    if ((word == "default") || (word == "none"))  { paint.reset = true;           continue; }
    if (word == "bold")                           { paint.style |= slBold;        continue; }
    if (word == "underline")                      { paint.style |= slUnderline;   continue; }
    if (word == "italic")                         { paint.style |= slItalic;      continue; }
    if (word == "blink")                          { paint.style |= slBlink;       continue; }
    if (word == "reverse")                        { paint.style |= slReverse;     continue; }

    if ((position = word.find("38;5;")) != std::string::npos)
    {
      // Escape sequence of 256 colors in quotes
      paint.foreground = true;
      paint.fore       = TColorXTerm(atoi(word.c_str() + position + 5));
      continue;
    }

    if ((position = word.find("48;5;")) != std::string::npos)
    {
      paint.background = true;
      paint.back       = TColorXTerm(atoi(word.c_str() + position + 5));
      continue;
    }

    // Names with on_ for the background and bright_ for the light ones, unchanged and dark change nothing
    name = (word.compare(0, 3, "on_") == 0) ? word.substr(3) : word;
    name = (name.compare(0, 7, "bright_") == 0) ? name.substr(7) : name;

    for (index = 0; index < 8; index ++)
    {
      if (name == names[index])
      {
        if (word.compare(0, 3, "on_") == 0)
        {
          paint.background = true;
          paint.back       = TColorBIOS(darks[index]);
          break;
        }

        paint.foreground = true;
        paint.fore       = TColorBIOS(lights[index]);
        break;
      }
    }
  }

  return paint;
}

// JournalHighlighter

std::shared_ptr<JournalHighlighter> JournalHighlighter::get(const std::string& name, std::string* error)
{
  static std::map<std::string, std::shared_ptr<JournalHighlighter>> cache;

  std::string path;
  std::string text;
  std::ifstream file;
  std::ostringstream content;
  std::shared_ptr<JournalHighlighter> highlighter;
  std::map<std::string, std::shared_ptr<JournalHighlighter>>::iterator item;

  if (error != nullptr)
  {
    error->clear();
  }

  if (name == JOURNAL_HIGHLIGHT_OFF)
  {
    // Nothing to paint
    return nullptr;
  }

  if ((item = cache.find(name)) != cache.end())
  {
    // Loaded by another window
    return item->second;
  }

  highlighter = std::make_shared<JournalHighlighter>();

  if (name == JOURNAL_HIGHLIGHT_BUILTIN)
  {
    highlighter->parse(builtin);
    cache.emplace(name, highlighter);
    return highlighter;
  }

  for (const char* directory : directories)
  {
    // A path of its own or a name of grc
    path = (name.find('/') != std::string::npos) ? ExpandHome(name) : (ExpandHome(directory) + "/" + name);
    file.open(path);

    if (file.is_open() ||
        (name.find('/') != std::string::npos))
    {
      break;
    }
  }

  if (!file.is_open())
  {
    if (error != nullptr)
    {
      *error = "there is no rule file " + name;
    }

    return nullptr;
  }

  content << file.rdbuf();
  highlighter->parse(content.str());
  cache.emplace(name, highlighter);

  return highlighter;
}

void JournalHighlighter::list(std::map<std::string, std::string>& values)
{
  DIR* handle;
  struct dirent* entry;

  // Parentheses sort before the names of files
  values.emplace("(built-in)", JOURNAL_HIGHLIGHT_BUILTIN);
  values.emplace("(off)",      JOURNAL_HIGHLIGHT_OFF);

  for (const char* directory : directories)
  {
    if ((handle = opendir(ExpandHome(directory).c_str())) != nullptr)
    {
      while ((entry = readdir(handle)) != nullptr)
      {
        if (strncmp(entry->d_name, "conf.", 5) == 0)
        {
          // The first directory has the name
          values.emplace(entry->d_name, entry->d_name);
        }
      }

      closedir(handle);
    }
  }
}

void JournalHighlighter::apply(const std::string& text, std::vector<JournalStroke>& strokes)
{
  size_t start;
  size_t end;
  size_t group;
  size_t offset;
  bool found;

  strokes.clear();
  marks.assign(text.size(), nullptr);

  for (Rule& rule : rules)
  {
    for (offset = 0, found = false; (offset <= text.size()) && rule.expression.search(text, offset, groups); offset = JournalExpression::advance(text, groups[0].first, groups[0].second))
    {
      for (group = 0; (group < groups.size()) && (group < rule.paints.size()); group ++)
      {
        if ((groups[group].first != std::string::npos) &&
            (rule.paints[group].reset || rule.paints[group].foreground || rule.paints[group].background || rule.paints[group].style))
        {
          // The whole match first, its groups over it, later rules over earlier ones
          std::fill(marks.begin() + groups[group].first, marks.begin() + groups[group].second, &rule.paints[group]);
        }
      }

      found = true;

      if (rule.count != COUNT_MORE)
      {
        // The first match only
        break;
      }
    }

    if (found &&
        (rule.count == COUNT_STOP))
    {
      // The rest of the rules leave the line alone
      break;
    }
  }

  for (start = 0; start < marks.size(); start = end)
  {
    for (end = start + 1; (end < marks.size()) && (marks[end] == marks[start]); end ++)
    {
      // Bytes of the same paint
    }

    if (marks[start] != nullptr)
    {
      strokes.push_back({ start, end, marks[start] });
    }
  }
}

bool JournalHighlighter::parse(const std::string& text)
{
  bool skip;
  size_t equal;
  size_t start;
  size_t end;
  std::string line;
  std::string key;
  std::string value;
  std::string pattern;
  std::string colours;
  std::string count;
  std::istringstream stream(text + "\n-\n");
  Rule rule;

  skip = false;

  while (std::getline(stream, line))
  {
    line = Trim(line);

    if (line.empty() ||
        (line[0] == '#'))
    {
      // Blank line or comment
      continue;
    }

    if ((line[0] == '-') ||
        (line[0] == '='))
    {
      // End of an entry: block and unblock span lines and replace changes the text, neither fits a window of lines
      if ((!pattern.empty()) &&
          (!skip) &&
          ((count.empty()) || (count == "more") || (count == "once") || (count == "stop")) &&
          (rule.expression.compile(pattern, false)))
      {
        rule.count = (count == "once") ? COUNT_ONCE : ((count == "stop") ? COUNT_STOP : COUNT_MORE);
        rule.paints.clear();

        for (start = 0; start <= colours.size(); start = end + 1)
        {
          // One per group, the match first
          end = std::min(colours.find(',', start), colours.size());
          rule.paints.push_back(ParsePaint(colours.substr(start, end - start)));
        }

        rules.push_back(std::move(rule));
        rule = Rule();
      }

      skip = false;
      pattern.clear();
      colours.clear();
      count.clear();
      continue;
    }

    if ((equal = line.find('=')) == std::string::npos)
    {
      // Not a key
      continue;
    }

    key   = Trim(line.substr(0, equal));
    value = line.substr(equal + 1);

    if (key == "regexp")                          pattern = value;
    if ((key == "colours") || (key == "colour"))  colours = value;
    if (key == "count")                           count   = Trim(value);
    if (key == "skip")                            skip    = (Trim(value) == "yes") || (Trim(value) == "true") || (Trim(value) == "1");
    if (key == "replace")                         skip    = true;
  }

  return !rules.empty();
}
