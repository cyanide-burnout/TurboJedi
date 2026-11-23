// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalTools.h"

#include <pwd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/magic.h>

#include <vector>
#include <algorithm>

#ifndef FUSE_SUPER_MAGIC
// linux/magic.h of the kernels of EL9 and Ubuntu 22.04 does not have it yet, the magic of FUSE has been this one always
#define FUSE_SUPER_MAGIC  0x65735546
#endif

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

std::string DescribeJournalLoss(const JournalFilter& filter)
{
  int probe;
  int result;
  sd_journal* journal;

  if ((result = OpenJournal(&journal, filter)) < 0)
  {
    return std::string("The journal cannot be opened: ") + strerror(-result);
  }

  // sd-journal opens the files it can and leaves the rest out without a word: one descriptor more tells whether the
  // limit of open files has been reached on the way. A sign, not a proof that the file of the entry is among the ones
  // left out
  probe  = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
  result = errno;
  sd_journal_close(journal);

  if (probe >= 0)
  {
    close(probe);
    return "The entry is not in the journal anymore";
  }

  return std::string("The journal cannot be opened wholly: ") + strerror(result);
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
    if ((!IsJournalExclusion(value)) &&
        (!IsJournalPresence(value)))
    {
      // Raw FIELD=VALUE, one left out and one of any value are up to a JournalChain
      sd_journal_add_match(*journal, value.c_str(), 0);
    }
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

// A value of the field is matched by the journal already
static bool HasJournalMatch(const JournalFilter& filter, const std::string& name)
{
  int field;

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if ((!filter.values[field].empty()) &&
        (name == JournalFieldNames[field]))
    {
      return true;
    }
  }

  if ((filter.pid != 0) &&
      (name == "_PID"))
  {
    return true;
  }

  for (const std::string& match : filter.matches)
  {
    if ((!IsJournalExclusion(match)) &&
        (!IsJournalPresence(match)) &&
        (match.compare(0, name.size() + 1, name + "=") == 0))
    {
      return true;
    }
  }

  return false;
}

int OpenJournalHistory(sd_journal** journal, const JournalFilter& filter)
{
  int result;
  size_t length;
  const void* data;
  std::string name;
  std::string value;
  std::vector<std::string> values;

  if ((result = OpenJournalQuery(journal, filter)) < 0)
  {
    return result;
  }

  for (const std::string& match : filter.matches)
  {
    if ((!IsJournalPresence(match)) ||
        (IsJournalExclusion(match)))
    {
      // No index tells a field missing
      continue;
    }

    name = match.substr(0, match.size() - 2);

    if ((HasJournalMatch(filter, name)) ||
        (sd_journal_query_unique(*journal, name.c_str()) < 0))
    {
      continue;
    }

    values.clear();

    SD_JOURNAL_FOREACH_UNIQUE(*journal, data, length)
    {
      if (values.size() == JOURNAL_UNIQUE_LIMIT)
      {
        break;
      }

      // Given as FIELD=VALUE already, a value left out by FIELD!=VALUE stays out
      value.assign(static_cast<const char*>(data), length);

      if (std::find(filter.matches.begin(), filter.matches.end(), name + "!" + value.substr(name.size())) == filter.matches.end())
      {
        values.push_back(value);
      }
    }

    if (values.size() == JOURNAL_UNIQUE_LIMIT)
    {
      // Too many to join, the chain looks at every entry
      continue;
    }

    if (values.empty())
    {
      // Nothing has the field or a value not left out, a match nothing satisfies
      values.push_back(name + "=");
    }

    for (const std::string& value : values)
    {
      // Values of one field are joined by OR, and with the other fields by AND
      sd_journal_add_match(*journal, value.data(), value.size());
    }
  }

  return result;
}

// Rank of a field among the lines of an entry: the address of the entry, of the program, of journald
static int RankJournalField(const std::string& line)
{
  if (line.compare(0, 2, "__") == 0) return 0;
  if (line.compare(0, 1, "_") == 0)  return 2;
  return 1;
}

std::vector<std::string> ReadJournalEntry(const JournalFilter& filter, const std::string& cursor, std::vector<bool>* matchable)
{
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
  std::vector<std::pair<std::string, bool>> lines;

  if (matchable != nullptr)
  {
    matchable->clear();
  }

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
      lines.emplace_back(std::string("__REALTIME_TIMESTAMP=") + std::to_string(stamp) + " (" + line + ")", false);
    }

    lines.emplace_back("__CURSOR=" + cursor, false);

    SD_JOURNAL_FOREACH_DATA(journal, data, length)
    {
      text  = static_cast<const char*>(data);
      equal = static_cast<const char*>(memchr(text, '=', length));

      if ((equal != nullptr) &&
          (!std::all_of(text, text + length, [] (char character) { return (character == '\n') || (character == '\t') || (character == '\r') || (character == '\x07') || (character == '\x1b') || (static_cast<unsigned char>(character) >= 0x20); })))
      {
        // Binary data, only its name and size, escape sequences of a terminal are text the details take out
        lines.emplace_back(std::string(text, equal - text) + "=[" + std::to_string(length - (equal - text) - 1) + " bytes]", false);
        continue;
      }

      lines.emplace_back(std::string(text, length), false);
      lines.back().second = (RankJournalField(lines.back().first) != 0) && CheckJournalMatch(lines.back().first, true);
    }

    // The time and the cursor stay in their order, the fields of the program and of journald in groups of their own
    std::stable_sort(lines.begin(), lines.end(), [] (const std::pair<std::string, bool>& one, const std::pair<std::string, bool>& other)
    {
      int left;
      int right;

      left  = RankJournalField(one.first);
      right = RankJournalField(other.first);

      return (left != right) ? (left < right) : ((left != 0) && (one.first < other.first));
    });
  }

  sd_journal_close(journal);

  for (const std::pair<std::string, bool>& line : lines)
  {
    fields.push_back(line.first);

    if (matchable != nullptr)
    {
      matchable->push_back(line.second);
    }
  }

  return fields;
}

std::vector<std::pair<std::string, std::string>> CollectJournalFields(sd_journal* journal)
{
  size_t length;
  uint64_t stamp;
  const void* data;
  const char* text;
  const char* equal;
  char* cursor;
  sd_id128_t boot;
  std::vector<std::pair<std::string, std::string>> fields;

  SD_JOURNAL_FOREACH_DATA(journal, data, length)
  {
    text  = static_cast<const char*>(data);
    equal = static_cast<const char*>(memchr(text, '=', length));

    if (equal != nullptr)
    {
      // Values are bytes, a plugin takes them as they are
      fields.emplace_back(std::string(text, equal - text), std::string(equal + 1, length - (equal - text) - 1));
    }
  }

  if (sd_journal_get_realtime_usec(journal, &stamp) >= 0)
  {
    fields.emplace_back("__REALTIME_TIMESTAMP", std::to_string(stamp));
  }

  if (sd_journal_get_monotonic_usec(journal, &stamp, &boot) >= 0)
  {
    // _BOOT_ID is a field of the entry already
    fields.emplace_back("__MONOTONIC_TIMESTAMP", std::to_string(stamp));
  }

  if (sd_journal_get_cursor(journal, &cursor) >= 0)
  {
    fields.emplace_back("__CURSOR", cursor);
    free(cursor);
  }

  return fields;
}

std::vector<std::vector<std::pair<std::string, std::string>>> ReadJournalRecords(const JournalFilter& filter, const std::vector<std::string>& cursors)
{
  sd_journal* journal;
  std::vector<std::vector<std::pair<std::string, std::string>>> records;

  if (cursors.empty() ||
      (OpenJournal(&journal, filter) < 0))
  {
    return records;
  }

  for (const std::string& cursor : cursors)
  {
    if ((sd_journal_seek_cursor(journal, cursor.c_str()) >= 0) &&
        (sd_journal_next(journal) > 0) &&
        (sd_journal_test_cursor(journal, cursor.c_str()) > 0))
    {
      records.push_back(CollectJournalFields(journal));
    }
  }

  sd_journal_close(journal);
  return records;
}

std::vector<std::pair<std::string, std::string>> ReadJournalFields(const JournalFilter& filter, const std::string& cursor)
{
  sd_journal* journal;
  std::vector<std::pair<std::string, std::string>> fields;

  if (OpenJournal(&journal, filter) < 0)
  {
    return fields;
  }

  // Seeking puts the cursor in front of the entry, the step lands on it
  if ((sd_journal_seek_cursor(journal, cursor.c_str()) >= 0) &&
      (sd_journal_next(journal) > 0) &&
      (sd_journal_test_cursor(journal, cursor.c_str()) > 0))
  {
    fields = CollectJournalFields(journal);
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

int GetJournalWait(sd_journal* journal, int limit)
{
  uint64_t moment;
  uint64_t timeout;
  struct timespec now;

  if ((sd_journal_get_timeout(journal, &timeout) < 0) ||
      (timeout == UINT64_MAX))
  {
    // Every change comes by inotify
    return limit;
  }

  // CLOCK_MONOTONIC, as the journal gives it
  clock_gettime(CLOCK_MONOTONIC, &now);
  moment = now.tv_sec * 1000000ULL + now.tv_nsec / 1000;

  return (timeout > moment) ? (int)std::min<uint64_t>(limit, (timeout - moment + 999) / 1000) : 0;
}

bool IsJournalBlind(sd_journal* journal, const JournalFilter& filter)
{
  struct statfs information;

  return (sd_journal_reliable_fd(journal) <= 0) ||
         ((!filter.directory.empty()) &&
          (statfs(filter.directory.c_str(), &information) == 0) &&
          (information.f_type == FUSE_SUPER_MAGIC));
}

void SeekJournalEnd(sd_journal* journal)
{
  sd_journal_seek_tail(journal);

  if (sd_journal_previous(journal) <= 0)
  {
    // An identifier that has written nothing yet, for one
    sd_journal_seek_head(journal);
  }
}

void SeekJournalCursor(sd_journal* journal, const char* position)
{
  // Whether the entry has passed a text filter or not
  if ((sd_journal_seek_cursor(journal, position) >= 0) &&
      (sd_journal_next(journal) > 0) &&
      (sd_journal_test_cursor(journal, position) <= 0))
  {
    // The entry has gone meanwhile, the one after it is still to read
    sd_journal_previous(journal);
  }
}

bool SeekJournalPlace(sd_journal* journal, sd_journal* source)
{
  char* position;

  if (sd_journal_get_cursor(source, &position) < 0)
  {
    // No entry there yet
    return false;
  }

  SeekJournalCursor(journal, position);
  free(position);
  return true;
}

static void RefreshJournalFile(const std::string& path)
{
  int handle;
  char byte;
  struct stat status;

  // FUSE takes in the changes of a file when it sees its attributes, NFS when the file is read: a mapping does neither
  if ((handle = open(path.c_str(), O_RDONLY | O_CLOEXEC)) >= 0)
  {
    fstat(handle, &status);
    pread(handle, &byte, 1, 0);
    close(handle);
  }
}

static void ListJournalFiles(const std::string& directory, int depth, std::vector<std::string>& files)
{
  DIR* handle;
  size_t length;
  std::string path;
  struct stat status;
  struct dirent* entry;

  if ((handle = opendir(directory.c_str())) == nullptr)
  {
    // Gone or not readable, the journal tells about it
    return;
  }

  while ((entry = readdir(handle)) != nullptr)
  {
    path   = directory + "/" + entry->d_name;
    length = strlen(entry->d_name);

    if (entry->d_name[0] == '.')
    {
      // Hidden ones
      continue;
    }

    if ((depth > 0) &&
        ((entry->d_type == DT_DIR) ||
         ((entry->d_type == DT_UNKNOWN) && (stat(path.c_str(), &status) == 0) && (S_ISDIR(status.st_mode)))))
    {
      // Directories of machine IDs, as libsystemd takes them
      ListJournalFiles(path, depth - 1, files);
      continue;
    }

    if (((length > 8) && (strcmp(entry->d_name + length - 8, ".journal") == 0)) ||
        ((length > 9) && (strcmp(entry->d_name + length - 9, ".journal~") == 0)))
    {
      if (strchr(entry->d_name, '@') == nullptr)
      {
        // An archived one has @ in its name and does not change anymore, only the files written to are looked at
        RefreshJournalFile(path);
      }

      // A rotation archives the file under a name of its own, the set of names tells it
      files.push_back(path);
    }
  }

  closedir(handle);
}

bool CheckJournalFiles(const std::string& directory, std::string& signature)
{
  std::string text;
  std::vector<std::string> files;

  ListJournalFiles(directory, 1, files);
  std::sort(files.begin(), files.end());

  for (const std::string& file : files)
  {
    text += file;
    text += '\n';
  }

  if (text == signature)
  {
    // The same files, their changes are in sight now
    return false;
  }

  signature = text;
  return true;
}

bool ReopenJournal(sd_journal** journal, const JournalFilter& filter, const std::string& cursor)
{
  sd_journal* fresh;

  if (OpenJournalQuery(&fresh, filter) < 0)
  {
    // The old one goes on with the files it has
    return false;
  }

  if (!cursor.empty())
  {
    // The last line read, the old journal may have nothing readable left
    SeekJournalCursor(fresh, cursor.c_str());
  }
  else if (!SeekJournalPlace(fresh, *journal))
  {
    // Nothing read yet, what comes after the tail is next
    SeekJournalEnd(fresh);
  }

  sd_journal_close(*journal);
  *journal = fresh;
  return true;
}

static long page;
static volatile sig_atomic_t faults;
static struct sigaction previous;

// What a signal handler may call only: open(), read(), close() and the functions of memory and strings, no allocation

static int GetHexDigit(char character)
{
  if ((character >= '0') && (character <= '9')) return character - '0';
  if ((character >= 'a') && (character <= 'f')) return character - 'a' + 10;
  return -1;
}

static bool EndsWith(const char* text, size_t length, const char* tail)
{
  size_t size;

  size = strlen(tail);
  return (length >= size) && (memcmp(text + length - size, tail, size) == 0);
}

static bool IsJournalLine(const char* line, size_t length, uintptr_t address)
{
  int digit;
  int field;
  size_t index;
  uintptr_t start;
  uintptr_t end;

  // start-end perms offset dev inode path, as /proc/self/maps has it
  start = 0;
  end   = 0;

  for (index = 0; (index < length) && ((digit = GetHexDigit(line[index])) >= 0); index ++) start = start * 16 + digit;

  if ((index >= length) ||
      (line[index ++] != '-'))
  {
    return false;
  }

  for (; (index < length) && ((digit = GetHexDigit(line[index])) >= 0); index ++) end = end * 16 + digit;

  if ((address < start) ||
      (address >= end))
  {
    // Another mapping
    return false;
  }

  for (field = 0; field < 4; field ++)
  {
    // Up to the path
    for (; (index < length) && (line[index] == ' '); index ++);
    for (; (index < length) && (line[index] != ' '); index ++);
  }

  for (; (index < length) && (line[index] == ' '); index ++);

  line   += index;
  length -= index;
  length -= EndsWith(line, length, " (deleted)") ? strlen(" (deleted)") : 0;

  // A file of a journal, rotated away or archived dirty as well; only libsystemd maps them in TurboJedi
  return EndsWith(line, length, ".journal") || EndsWith(line, length, ".journal~");
}

static bool IsJournalMapping(uintptr_t address)
{
  int handle;
  bool found;
  size_t used;
  size_t begin;
  size_t index;
  ssize_t length;
  char buffer[8192];

  if ((handle = open("/proc/self/maps", O_RDONLY | O_CLOEXEC)) < 0)
  {
    // Nothing to tell it by, the fault is not taken for a journal one
    return false;
  }

  found = false;
  used  = 0;

  while ((!found) &&
         ((length = read(handle, buffer + used, sizeof(buffer) - used)) > 0))
  {
    used += length;

    for (begin = 0, index = 0; index < used; index ++)
    {
      if (buffer[index] == '\n')
      {
        found = found || IsJournalLine(buffer + begin, index - begin, address);
        begin = index + 1;
      }
    }

    // The part of a line read so far goes in front of the rest, a line longer than the buffer has no path of interest
    memmove(buffer, buffer + begin, used - begin);
    used = (used - begin < sizeof(buffer)) ? (used - begin) : 0;
  }

  close(handle);
  return found;
}

static void HandleJournalFault(int number, siginfo_t* information, void* context)
{
  void* address;

  address = (void*)((uintptr_t)information->si_addr & ~((uintptr_t)page - 1));

  // Only a page of a mapped file of a journal is replaced, any other fault is not ours to hide
  if ((information->si_code == BUS_ADRERR) &&
      (IsJournalMapping((uintptr_t)information->si_addr)) &&
      (mmap(address, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED))
  {
    // The access made again reads zeros, the way systemd itself survives a journal file cut short
    faults = faults + 1;
    return;
  }

  if ((previous.sa_flags & SA_SIGINFO) &&
      (previous.sa_sigaction != nullptr))
  {
    // Not a mapped file gone bad, the handler of Turbo Vision gives the terminal back before the default action
    previous.sa_sigaction(number, information, context);
    return;
  }

  // No handler before, the access made again meets the default action
  signal(SIGBUS, ((previous.sa_handler == SIG_IGN) || (previous.sa_handler == SIG_DFL)) ? SIG_DFL : previous.sa_handler);
}

void SetJournalFaultHandler()
{
  struct sigaction action {};

  page                = sysconf(_SC_PAGESIZE);
  action.sa_sigaction = HandleJournalFault;
  action.sa_flags     = SA_SIGINFO | SA_NODEFER;

  sigemptyset(&action.sa_mask);
  sigaction(SIGBUS, &action, &previous);
}

unsigned GetJournalFaults()
{
  return faults;
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

bool CheckJournalMatch(const std::string& match, bool equal)
{
  size_t end;

  // The name ends at = or at != of a value left out
  end = match.find('=');
  end = ((!equal) && IsJournalExclusion(match)) ? (end - 1) : end;

  return
    (end != std::string::npos) && (end > 0) && (end <= 64) &&
    ((match[0] < '0') || (match[0] > '9')) &&
    (match.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == end);
}

bool IsJournalExclusion(const std::string& match)
{
  size_t equal;

  // ! cannot be a part of the name of a field, the first = tells where the name ends
  equal = match.find('=');

  return (equal != std::string::npos) && (equal > 0) && (match[equal - 1] == '!');
}

bool IsJournalPresence(const std::string& match)
{
  size_t equal;

  // The value is the star alone, a value of a field that has a star in it is matched as it is
  equal = match.find('=');

  return (equal != std::string::npos) && (equal > 0) && (match.compare(equal + 1, std::string::npos, "*") == 0);
}

std::string FormatJournalText(const JournalText& text)
{
  std::string notation;

  notation  = (text.mode & JOURNAL_TEXT_EXCLUDE) ? "!/" : "/";
  notation += text.text;
  notation += '/';
  notation += (text.mode & JOURNAL_TEXT_EXPRESSION) ? "r" : "";
  notation += (text.mode & JOURNAL_TEXT_SENSITIVE)  ? "c" : "";

  return notation;
}

bool ParseJournalText(const std::string& notation, JournalText& text)
{
  size_t start;
  size_t end;

  start = (notation.compare(0, 1, "!") == 0) ? 1 : 0;
  end   = notation.rfind('/');

  // The text is between the first / and the last one, a / inside it stays, only r and c may follow
  if ((notation.compare(start, 1, "/") != 0) ||
      (end == std::string::npos) || (end <= start + 1) ||
      (notation.find_first_not_of("rc", end + 1) != std::string::npos))
  {
    return false;
  }

  text.text = notation.substr(start + 1, end - start - 1);
  text.mode =
    ((start != 0)                                           ? JOURNAL_TEXT_EXCLUDE    : 0) |
    ((notation.find('r', end + 1) != std::string::npos)     ? JOURNAL_TEXT_EXPRESSION : 0) |
    ((notation.find('c', end + 1) != std::string::npos)     ? JOURNAL_TEXT_SENSITIVE  : 0);

  return true;
}

// A span of time as systemd writes it, without a sign: 30min, 1h 30min, 2 days, 90 (seconds); in microseconds
static bool ParseJournalSpan(const char* text, int64_t* span)
{
  static const struct { const char* name; int64_t size; } units[] =
  {
    { "us", 1LL },               { "usec", 1LL },
    { "ms", 1000LL },            { "msec", 1000LL },
    { "s", 1000000LL },          { "sec", 1000000LL },        { "second", 1000000LL },      { "seconds", 1000000LL },
    { "m", 60000000LL },         { "min", 60000000LL },       { "minute", 60000000LL },     { "minutes", 60000000LL },
    { "h", 3600000000LL },       { "hr", 3600000000LL },      { "hour", 3600000000LL },     { "hours", 3600000000LL },
    { "d", 86400000000LL },      { "day", 86400000000LL },    { "days", 86400000000LL },
    { "w", 604800000000LL },     { "week", 604800000000LL },  { "weeks", 604800000000LL },
    { "M", 2629800000000LL },    { "month", 2629800000000LL },{ "months", 2629800000000LL },  // 30.44 days, as systemd counts
    { "y", 31557600000000LL },   { "year", 31557600000000LL },{ "years", 31557600000000LL }   // 365.25 days
  };

  bool any;
  long long value;
  char* end;
  size_t length;
  int64_t size;

  any   = false;
  *span = 0;

  while (true)
  {
    while (*text == ' ') text ++;

    if (*text == '\0')
    {
      return any;
    }

    if (!isdigit((unsigned char)*text))
    {
      return false;
    }

    errno = 0;
    value = strtoll(text, &end, 10);

    if (errno == ERANGE)
    {
      return false;
    }

    for (text = end; *text == ' '; text ++);
    for (length = 0; isalpha((unsigned char)text[length]); length ++);

    // A number without a unit is seconds; the case of a unit matters, m is a minute and M a month
    size = (length == 0) ? 1000000LL : 0;

    for (const auto& unit : units)
    {
      if ((strlen(unit.name) == length) &&
          (strncmp(unit.name, text, length) == 0))
      {
        size = unit.size;
      }
    }

    if (size == 0)
    {
      return false;
    }

    // A span past what microseconds of int64_t hold is no time
    if (__builtin_mul_overflow((int64_t)value, size, &size) ||
        __builtin_add_overflow(*span, size, span))
    {
      return false;
    }

    text += length;
    any   = true;
  }
}

// Now moved by a span: false when it overflows or goes before 1970, which microseconds of the journal do not hold
static bool MoveJournalTime(time_t now, int64_t span, uint64_t* stamp)
{
  int64_t moment;

  if (__builtin_mul_overflow((int64_t)now, (int64_t)1000000, &moment) ||
      __builtin_add_overflow(moment, span, &moment) ||
      (moment < 0))
  {
    return false;
  }

  *stamp = moment;
  return true;
}

// What strptime() leaves after the seconds: nothing, or a fraction of up to six digits as journalctl takes it,
// 17:40:59.999999 the last microsecond of a second
static bool ParseJournalFraction(const char* rest, bool seconds, uint64_t* micro)
{
  int count;

  *micro = 0;

  if (*rest == '\0')
  {
    return true;
  }

  if ((!seconds) ||
      (*rest != '.'))
  {
    return false;
  }

  for (count = 0, rest ++; (*rest >= '0') && (*rest <= '9') && (count < 6); count ++, rest ++)
  {
    *micro = *micro * 10 + (*rest - '0');
  }

  for (; count < 6; count ++)
  {
    // Tenths, hundredths and so on in microseconds
    *micro *= 10;
  }

  return (*rest == '\0') && (rest[-1] != '.');
}

// How a time is written, as ReadJournalTime() tells it
#define TIME_UNKNOWN    0
#define TIME_RELATIVE   1  // now and spans from it, of no zone
#define TIME_DAY        2  // today, yesterday, tomorrow: midnight of the zone
#define TIME_ABSOLUTE   3  // date and time, or time of today, of the zone

// A time without the UTC at its end, in the local zone or in UTC
static int ReadJournalTime(const std::string& text, bool utc, uint64_t* stamp)
{
  static const char* const dates[]  = { "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d" };
  static const char* const clocks[] = { "%H:%M:%S", "%H:%M" };

  int64_t span;
  time_t now;
  struct tm date;
  uint64_t micro;
  const char* rest;

  now = time(nullptr);

  if (text == "now")
  {
    *stamp = now * 1000000ULL;
    return TIME_RELATIVE;
  }

  if ((text == "today") ||
      (text == "yesterday") ||
      (text == "tomorrow"))
  {
    // Midnight of the day of the zone
    utc ? gmtime_r(&now, &date) : localtime_r(&now, &date);
    date.tm_hour   = 0;
    date.tm_min    = 0;
    date.tm_sec    = 0;
    date.tm_mday  += (text == "yesterday") ? -1 : ((text == "tomorrow") ? 1 : 0);
    date.tm_isdst  = -1;
    *stamp = (utc ? timegm(&date) : mktime(&date)) * 1000000ULL;
    return TIME_DAY;
  }

  if ((text.size() > 1) &&
      ((text[0] == '-') || (text[0] == '+')))
  {
    // Relative to now, as journalctl takes it: -1h, -1h 30min, +2days
    return (ParseJournalSpan(text.c_str() + 1, &span) &&
            MoveJournalTime(now, (text[0] == '-') ? -span : span, stamp)) ? TIME_RELATIVE : TIME_UNKNOWN;
  }

  if ((text.size() > 4) &&
      ((text.compare(text.size() - 4, 4, " ago") == 0) ||
       ((text.size() > 5) && (text.compare(text.size() - 5, 5, " left") == 0))))
  {
    // The same the other way: 5min ago, 2 hours left
    return (ParseJournalSpan(text.substr(0, text.rfind(' ')).c_str(), &span) &&
            MoveJournalTime(now, (text.back() == 'o') ? -span : span, stamp)) ? TIME_RELATIVE : TIME_UNKNOWN;
  }

  for (const char* format : dates)
  {
    memset(&date, 0, sizeof(date));

    if (((rest = strptime(text.c_str(), format, &date)) != nullptr) &&
        (ParseJournalFraction(rest, strstr(format, "%S") != nullptr, &micro)))
    {
      // Full date
      date.tm_isdst = -1;
      *stamp = (utc ? timegm(&date) : mktime(&date)) * 1000000ULL + micro;
      return TIME_ABSOLUTE;
    }
  }

  for (const char* format : clocks)
  {
    utc ? gmtime_r(&now, &date) : localtime_r(&now, &date);
    date.tm_sec = 0;

    if (((rest = strptime(text.c_str(), format, &date)) != nullptr) &&
        (ParseJournalFraction(rest, strstr(format, "%S") != nullptr, &micro)))
    {
      // Time of today
      date.tm_isdst = -1;
      *stamp = (utc ? timegm(&date) : mktime(&date)) * 1000000ULL + micro;
      return TIME_ABSOLUTE;
    }
  }

  return TIME_UNKNOWN;
}

bool IsJournalTimeUniversal(const std::string& text)
{
  return (text.size() > 4) && (text.compare(text.size() - 4, 4, " UTC") == 0);
}

bool ParseJournalTime(const std::string& text, uint64_t* stamp)
{
  // 2026-11-16 03:00 UTC as journalctl takes it
  return IsJournalTimeUniversal(text) ?
    (ReadJournalTime(text.substr(0, text.size() - 4), true, stamp) != TIME_UNKNOWN) :
    (ReadJournalTime(text, false, stamp) != TIME_UNKNOWN);
}

bool GetJournalTimeZone(const std::string& text, bool* utc)
{
  uint64_t stamp;

  // A date, a time or a day is of a zone, now and spans from it are not
  *utc = IsJournalTimeUniversal(text);
  return *utc || (ReadJournalTime(text, false, &stamp) >= TIME_DAY);
}

bool ConvertJournalTime(const std::string& text, bool from, bool to, std::string& result)
{
  int kind;
  bool utc;
  uint64_t stamp;
  std::string bare;

  // The UTC at the end tells the zone, without it the time is of the one it has been typed in
  utc  = IsJournalTimeUniversal(text) || from;
  bare = IsJournalTimeUniversal(text) ? text.substr(0, text.size() - 4) : text;

  if ((bare.empty()) ||
      ((kind = ReadJournalTime(bare, utc, &stamp)) == TIME_UNKNOWN))
  {
    result = text;
    return bare.empty();
  }

  // The moment stays: in the other zone a day becomes the date and time its midnight is there
  if (kind == TIME_RELATIVE)  result = bare;
  else if (utc == to)         result = bare + (to ? " UTC" : "");
  else                        result = FormatJournalTime(stamp, to);

  return true;
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

void ListJournalFields(const JournalFilter& filter, std::set<std::string>& names)
{
  const char* field;
  sd_journal* journal;

  if (OpenJournal(&journal, filter) < 0)
  {
    return;
  }

  SD_JOURNAL_FOREACH_FIELD(journal, field)
  {
    if (strncmp(field, "__", 2) != 0)
    {
      // __CURSOR and the like are no fields to match
      names.insert(field);
    }
  }

  sd_journal_close(journal);
}

void ListJournalFieldValues(const JournalFilter& filter, const std::string& name, std::map<std::string, std::string>& values)
{
  size_t length;
  size_t prefix;
  const void* data;
  const char* text;
  sd_journal* journal;

  if ((!CheckJournalMatch(name + "=", true)) ||
      (OpenJournal(&journal, filter) < 0))
  {
    return;
  }

  prefix = name.size() + 1;

  if (sd_journal_query_unique(journal, name.c_str()) >= 0)
  {
    SD_JOURNAL_FOREACH_UNIQUE(journal, data, length)
    {
      text = static_cast<const char*>(data) + prefix;

      if ((length > prefix) &&
          (length - prefix <= JOURNAL_VALUE_LENGTH) &&
          (std::all_of(text, text + length - prefix, [] (char character) { return static_cast<unsigned char>(character) >= 0x20; })))
      {
        values.emplace(std::string(text, length - prefix), std::string(text, length - prefix));
      }

      if (values.size() >= JOURNAL_VALUE_LIMIT)
      {
        // MESSAGE and the like have more than a list can show
        break;
      }
    }
  }

  sd_journal_close(journal);
}

std::string GetJournalFilterName(const JournalFilter& filter)
{
  static const char letters[] = "EACRWNID";
  static const char* const keys[] = { "unit", "id", "comm", "via", "boot", "user", "machine" };

  int field;
  int priority;
  std::string caption;

  if (!filter.directory.empty()) caption = "journals=" + filter.directory;
  else if (filter.merge)         caption = "journals=all";

  if (filter.levels != 0xff)
  {
    caption += '[';
    for (priority = 0; priority < 8; priority ++)
    {
      // Letter of a shown level, dash of a hidden one
      caption += (filter.levels & (1 << priority)) ? letters[priority] : '-';
    }
    caption += ']';
  }

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (!filter.values[field].empty())
    {
      // Boot and machine identifiers are long, their beginning tells them apart
      caption += caption.empty() ? "" : " ";
      caption += keys[field];
      caption += '=';
      caption += ((field == JOURNAL_FIELD_BOOT) || (field == JOURNAL_FIELD_MACHINE)) ? filter.values[field].substr(0, 8) : filter.values[field];
    }
  }

  if (filter.pid != 0)
  {
    caption += caption.empty() ? "pid=" : " pid=";
    caption += std::to_string(filter.pid);
  }

  for (const std::string& value : filter.matches)
  {
    caption += caption.empty() ? "" : " ";
    caption += value;
  }

  if (!filter.since.empty())
  {
    caption += caption.empty() ? "since=" : " since=";
    caption += filter.since;
  }

  if (!filter.until.empty())
  {
    caption += caption.empty() ? "until=" : " until=";
    caption += filter.until;
  }

  for (const JournalText& text : filter.texts)
  {
    // Like sed: !/text/ leaves out, r is a regular expression, c cares about case
    caption += caption.empty() ? "" : " ";
    caption += FormatJournalText(text);
  }

  return caption;
}

std::string FormatJournalTime(uint64_t stamp, bool utc)
{
  time_t second;
  struct tm date;
  char text[48];

  second = stamp / 1000000;
  utc ? gmtime_r(&second, &date) : localtime_r(&second, &date);
  strftime(text, sizeof(text), "%F %T", &date);

  if ((stamp % 1000000) != 0)
  {
    snprintf(text + strlen(text), sizeof(text) - strlen(text), ".%06u", (unsigned)(stamp % 1000000));
  }

  if (utc)
  {
    strcat(text, " UTC");
  }

  return text;
}

uint64_t AlignJournalTime(uint64_t realtime, uint64_t step, bool utc)
{
  time_t second;
  struct tm date;
  int64_t offset;
  int64_t local;

  if (utc)
  {
    return realtime - (realtime % step);
  }

  // The wall clock of the moment, cut down to the step
  second = realtime / 1000000;
  localtime_r(&second, &date);
  offset = date.tm_gmtoff * 1000000LL;
  local  = (int64_t)realtime + offset;
  local -= local % (int64_t)step;

  // Back by the offset of the start, which differs on a day the clocks change: midnight of 2026-03-29 is still of
  // winter time in Berlin while 10:00 is of summer time
  second = (local - offset) / 1000000;
  localtime_r(&second, &date);

  return local - date.tm_gmtoff * 1000000LL;
}

uint64_t GetJournalWallTime(const struct tm& wall)
{
  int flag;
  time_t moment;
  time_t chosen;
  struct tm date;

  chosen = (time_t)-1;

  for (flag = 0; flag <= 1; flag ++)
  {
    // As winter time and as summer time: the one that keeps the clock is it, in the hour that comes twice both do
    date          = wall;
    date.tm_isdst = flag;
    moment        = mktime(&date);

    if ((date.tm_hour == ((wall.tm_hour % 24 + 24) % 24)) &&
        (date.tm_min == wall.tm_min) &&
        (date.tm_isdst == flag) &&
        ((chosen == (time_t)-1) || (moment < chosen)))
    {
      chosen = moment;
    }
  }

  if (chosen == (time_t)-1)
  {
    // A time the clocks skip, mktime() moves it on
    date          = wall;
    date.tm_isdst = -1;
    chosen        = mktime(&date);
  }

  return chosen * 1000000ULL;
}
