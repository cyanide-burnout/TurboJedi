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
  cmJournalFilter,             // The window of the filter, F3 opens it on the levels
  cmJournalSource,             // The journals of the window
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
  cmJournalUpdate,
  cmJournalUpdated,            // The check for a newer release is over
  cmJournalUpdateAsk,          // Nobody has chosen or declined the check on start yet
  cmUpdateCopyLink,
  cmUpdateCopyCommand,
  cmJournalGoTo,
  cmJournalSync,
  cmJournalSyncTo,             // Broadcast with a JournalMoment
  cmJournalOpenFilter,         // Broadcast with a JournalFilter for a new window
  cmJournalHighlight,
  cmJournalBack,
  cmJournalForward,
  cmJournalBrowse,
  cmJournalTimeline,
  cmJournalClone,
  cmJournalWindows,
  cmJournalReverse,
  cmJournalMarked,
  cmPageCopy,
  cmPageShow,                  // The field under the cursor of a page into the filter
  cmPageHide,                  // Its value left out
  cmFilterFields,              // The window of the filter: the list of the fields
  cmFilterValues,              // The list of the values of the field
  cmFilterAddField,            // The field and its value into the rules
  cmFilterAddText,             // The text into the rules
  cmFilterRemove,              // The rule under the cursor out
  cmFilterClear,               // The whole filter out
  cmFilterTime,                // The dialog of the time range
  cmPageCursor,                // Broadcast of a page: its cursor has moved
  cmJournalPoll,
  cmJournalActions,            // Menu of the actions of plugins for the entry under the cursor
  cmJournalPlugins,            // Problems of the plugins found at start, told once the windows are up
  cmJournalReports,            // Commands of actions in the background have ended badly
  cmJournalPluginEvents,       // Descriptors or timers of plugins are due
  cmJournalOpenWatch,          // Broadcast with a JournalWindowRequest of a watch of a plugin
  cmJournalWatches,            // The list of the watches of plugins
  cmJournalWatchesChanged,     // Broadcast: watches have changed, their windows draw again
  cmWatchOpen,                 // Enter of a window of watches, by the status line
  cmWatchRemove,               // Delete there
  cmWatchUniversal,            // U there: the times in UTC or in the local zone
  cmTimelineBars,              // B of a timeline: blocks or Braille, by the status line
  cmTimelineScale,             // L there: linear or logarithmic
  cmTimelineZoom,              // Z there: the selection becomes the range
  cmTimelineZone,              // U there: hours and days of UTC or of the local zone
  cmJournalShadows,            // Window, Shadows...: none, light, medium or dark under the windows
  cmJournalConsole,            // Plugins, Console: the lines of the console of TurboJedi
  cmJournalTimePick   = 1100,  // Since, then Until
  cmJournalTimePreset = 1110,  // One per preset of the time picker
  cmJournalMenu       = 1300   // One per item of the Plugins menu, PLUGIN_MENU_LIMIT of them
};

// Contexts the status line goes by: the windows of watches and the timeline have their own keys
enum
{
  hcWatchList  = 100,
  hcWatchLines = 101,
  hcTimeline   = 102
};

#endif
