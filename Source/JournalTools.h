#ifndef JOURNALTOOLS_H
#define JOURNALTOOLS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <stdint.h>

#include <systemd/sd-journal.h>

#include <map>
#include <set>
#include <vector>
#include <string>

#define JOURNAL_FIELD_UNIT        0
#define JOURNAL_FIELD_IDENTIFIER  1
#define JOURNAL_FIELD_COMMAND     2
#define JOURNAL_FIELD_TRANSPORT   3
#define JOURNAL_FIELD_BOOT        4
#define JOURNAL_FIELD_USER        5
#define JOURNAL_FIELD_MACHINE     6
#define JOURNAL_FIELD_COUNT       7

#define JOURNAL_VALUE_LENGTH      128

#define JOURNAL_COLUMN_DATE        (1 << 0)
#define JOURNAL_COLUMN_TIME        (1 << 1)
#define JOURNAL_COLUMN_LEVEL       (1 << 2)
#define JOURNAL_COLUMN_HOST        (1 << 3)
#define JOURNAL_COLUMN_UNIT        (1 << 4)
#define JOURNAL_COLUMN_IDENTIFIER  (1 << 5)
#define JOURNAL_COLUMN_PID         (1 << 6)
#define JOURNAL_COLUMN_UTC         (1 << 7)  // Options of the way columns look rather than columns
#define JOURNAL_COLUMN_MILLISECOND (1 << 8)
#define JOURNAL_COLUMN_WRAP        (1 << 9)
#define JOURNAL_COLUMN_HEADLESS    (1 << 10)  // The heading of the columns is hidden
#define JOURNAL_COLUMN_REVERSE     (1 << 11)  // The newest entries on top, like journalctl --reverse
#define JOURNAL_COLUMN_DEFAULT     (JOURNAL_COLUMN_DATE | JOURNAL_COLUMN_TIME | JOURNAL_COLUMN_LEVEL | JOURNAL_COLUMN_IDENTIFIER | JOURNAL_COLUMN_PID | JOURNAL_COLUMN_MILLISECOND)

#define JOURNAL_TEXT_EXPRESSION    (1 << 0)  // Text is a regular expression
#define JOURNAL_TEXT_EXCLUDE       (1 << 1)  // Entries that match are left out
#define JOURNAL_TEXT_SENSITIVE     (1 << 2)  // Case matters

// What a window asks the journal for, empty values and zero PID stand for any

struct JournalFilter
{
  std::string directory;             // Journal files of a directory, empty for the journals of this system
  bool merge;                        // Of every machine there are instead of this one, like journalctl --merge
  std::string values[JOURNAL_FIELD_COUNT];
  std::vector<std::string> matches;  // Raw FIELD=VALUE from the command line
  int pid;
  uint16_t levels;                   // Bit N shows priority N
  std::string text;                  // The only filter applied on this side, JOURNAL_TEXT_* tell how
  uint16_t mode;
  std::string since;                 // Times as given, relative ones count from each opening
  std::string until;
  uint16_t columns;                  // Shown in front of the message, not a filter but a new window takes it along too
  std::string highlight;             // Rules that paint the message, the same way
};

extern const char* const JournalFieldNames[JOURNAL_FIELD_COUNT];

// Journals the filter names: of this machine, of every machine or of a directory
int OpenJournal(sd_journal** journal, const JournalFilter& filter);

// Journal with the matches of a filter, everything but its text and times
int OpenJournalQuery(sd_journal** journal, const JournalFilter& filter);

// Milliseconds up to the limit to wait for the journal, less when inotify cannot tell about every change
int GetJournalWait(sd_journal* journal, int limit);

// Every field of the entry at a cursor as FIELD=value lines, empty when the entry is gone
std::vector<std::string> ReadJournalEntry(const JournalFilter& filter, const std::string& cursor);
bool GetJournalField(sd_journal* journal, const char* name, char* buffer, size_t size);

// The file system of the journal tells nothing about its changes: a network one, which libsystemd knows, or FUSE such as sshfs,
// which it does not; such a journal is read on every poll instead of when inotify tells
bool IsJournalBlind(sd_journal* journal, const JournalFilter& filter);

// Files of a journal directory and of the directories of machine IDs in it: those written to are opened and read, as FUSE gives
// what a journal has mapped anew only when it sees the attributes of a file and NFS only when the file is read. True when the
// names differ from the signature, which takes them then: a rotation archives a file under a new name, libsystemd does not see
// the new file without inotify
bool CheckJournalFiles(const std::string& directory, std::string& signature);

// The journal opened again with its filter, after the entry of the cursor or on the place it has read up to; false keeps the old one
bool ReopenJournal(sd_journal** journal, const JournalFilter& filter, const std::string& cursor);

// A fault in a mapping of a *.journal file, as /proc/self/maps tells it, gives a page of zeros instead of SIGBUS, libsystemd reads
// it as a damaged entry: a file of FUSE or of a network goes bad under an open journal when its connection comes back, sshfs
// after reconnect for one. Other faults go to the handler there has been before, set after Turbo Vision the terminal is not
// touched for a journal
void SetJournalFaultHandler();
unsigned GetJournalFaults();  // Faults so far, a journal opened before one of them is opened again

// Puts the journal on its last entry of the filter, so next() goes on with the newest ones. Without any entry of the filter yet
// it goes to the head: libsystemd never finds anything after a bare tail, from the head the first entry to come is next
void SeekJournalEnd(sd_journal* journal);

// Puts the journal on the entry of a cursor, so next() goes on after it; when the entry is gone, on the one that has followed it
void SeekJournalCursor(sd_journal* journal, const char* position);

// Puts the journal on the entry another one has read last, so next() goes on after it; false when the other has read nothing
bool SeekJournalPlace(sd_journal* journal, sd_journal* source);

// Labels with values to choose a source field from, JOURNAL_FIELD_COUNT stands for PID with the labels of running processes
void ListJournalValues(const JournalFilter& filter, int field, std::map<std::string, std::string>& values, std::set<std::string>& running);

// _UID value of a user name or number, empty when there is no such user
std::string GetJournalUserIdentifier(const std::string& user);

// FIELD=VALUE with a field name the journal takes: A-Z, 0-9 and _, not a digit first, up to 64 characters
bool CheckJournalMatch(const std::string& match);

// Short form of a filter for window titles: [levels], source fields, matches, times and /text/ the way sed writes it
std::string GetJournalFilterName(const JournalFilter& filter);

// Microseconds of 2026-10-23 19:00[:00], 2026-10-23, 19:00[:00] (today), now, today, yesterday, tomorrow, -1h, +30m (s, m, h, d)
bool ParseJournalTime(const std::string& text, uint64_t* stamp);

#endif
