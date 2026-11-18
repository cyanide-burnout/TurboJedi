// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalMatcher.h"

#include <string.h>

// JournalMatcher

JournalMatcher::JournalMatcher()
{
  mode = 0;
}

bool JournalMatcher::set(const std::string& text, uint16_t mode)
{
  JournalExpression compiled;

  if ((mode & JOURNAL_TEXT_EXPRESSION) &&
      (!text.empty()) &&
      (!compiled.compile(text, !(mode & JOURNAL_TEXT_SENSITIVE))))
  {
    // Broken expression
    return false;
  }

  pattern    = text;
  expression = std::move(compiled);
  this->mode = mode;
  return true;
}

bool JournalMatcher::empty() const
{
  return pattern.empty();
}

const std::string& JournalMatcher::getText() const
{
  return pattern;
}

uint16_t JournalMatcher::getMode() const
{
  return mode;
}

bool JournalMatcher::match(const std::string& text)
{
  if (mode & JOURNAL_TEXT_EXPRESSION)
  {
    // Anywhere in the text
    return expression.match(text);
  }

  return ((mode & JOURNAL_TEXT_SENSITIVE) ? strstr(text.c_str(), pattern.c_str()) : strcasestr(text.c_str(), pattern.c_str())) != nullptr;
}

void JournalMatcher::locate(const std::string& text, std::vector<std::pair<size_t, size_t>>& ranges)
{
  size_t offset;
  const char* start;
  const char* found;

  ranges.clear();

  if (pattern.empty())
  {
    // Nothing to look for
    return;
  }

  if (mode & JOURNAL_TEXT_EXPRESSION)
  {
    for (offset = 0; (offset <= text.size()) && expression.search(text, offset, groups); offset = JournalExpression::advance(text, groups[0].first, groups[0].second))
    {
      if (groups[0].second > groups[0].first)
      {
        // Empty matches have nothing to show
        ranges.push_back(groups[0]);
      }
    }

    return;
  }

  for (start = text.c_str(); (found = (mode & JOURNAL_TEXT_SENSITIVE) ? strstr(start, pattern.c_str()) : strcasestr(start, pattern.c_str())) != nullptr; start = found + pattern.size())
  {
    // Byte offsets of every occurrence
    ranges.emplace_back(found - text.c_str(), found - text.c_str() + pattern.size());
  }
}
