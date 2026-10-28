// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TEvent
#include <tvision/tv.h>
#include <internal/platform.h>

#include "JournalSource.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

namespace
{
  // Turbo Vision keeps its event sources to itself, the rules of access are not checked in an explicit instantiation

  template <tvision::EventWaiter tvision::Platform::*Member>
  struct WaiterAccess
  {
    friend tvision::EventWaiter& GetEventWaiter()
    {
      return tvision::Platform::getInstance().*Member;
    }
  };

  template struct WaiterAccess<&tvision::Platform::waiter>;

  tvision::EventWaiter& GetEventWaiter();
}

// JournalSource

class JournalSource : public tvision::EventSource
{
  public:

    JournalSource(int handle) :
      tvision::EventSource(handle)
    {
    }

    ~JournalSource()
    {
      close(handle);
    }

    bool getEvent(TEvent& event) noexcept override
    {
      // Nothing to hand over, the windows read their journals in idle() and clear inotify that way
      event.what = evNothing;
      return true;
    }
};

// Interface

JournalSource* CreateJournalSource(int handle)
{
  JournalSource* source;

  // Turbo Vision waits by select(), which leaves out a descriptor from FD_SETSIZE on without a word; a journal keeps one per file
  // and makes inotify after them, a copy takes the lowest number free and gets readable together with the original
  if ((handle < 0) ||
      ((handle = fcntl(handle, F_DUPFD_CLOEXEC, 0)) < 0))
  {
    return nullptr;
  }

  if (handle >= FD_SETSIZE)
  {
    // The wait times out to read the journals anyway
    close(handle);
    return nullptr;
  }

  source = new JournalSource(handle);
  GetEventWaiter().addSource(*source);
  return source;
}

void ReleaseJournalSource(JournalSource* source)
{
  if (source != nullptr)
  {
    // The copy would keep inotify alive and readable with nobody to clear it
    GetEventWaiter().removeSource(*source);
    delete source;
  }
}
