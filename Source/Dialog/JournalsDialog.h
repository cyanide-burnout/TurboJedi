#ifndef JOURNALSDIALOG_H
#define JOURNALSDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TRadioButtons
#include <tvision/tv.h>

#define JOURNALS_THIS       0
#define JOURNALS_ALL        1
#define JOURNALS_DIRECTORY  2

#define JOURNALS_PATH_LENGTH  256

struct JournalsData
{
  ushort scope;                         // JOURNALS_*
  char directory[JOURNALS_PATH_LENGTH];
};

// Journals a window reads: of this machine, of every machine there is, or of a directory; OK does not close it on a
// directory that is not there

class JournalsDialog : public TDialog
{
  public:

    JournalsDialog();

    void handleEvent(TEvent& event) override;
    Boolean valid(ushort command) override;

  private:

    TRadioButtons* buttons;
    TInputLine* line;

    void browse();
};

#endif
