#ifndef COMMANDS_H
#define COMMANDS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

enum
{
  cmJournalNew = 1000,
  cmJournalOpen,
  cmJournalSave,
  cmJournalLevels,
  cmJournalText,
  cmJournalSource,
  cmJournalTime,
  cmJournalColumns,
  cmJournalPause,
  cmJournalCopy,
  cmJournalClear,
  cmJournalFind,
  cmJournalFindNext,
  cmJournalFindPrevious,
  cmJournalDetails,
  cmJournalExport,
  cmJournalWrap,
  cmJournalHelp,
  cmJournalAbout,
  cmPageCopy,
  cmJournalPoll,
  cmJournalSourceClear,
  cmJournalSourceList,         // One per source field, then one for PID
  cmJournalTimePick   = 1100,  // Since, then Until
  cmJournalTimePreset = 1110   // One per preset of the time picker
};

#endif
