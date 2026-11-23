// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalChain.h"

#include <string.h>

// JournalChain

bool JournalChain::set(const JournalFilter& filter)
{
  bool whole;
  size_t equal;

  whole = true;

  texts.clear();
  exclusions.clear();
  presences.clear();

  for (const std::string& match : filter.matches)
  {
    if (IsJournalPresence(match))
    {
      // FIELD=* has the field, FIELD!=* has it not
      equal = match.find('=');
      presences.emplace_back(match.substr(0, IsJournalExclusion(match) ? (equal - 1) : equal), !IsJournalExclusion(match));
      continue;
    }

    if (IsJournalExclusion(match))
    {
      // FIELD!=VALUE is compared as the journal gives the data, FIELD=VALUE
      equal = match.find('=');
      exclusions.emplace_back(match.substr(0, equal - 1), match.substr(0, equal - 1) + match.substr(equal));
    }
  }

  for (const JournalText& text : filter.texts)
  {
    if (text.text.empty())
    {
      // Passes anything
      continue;
    }

    texts.emplace_back();
    texts.back().exclude = (text.mode & JOURNAL_TEXT_EXCLUDE) != 0;

    if (!texts.back().matcher.set(text.text, text.mode))
    {
      texts.back().matcher.set(text.text, text.mode & ~JOURNAL_TEXT_EXPRESSION);
      whole = false;
    }
  }

  return whole;
}

bool JournalChain::empty() const
{
  return texts.empty() && exclusions.empty() && presences.empty();
}

bool JournalChain::pass(sd_journal* journal, const std::string* line)
{
  size_t length;
  const void* data;
  std::string text;
  char identifier[256];
  char pid[32];

  for (const std::pair<std::string, std::string>& exclusion : exclusions)
  {
    if ((sd_journal_get_data(journal, exclusion.first.c_str(), &data, &length) >= 0) &&
        (length == exclusion.second.size()) &&
        (memcmp(data, exclusion.second.data(), length) == 0))
    {
      // A value left out
      return false;
    }
  }

  for (const std::pair<std::string, bool>& presence : presences)
  {
    if ((sd_journal_get_data(journal, presence.first.c_str(), &data, &length) >= 0) != presence.second)
    {
      // A field there that has not to be, or one missing
      return false;
    }
  }

  if (texts.empty())
  {
    return true;
  }

  if (line == nullptr)
  {
    // As a window matches its text
    text = (GetJournalField(journal, "SYSLOG_IDENTIFIER", identifier, sizeof(identifier)) || GetJournalField(journal, "_COMM", identifier, sizeof(identifier))) ? identifier : "?";
    text += "[";
    text += GetJournalField(journal, "_PID", pid, sizeof(pid)) ? pid : "";
    text += "]: ";

    if (sd_journal_get_data(journal, "MESSAGE", &data, &length) >= 0)
    {
      text.append(static_cast<const char*>(data) + 8, length - 8);
    }

    line = &text;
  }

  for (Text& item : texts)
  {
    if (item.matcher.match(*line) == item.exclude)
    {
      return false;
    }
  }

  return true;
}

void RepairJournalTexts(JournalFilter& filter)
{
  JournalExpression expression;

  for (JournalText& text : filter.texts)
  {
    if ((text.mode & JOURNAL_TEXT_EXPRESSION) &&
        (!expression.compile(text.text, !(text.mode & JOURNAL_TEXT_SENSITIVE))))
    {
      text.mode &= ~JOURNAL_TEXT_EXPRESSION;
    }
  }
}
