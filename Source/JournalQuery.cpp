// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TEvent
#include <tvision/tv.h>

#include "JournalQuery.h"
#include "JournalSource.h"
#include "Commands.h"

#include <time.h>
#include <string.h>

#include <algorithm>

#define QUERY_QUIET   250  // Milliseconds a query may take before its dialog is shown
#define QUERY_STEP    20   // Milliseconds of reading between the events
#define QUERY_BATCH   256  // Entries read between two looks at the clock

#define cmQueryDone   1310

int64_t GetQueryMilliseconds()
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

// JournalQuery

JournalQuery::JournalQuery()
{
  journal = nullptr;
  stopped = nullptr;
  scanned = 0;
  over    = true;
  held    = false;
}

JournalQuery::~JournalQuery()
{
  if (journal != nullptr)
  {
    sd_journal_close(journal);
  }
}

bool JournalQuery::start(const JournalFilter& filter, const std::string& cursor, bool older, size_t limit, size_t scan, std::string& error)
{
  int result;

  this->older = older;
  this->limit = limit;
  this->scan  = scan;
  exclude     = (filter.mode & JOURNAL_TEXT_EXCLUDE) != 0;
  since       = 0;
  until       = 0;

  if ((!filter.since.empty() && !ParseJournalTime(filter.since, &since)) ||
      (!filter.until.empty() && !ParseJournalTime(filter.until, &until)) ||
      (!matcher.set(filter.text, filter.mode)))
  {
    error = "the time or the text of the filter is wrong";
    return false;
  }

  if ((result = OpenJournalQuery(&journal, filter)) < 0)
  {
    error = strerror(-result);
    journal = nullptr;
    return false;
  }

  if (!cursor.empty())
  {
    // The entry of the cursor itself is a bound, not a result: the first step lands on it, or on the nearest one when it
    // is gone, which is a result then
    if ((sd_journal_seek_cursor(journal, cursor.c_str()) < 0) ||
        ((older ? sd_journal_previous(journal) : sd_journal_next(journal)) <= 0))
    {
      over = false;
      stop(nullptr);
      return true;
    }

    held = (sd_journal_test_cursor(journal, cursor.c_str()) <= 0);
  }
  else if (older)
  {
    // The newest of the range first
    (until != 0) ? sd_journal_seek_realtime_usec(journal, until) : sd_journal_seek_tail(journal);
  }
  else
  {
    (since != 0) ? sd_journal_seek_realtime_usec(journal, since) : sd_journal_seek_head(journal);
  }

  over = false;
  return true;
}

void JournalQuery::stop(const char* reason)
{
  if (!over)
  {
    stopped = reason;
    over    = true;
  }
}

bool JournalQuery::step(int64_t deadline)
{
  int count;
  int result;
  size_t length;
  uint64_t stamp;
  const void* data;
  std::string text;
  char identifier[256];
  char pid[32];

  while (!over)
  {
    if (GetQueryMilliseconds() >= deadline)
    {
      // The caller tells whether it goes on
      return false;
    }

    for (count = 0; (count < QUERY_BATCH) && (!over); count ++)
    {
      if (held)
      {
        // Landed on by the start, not looked at yet
        held = false;
      }
      else if ((result = (older ? sd_journal_previous(journal) : sd_journal_next(journal))) <= 0)
      {
        // The end of the journal, or one that cannot be read on
        stop(nullptr);
        break;
      }

      if (sd_journal_get_realtime_usec(journal, &stamp) >= 0)
      {
        if (older ? ((since != 0) && (stamp < since)) : ((until != 0) && (stamp > until)))
        {
          // Past the range
          stop(nullptr);
          break;
        }

        if (older ? ((until != 0) && (stamp > until)) : ((since != 0) && (stamp < since)))
        {
          // Not in the range yet
          continue;
        }
      }

      if (scanned >= scan)
      {
        stop("scan");
        break;
      }

      scanned ++;

      if (!matcher.empty())
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

        if (matcher.match(text) == exclude)
        {
          continue;
        }
      }

      records.push_back(CollectJournalFields(journal));

      if (records.size() >= limit)
      {
        stop("limit");
      }
    }
  }

  return true;
}

// QueryDialog

class QueryDialog : public TDialog
{
  public:

    QueryDialog(JournalQuery& query, int64_t deadline);

    void handleEvent(TEvent& event) override;

  private:

    JournalQuery& query;
    int64_t deadline;
};

QueryDialog::QueryDialog(JournalQuery& query, int64_t deadline) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 44, 8), "Reading"),
  query(query)
{
  this->deadline = deadline;

  options |= ofCentered;
  insert(new TStaticText(TRect(2, 2, 42, 3), "Reading the journal for a plugin"));
  insert(new TButton(TRect(16, 5, 28, 7), "Cancel", cmCancel, bfDefault));
}

void QueryDialog::handleEvent(TEvent& event)
{
  JournalPoll* poll;

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalPoll))
  {
    // Between the events, the windows go on
    poll          = static_cast<JournalPoll*>(event.message.infoPtr);
    poll->timeout = 0;

    if (query.step(std::min(GetQueryMilliseconds() + QUERY_STEP, (deadline > 0) ? deadline : INT64_MAX)))
    {
      endModal(cmQueryDone);
    }
    else if ((deadline > 0) &&
             (GetQueryMilliseconds() >= deadline))
    {
      query.stop("time");
      endModal(cmQueryDone);
    }

    return;
  }

  TDialog::handleEvent(event);
}

void RunJournalQuery(JournalQuery& query, int timeout)
{
  int64_t deadline;
  QueryDialog* dialog;

  deadline = (timeout > 0) ? (GetQueryMilliseconds() + timeout * 1000LL) : 0;

  if (query.step(GetQueryMilliseconds() + QUERY_QUIET))
  {
    // A short one ends before anything is shown
    return;
  }

  dialog = new QueryDialog(query, deadline);

  if (TProgram::deskTop->execView(dialog) != cmQueryDone)
  {
    query.stop("cancel");
  }

  TObject::destroy(dialog);
}
