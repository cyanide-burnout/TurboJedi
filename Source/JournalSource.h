#ifndef JOURNALSOURCE_H
#define JOURNALSOURCE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Ends the wait of Turbo Vision for events when a journal changes, the application goes on to idle() then

#define JOURNAL_POLL_TIMEOUT  20  // Milliseconds between polls of a journal without a source, as Turbo Vision waits by default

class JournalSource;

// The descriptor is the one of sd_journal_get_fd(), released before the journal is closed; nullptr when it cannot be waited for
JournalSource* CreateJournalSource(int handle);
void ReleaseJournalSource(JournalSource* source);

// Poll of the windows from idle()
struct JournalPoll
{
  int timeout;  // Milliseconds the next wait for events may take, a window shortens it
};

#endif
