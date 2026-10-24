#ifndef SOURCEDIALOG_H
#define SOURCEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#include <tvision/tv.h>

#include <string>

#include "JournalTools.h"

#define PID_LENGTH  16

struct SourceData
{
  char values[JOURNAL_FIELD_COUNT][JOURNAL_VALUE_LENGTH];
  char pid[PID_LENGTH];
};

class SourceDialog : public TDialog
{
  public:

    SourceDialog(const std::string& directory);

    void handleEvent(TEvent& event) override;

  private:

    std::string directory;
    TInputLine* lines[JOURNAL_FIELD_COUNT + 1];  // The last one is PID

    void pick(int field);
};

#endif
