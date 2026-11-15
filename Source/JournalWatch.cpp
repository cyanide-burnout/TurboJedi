// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalWatch.h"

#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <algorithm>

static uint64_t GetRealTime()
{
  struct timespec now;

  clock_gettime(CLOCK_REALTIME, &now);
  return now.tv_sec * 1000000ULL + now.tv_nsec / 1000;
}

// JournalWatch

JournalWatch::JournalWatch()
{
  number      = 0;
  state       = WATCH_ACTIVE;
  global      = false;
  fresh       = false;
  raised      = false;
  window      = nullptr;
  serial      = 1;
  latest      = 0;
  order       = 0;
  gaps        = 0;
  gap         = { 0, 0, false };
  delivered   = { std::string(), 0 };
  lag         = WATCH_LAG;
  callback    = -2;  // LUA_NOREF, the engine sets it
  self        = -2;
  journal     = nullptr;
  notifier    = nullptr;
  blind       = false;
  faults      = 0;
  exclude     = false;
  place       = { std::string(), 0 };
  begun       = 0;
  reached     = 0;
  more        = false;
  checking    = false;
  attempts    = 0;
  invalidated = 0;
  gapping     = false;
  since       = 0;
  uncertain   = false;
}

JournalWatch::~JournalWatch()
{
  close();
}

bool JournalWatch::start(const JournalFilter& filter, const JournalPosition* from, std::string& error)
{
  int result;

  this->filter = filter;
  exclude      = (filter.mode & JOURNAL_TEXT_EXCLUDE) != 0;

  if (!matcher.set(filter.text, filter.mode))
  {
    error = "the text of the filter is a broken expression";
    return false;
  }

  if ((result = OpenJournalQuery(&journal, filter)) < 0)
  {
    error   = strerror(-result);
    journal = nullptr;
    return false;
  }

  attach();
  begun   = GetRealTime();
  reached = GetQueryMilliseconds();

  if ((from != nullptr) &&
      (!from->cursor.empty()))
  {
    // Restored from a layout: the files have had all the time to change, the entry is looked for once
    place     = *from;
    delivered = *from;

    if (!find())
    {
      fall();
    }

    return true;
  }

  // What has been there before the watch is not its business: the last entry of the filter is the place, the next one is
  // the first new one
  SeekJournalEnd(journal);
  remember();
  return true;
}

void JournalWatch::attach()
{
  // As a window does: changes wake the application, a file system without inotify is looked at on every poll
  notifier = CreateJournalSource(sd_journal_get_fd(journal));
  blind    = IsJournalBlind(journal, filter);
  faults   = GetJournalFaults();
  files.clear();

  if (blind)
  {
    CheckJournalFiles(filter.directory, files);
  }
}

void JournalWatch::close()
{
  if (journal != nullptr)
  {
    // The copy of inotify goes first
    ReleaseJournalSource(notifier);
    sd_journal_close(journal);
    journal  = nullptr;
    notifier = nullptr;
  }
}

void JournalWatch::remember()
{
  char* cursor;
  uint64_t stamp;

  if (sd_journal_get_cursor(journal, &cursor) < 0)
  {
    // Not on an entry: nothing of the filter yet, the place stays empty
    return;
  }

  place.cursor = cursor;
  free(cursor);

  if (sd_journal_get_realtime_usec(journal, &stamp) >= 0)
  {
    place.realtime = stamp;
  }
}

bool JournalWatch::find()
{
  if (place.cursor.empty())
  {
    // Nothing read yet, the journal is still on its end
    return true;
  }

  // A successful seek only puts the journal near the place, the entry is there only when the step lands on it
  return (sd_journal_seek_cursor(journal, place.cursor.c_str()) >= 0) &&
         (sd_journal_next(journal) > 0) &&
         (sd_journal_test_cursor(journal, place.cursor.c_str()) > 0);
}

void JournalWatch::fall()
{
  int result;
  uint64_t first;
  uint64_t last;
  JournalWatchEvent event;

  first  = 0;
  last   = 0;
  result = sd_journal_get_cutoff_realtime_usec(journal, &first, &last);

  if ((result >= 0) &&
      (first != 0) &&
      (first > place.realtime))
  {
    // The journal begins after the place: its entry has been deleted along with all that has followed it up to there
    sd_journal_seek_head(journal);
    event.gap      = true;
    event.range    = { place.realtime, first, false };
    event.position = place;
    events.push_back(event);
    gapping = false;
    return;
  }

  // Older entries are still there, a file between them may have gone or may not: on after the time of the place, and
  // whether something is missing in between is not known. The gap is told with the next entry, an empty journal waits
  sd_journal_seek_realtime_usec(journal, place.realtime + 1);
  gapping   = true;
  since     = place.realtime;
  uncertain = true;
}

void JournalWatch::jump()
{
  JournalWatchEvent event;

  // Neither the entries read and not given yet nor the rest are going to be, the gap starts after the last one given
  event.gap   = true;
  event.range = { (delivered.realtime != 0) ? delivered.realtime : begun, 0, false };

  events.clear();
  SeekJournalEnd(journal);
  remember();

  event.range.till = place.realtime;
  event.position   = place;
  events.push_back(event);

  gapping = false;
  more    = false;
  reached = GetQueryMilliseconds();
}

void JournalWatch::poll(size_t limit, int64_t deadline, bool paused)
{
  int result;
  int64_t now;
  size_t count;
  sd_journal* fresh;
  JournalWatchEvent event;

  if ((state != WATCH_ACTIVE) ||
      (journal == nullptr))
  {
    return;
  }

  now = GetQueryMilliseconds();

  if ((blind) &&
      ((CheckJournalFiles(filter.directory, files)) || (faults != GetJournalFaults())))
  {
    // A rotation on a file system without inotify, or its files gone bad after a reconnect: opened again and the place
    // looked for as after a change of the files
    if ((result = OpenJournalQuery(&fresh, filter)) < 0)
    {
      stop(WATCH_STOPPED, std::string("the journal cannot be opened again, ") + strerror(-result));
      return;
    }

    close();
    journal = fresh;
    attach();

    if (place.cursor.empty())
    {
      SeekJournalEnd(journal);
    }

    checking    = true;
    attempts    = 0;
    invalidated = now;
  }

  // Taken even while paused: inotify would keep waking the application otherwise
  result = sd_journal_process(journal);

  if (result == SD_JOURNAL_INVALIDATE)
  {
    if ((!filter.directory.empty()) &&
        (access(filter.directory.c_str(), R_OK | X_OK) != 0))
    {
      // Unmounted or removed under the watch
      stop(WATCH_STOPPED, filter.directory + " is gone, " + strerror(errno));
      return;
    }

    // Files have come or gone: a rotation may still go on, the place is looked for while the files settle
    if (!checking)
    {
      checking = true;
      attempts = 0;
    }

    invalidated = now;
  }

  if (paused)
  {
    // A dialog is open: the plugin is not called, nothing piles up, the time behind starts anew when it is closed
    reached = now;
    return;
  }

  if (checking)
  {
    if (find())
    {
      // Still there, the journal is on it again
      checking = false;
    }
    else if (((++ attempts) >= WATCH_ATTEMPTS) ||
             (now - invalidated >= WATCH_RETRY))
    {
      // Either bound ends the wait
      checking = false;
      fall();
    }
    else
    {
      // Looked for again on the next poll
      return;
    }
  }

  more = false;

  for (count = 0; events.size() < WATCH_QUEUE; )
  {
    if ((count >= limit) ||
        (GetQueryMilliseconds() >= deadline))
    {
      // The rest on the next poll
      more = true;
      break;
    }

    if (sd_journal_next(journal) <= 0)
    {
      // The end, or nothing readable there
      reached = now;
      break;
    }

    count ++;
    remember();

    if (!TestJournalText(journal, matcher, exclude))
    {
      continue;
    }

    if (gapping)
    {
      // The first entry after a lost place tells where the gap ends
      event.gap      = true;
      event.range    = { since, place.realtime, uncertain };
      event.position = place;
      events.push_back(event);
      gapping = false;
    }

    event.gap      = false;
    event.range    = { 0, 0, false };
    event.fields   = CollectJournalFields(journal);
    event.position = place;
    events.push_back(event);
  }

  if (events.size() >= WATCH_QUEUE)
  {
    // The plugin takes them slower than they come
    more = true;
  }

  if ((more) &&
      (now - reached >= lag))
  {
    // Not once at the end for that long while reading all it may: it does not catch up
    jump();
  }
}

int JournalWatch::wait(int limit)
{
  if ((state != WATCH_ACTIVE) ||
      (journal == nullptr))
  {
    return limit;
  }

  if ((more) &&
      (events.size() < WATCH_QUEUE))
  {
    // More to read at once
    return 0;
  }

  if ((checking) ||
      (events.size() >= WATCH_QUEUE))
  {
    // The place is looked for again soon, or the plugin has to take some first
    return std::min(limit, 50);
  }

  if ((blind) ||
      (notifier == nullptr))
  {
    // Nothing would wake the application for this one
    return std::min(limit, JOURNAL_POLL_TIMEOUT);
  }

  return GetJournalWait(journal, limit);
}

void JournalWatch::stop(JournalWatchState state, const std::string& reason)
{
  close();
  events.clear();
  checking     = false;
  gapping      = false;
  more         = false;
  this->state  = state;
  this->reason = reason;
}

uint64_t JournalWatch::add(const std::string& text, const JournalPosition& entry)
{
  JournalWatchLine line;

  line.number = serial ++;
  line.moment = GetRealTime();
  line.text   = text;
  line.entry  = entry;
  line.unread = true;

  lines.push_back(line);

  if (lines.size() > WATCH_LINE_LIMIT)
  {
    // The oldest go
    lines.pop_front();
  }

  latest = line.moment;
  fresh  = true;
  return line.number;
}

void JournalWatch::erase(uint64_t number)
{
  std::deque<JournalWatchLine>::iterator line;

  line = std::find_if(lines.begin(), lines.end(), [number] (const JournalWatchLine& item) { return item.number == number; });

  if (line != lines.end())
  {
    // A line gone already is not an error
    lines.erase(line);
  }
}

void JournalWatch::clear()
{
  lines.clear();
  raised = false;
}

size_t JournalWatch::unread() const
{
  return std::count_if(lines.begin(), lines.end(), [] (const JournalWatchLine& line) { return line.unread; });
}

bool JournalWatch::restoring() const
{
  return gapping;
}
