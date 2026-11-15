// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TStatusItem
#define Uses_TEvent
#include "JediStatusLine.h"
#include "JournalWindow.h"
#include "WatchWindow.h"
#include "JediScript.h"
#include "Commands.h"

#include <stdio.h>
#include <syslog.h>

// JediStatusLine

JediStatusLine::JediStatusLine(const TRect& bounds, TStatusDef& definitions) :
  TStatusLine(bounds, definitions)
{
}

const char* JediStatusLine::hint(ushort context)
{
  char buffer[160];
  JournalWindow* window;
  const uint64_t* counts;

  // Watches of plugins first, whatever window is in front: a click on them opens their list
  indicator = DescribeWatches();
  status    = indicator;

  if ((TProgram::deskTop != nullptr) &&
      ((dynamic_cast<WatchWindow*>(TProgram::deskTop->current) != nullptr) ||
       (dynamic_cast<WatchLogWindow*>(TProgram::deskTop->current) != nullptr)))
  {
    // The zone of their times, U turns it over
    status += (status.empty() ? "" : " \xe2\x94\x82 ") + std::string(GetPluginUniversal() ? "UTC" : "local time");
  }

  if ((TProgram::deskTop == nullptr) ||
      ((window = dynamic_cast<JournalWindow*>(TProgram::deskTop->current)) == nullptr))
  {
    // No window to tell about
    return status.c_str();
  }

  counts  = window->view->counts;
  status += (status.empty() ? "" : " \xe2\x94\x82 ") + std::string(window->view->describe());

  snprintf(buffer, sizeof(buffer), " \xe2\x94\x82 err %llu warn %llu note %llu info %llu debug %llu",
    (unsigned long long)(counts[LOG_EMERG] + counts[LOG_ALERT] + counts[LOG_CRIT] + counts[LOG_ERR]),
    (unsigned long long)counts[LOG_WARNING],
    (unsigned long long)counts[LOG_NOTICE],
    (unsigned long long)counts[LOG_INFO],
    (unsigned long long)counts[LOG_DEBUG]);

  status += buffer;
  return status.c_str();
}

void JediStatusLine::handleEvent(TEvent& event)
{
  int start;
  TPoint where;

  if ((event.what == evMouseDown) &&
      (!indicator.empty()))
  {
    // The hint goes after the items and the separator, the way TStatusLine draws it
    where = makeLocal(event.mouse.where);
    start = 0;

    for (TStatusItem* item = items; item != nullptr; item = item->next)
    {
      if (item->text != nullptr)
      {
        start += cstrlen(item->text) + 2;
      }
    }

    start += 2;

    if ((where.x >= start) &&
        (where.x < start + strwidth(indicator)))
    {
      TEvent command;

      command.what            = evCommand;
      command.message.command = cmJournalWatches;
      command.message.infoPtr = nullptr;
      putEvent(command);
      clearEvent(event);
      return;
    }
  }

  TStatusLine::handleEvent(event);
}
