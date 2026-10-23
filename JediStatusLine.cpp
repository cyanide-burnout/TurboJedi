#define Uses_TProgram
#define Uses_TDeskTop
#include "JediStatusLine.h"
#include "JournalView.h"

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

  if ((TProgram::deskTop == nullptr) ||
      ((window = dynamic_cast<JournalWindow*>(TProgram::deskTop->current)) == nullptr))
  {
    // No window to tell about
    return "";
  }

  counts = window->view->counts;
  status = window->view->describe();

  snprintf(buffer, sizeof(buffer), " \xe2\x94\x82 err %llu warn %llu note %llu info %llu debug %llu",
    (unsigned long long)(counts[LOG_EMERG] + counts[LOG_ALERT] + counts[LOG_CRIT] + counts[LOG_ERR]),
    (unsigned long long)counts[LOG_WARNING],
    (unsigned long long)counts[LOG_NOTICE],
    (unsigned long long)counts[LOG_INFO],
    (unsigned long long)counts[LOG_DEBUG]);

  status += buffer;
  return status.c_str();
}
