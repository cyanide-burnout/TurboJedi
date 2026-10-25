// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalTools.h"

#include <pwd.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#define PID_SCAN  100000  // Recent entries to learn the names of processes from

// Journal fields behind the source part of a filter
const char* const JournalFieldNames[JOURNAL_FIELD_COUNT] =
{
  "_SYSTEMD_UNIT",
  "SYSLOG_IDENTIFIER",
  "_COMM",
  "_TRANSPORT",
  "_BOOT_ID",
  "_UID",
  "_MACHINE_ID"
};

int OpenJournal(sd_journal** journal, const JournalFilter& filter)
{
  int result;

  // Other machines are other directories of /var/log/journal and /var/log/journal/remote of systemd-journal-remote
  result = filter.directory.empty() ?
    sd_journal_open(journal, filter.merge ? 0 : SD_JOURNAL_LOCAL_ONLY) :
    sd_journal_open_directory(journal, filter.directory.c_str(), 0);

  if (result >= 0)
  {
    // Fields over 64 KiB may come cut by default, the text filter, Find and the details must see the end of a message
    sd_journal_set_data_threshold(*journal, 0);
  }

  return result;
}

int OpenJournalQuery(sd_journal** journal, const JournalFilter& filter)
{
  int field;
  int level;
  int result;
  char match[JOURNAL_VALUE_LENGTH + 32];
  std::string value;

  if ((result = OpenJournal(journal, filter)) < 0)
  {
    return result;
  }

  // Matches of the same field are joined by OR, of different fields by AND
  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (!filter.values[field].empty())
    {
      // User may be given by name while the journal knows numbers only, an unknown one leaves a match nothing satisfies
      value = (field == JOURNAL_FIELD_USER) ? GetJournalUserIdentifier(filter.values[field]) : filter.values[field];
      snprintf(match, sizeof(match), "%s=%s", JournalFieldNames[field], value.empty() ? "-" : value.c_str());
      sd_journal_add_match(*journal, match, 0);
    }
  }

  if (filter.pid != 0)
  {
    snprintf(match, sizeof(match), "_PID=%d", filter.pid);
    sd_journal_add_match(*journal, match, 0);
  }

  for (const std::string& value : filter.matches)
  {
    // Raw FIELD=VALUE
    sd_journal_add_match(*journal, value.c_str(), 0);
  }

  if (filter.levels != 0xff)
  {
    // No level at all leaves a match nothing can satisfy
    sd_journal_add_match(*journal, "PRIORITY=-", 0);

    for (level = 0; level < 8; level ++)
    {
      if (filter.levels & (1 << level))
      {
        snprintf(match, sizeof(match), "PRIORITY=%d", level);
        sd_journal_add_match(*journal, match, 0);
      }
    }
  }

  return result;
}

std::vector<std::string> ReadJournalEntry(const JournalFilter& filter, const std::string& cursor)
{
  size_t top;
  size_t length;
  time_t second;
  uint64_t stamp;
  const void* data;
  const char* text;
  const char* equal;
  struct tm date;
  char line[64];
  sd_journal* journal;
  std::vector<std::string> fields;

  if (OpenJournal(&journal, filter) < 0)
  {
    return fields;
  }

  // Seeking puts the cursor in front of the entry, the step lands on it
  if ((sd_journal_seek_cursor(journal, cursor.c_str()) >= 0) &&
      (sd_journal_next(journal) > 0) &&
      (sd_journal_test_cursor(journal, cursor.c_str()) > 0))
  {
    if (sd_journal_get_realtime_usec(journal, &stamp) >= 0)
    {
      // Readable time on top, the raw fields below
      second = stamp / 1000000;
      localtime_r(&second, &date);
      strftime(line, sizeof(line), "%F %T %Z", &date);
      fields.push_back(std::string("__REALTIME_TIMESTAMP=") + std::to_string(stamp) + " (" + line + ")");
    }

    top = fields.size();

    SD_JOURNAL_FOREACH_DATA(journal, data, length)
    {
      text  = static_cast<const char*>(data);
      equal = static_cast<const char*>(memchr(text, '=', length));

      if ((equal != nullptr) &&
          (!std::all_of(text, text + length, [] (char character) { return (character == '\n') || (character == '\t') || (static_cast<unsigned char>(character) >= 0x20); })))
      {
        // Binary data, only its name and size
        fields.push_back(std::string(text, equal - text) + "=[" + std::to_string(length - (equal - text) - 1) + " bytes]");
        continue;
      }

      fields.emplace_back(text, length);
    }

    std::sort(fields.begin() + top, fields.end());
    fields.push_back("__CURSOR=" + cursor);
  }

  sd_journal_close(journal);
  return fields;
}

bool GetJournalField(sd_journal* journal, const char* name, char* buffer, size_t size)
{
  size_t length;
  size_t prefix;
  const char* data;

  prefix = strlen(name) + 1;

  if ((sd_journal_get_data(journal, name, (const void**)&data, &length) < 0) ||
      (length <= prefix))
  {
    // Field is not set
    return false;
  }

  length -= prefix;
  length  = std::min(length, size - 1);

  memcpy(buffer, data + prefix, length);
  buffer[length] = '\0';

  return true;
}

std::string GetJournalUserIdentifier(const std::string& user)
{
  struct passwd* entry;

  if ((!user.empty()) &&
      (user.find_first_not_of("0123456789") == std::string::npos))
  {
    // Already a number
    return user;
  }

  return ((entry = getpwnam(user.c_str())) != nullptr) ? std::to_string(entry->pw_uid) : std::string();
}

bool CheckJournalMatch(const std::string& match)
{
  size_t equal;

  equal = match.find('=');

  return
    (equal != std::string::npos) && (equal > 0) && (equal <= 64) &&
    ((match[0] < '0') || (match[0] > '9')) &&
    (match.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == equal);
}

bool ParseJournalTime(const std::string& text, uint64_t* stamp)
{
  static const char* const dates[]  = { "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d" };
  static const char* const clocks[] = { "%H:%M:%S", "%H:%M" };

  long value;
  char* end;
  time_t now;
  struct tm date;
  const char* rest;

  now = time(nullptr);

  if (text == "now")
  {
    *stamp = now * 1000000ULL;
    return true;
  }

  if ((text == "today") ||
      (text == "yesterday") ||
      (text == "tomorrow"))
  {
    // Midnight of the local day
    localtime_r(&now, &date);
    date.tm_hour   = 0;
    date.tm_min    = 0;
    date.tm_sec    = 0;
    date.tm_mday  += (text == "yesterday") ? -1 : ((text == "tomorrow") ? 1 : 0);
    date.tm_isdst  = -1;
    *stamp = mktime(&date) * 1000000ULL;
    return true;
  }

  if ((text.size() > 2) &&
      ((text[0] == '-') || (text[0] == '+')))
  {
    // Relative to now
    value = strtol(text.c_str(), &end, 10);

    if ((end[0] != '\0') &&
        (end[1] == '\0') &&
        (strchr("smhd", end[0]) != nullptr))
    {
      value *= (end[0] == 'm') ? 60 : ((end[0] == 'h') ? 3600 : ((end[0] == 'd') ? 86400 : 1));
      *stamp = (now + value) * 1000000ULL;
      return true;
    }

    return false;
  }

  for (const char* format : dates)
  {
    memset(&date, 0, sizeof(date));

    if (((rest = strptime(text.c_str(), format, &date)) != nullptr) &&
        (*rest == '\0'))
    {
      // Full date, local time
      date.tm_isdst = -1;
      *stamp = mktime(&date) * 1000000ULL;
      return true;
    }
  }

  for (const char* format : clocks)
  {
    localtime_r(&now, &date);
    date.tm_sec = 0;

    if (((rest = strptime(text.c_str(), format, &date)) != nullptr) &&
        (*rest == '\0'))
    {
      // Time of today
      date.tm_isdst = -1;
      *stamp = mktime(&date) * 1000000ULL;
      return true;
    }
  }

  return false;
}

static bool IsProcessRunning(int pid, uint64_t monotonic)
{
  int field;
  char* token;
  char* rest;
  char* bracket;
  char path[32];
  char buffer[1024];
  FILE* file;
  size_t length;
  unsigned long long start;

  snprintf(path, sizeof(path), "/proc/%d/stat", pid);

  if ((file = fopen(path, "r")) == nullptr)
  {
    // Gone
    return false;
  }

  length = fread(buffer, 1, sizeof(buffer) - 1, file);
  buffer[length] = '\0';
  fclose(file);

  if ((bracket = strrchr(buffer, ')')) == nullptr)
  {
    return false;
  }

  // The name may hold spaces and brackets, the fields after its last bracket start with the state, field 3
  for (field = 3, token = strtok_r(bracket + 1, " ", &rest); (token != nullptr) && (field < 22); field ++)
  {
    token = strtok_r(nullptr, " ", &rest);
  }

  if ((token == nullptr) ||
      (sscanf(token, "%llu", &start) != 1))
  {
    return false;
  }

  // Ticks since boot, a process started after the entry has only taken over its number
  return (start * 1000000ULL / sysconf(_SC_CLK_TCK)) <= monotonic;
}

static void ListProcesses(sd_journal* journal, std::map<std::string, std::string>& values, std::set<std::string>& running)
{
  struct Process
  {
    std::string name;
    bool current;        // Entry of the current boot
    uint64_t monotonic;  // Since that boot
  };

  int pid;
  int count;
  sd_id128_t boot;
  sd_id128_t entry;
  uint64_t monotonic;
  bool known;
  char label[JOURNAL_VALUE_LENGTH];
  char value[JOURNAL_VALUE_LENGTH];
  char name[JOURNAL_VALUE_LENGTH];
  std::map<int, Process> processes;

  // A journal of another machine has no boot of this one
  known = sd_id128_get_boot(&boot) >= 0;

  // Unique PIDs of the journal come without names, recent entries give both, the newest name wins
  sd_journal_seek_tail(journal);

  for (count = 0; (count < PID_SCAN) && (sd_journal_previous(journal) > 0); count ++)
  {
    if (GetJournalField(journal, "_PID", value, sizeof(value)) &&
        ((pid = atoi(value)) > 0) &&
        (processes.find(pid) == processes.end()) &&
        (GetJournalField(journal, "SYSLOG_IDENTIFIER", name, sizeof(name)) ||
         GetJournalField(journal, "_COMM", name, sizeof(name))))
    {
      // First seen from the tail is the newest
      processes.emplace(pid, Process { name, known && (sd_journal_get_monotonic_usec(journal, &monotonic, &entry) >= 0) && sd_id128_equal(entry, boot), monotonic });
    }
  }

  for (const std::pair<const int, Process>& item : processes)
  {
    // Right-aligned number sorts the labels the way numbers sort
    snprintf(label, sizeof(label), "%7d  %s", item.first, item.second.name.c_str());
    values.emplace(label, std::to_string(item.first));

    if (item.second.current &&
        IsProcessRunning(item.first, item.second.monotonic))
    {
      // Still the same process
      running.insert(label);
    }
  }
}

void ListJournalValues(const JournalFilter& filter, int field, std::map<std::string, std::string>& values, std::set<std::string>& running)
{
  size_t length;
  size_t prefix;
  uint64_t stamp;
  const void* data;
  struct tm date;
  time_t second;
  char host[JOURNAL_VALUE_LENGTH];
  char label[JOURNAL_VALUE_LENGTH * 2];
  char match[JOURNAL_VALUE_LENGTH];
  sd_journal* journal;
  struct passwd* entry;
  std::vector<std::string> list;

  // A journal of its own, so the cursors of the windows stay where they are
  if (OpenJournal(&journal, filter) < 0)
  {
    return;
  }

  if (field == JOURNAL_FIELD_COUNT)
  {
    // PID follows the source fields
    ListProcesses(journal, values, running);
    sd_journal_close(journal);
    return;
  }

  prefix = strlen(JournalFieldNames[field]) + 1;

  if (sd_journal_query_unique(journal, JournalFieldNames[field]) >= 0)
  {
    SD_JOURNAL_FOREACH_UNIQUE(journal, data, length)
    {
      if (length > prefix)
      {
        // FIELD=value
        list.emplace_back(static_cast<const char*>(data) + prefix, length - prefix);
      }
    }
  }

  for (const std::string& value : list)
  {
    if (field == JOURNAL_FIELD_USER)
    {
      // Number with the name, right-aligned so the labels sort the way numbers sort, the name goes to the field
      entry = getpwuid(atoi(value.c_str()));
      snprintf(label, sizeof(label), "%7s  %s", value.c_str(), (entry != nullptr) ? entry->pw_name : "?");
      values.emplace(label, (entry != nullptr) ? entry->pw_name : value);
      continue;
    }

    if (field == JOURNAL_FIELD_MACHINE)
    {
      // Machines are told by the host name of their last entry
      snprintf(match, sizeof(match), "_MACHINE_ID=%s", value.c_str());
      sd_journal_flush_matches(journal);
      sd_journal_add_match(journal, match, 0);

      snprintf(label, sizeof(label), "%-20s  %s",
        ((sd_journal_seek_tail(journal) >= 0) &&
         (sd_journal_previous(journal) > 0) &&
         GetJournalField(journal, "_HOSTNAME", host, sizeof(host))) ? host : "?",
        value.c_str());
      values.emplace(label, value);
      continue;
    }

    if (field != JOURNAL_FIELD_BOOT)
    {
      // Value is its own label
      values.emplace(value, value);
      continue;
    }

    // Boots are told by the time of their first entry, which also sorts them
    snprintf(match, sizeof(match), "_BOOT_ID=%s", value.c_str());
    sd_journal_flush_matches(journal);
    sd_journal_add_match(journal, match, 0);

    if ((sd_journal_seek_head(journal) >= 0) &&
        (sd_journal_next(journal) > 0) &&
        (sd_journal_get_realtime_usec(journal, &stamp) >= 0))
    {
      second = stamp / 1000000;
      localtime_r(&second, &date);
      strftime(label, sizeof(label), "%F %T  ", &date);
      strncat(label, value.c_str(), sizeof(label) - strlen(label) - 1);
      values.emplace(label, value);
    }
  }

  sd_journal_close(journal);
}
