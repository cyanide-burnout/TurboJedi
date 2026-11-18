#ifndef LEVELSDIALOG_H
#define LEVELSDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#include <tvision/tv.h>

// Levels to show, the data is the word of levels of the filter: bit N is priority N

class LevelsDialog : public TDialog
{
  public:

    LevelsDialog();
};

#endif
