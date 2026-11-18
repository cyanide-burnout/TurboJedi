#ifndef COLUMNSDIALOG_H
#define COLUMNSDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TCheckBoxes
#include <tvision/tv.h>

// Columns and their options, the data is the word of columns of the filter: JOURNAL_COLUMN_*

class ColumnsDialog : public TDialog
{
  public:

    ColumnsDialog();

    void getData(void* record) override;
    void setData(void* record) override;

  private:

    TCheckBoxes* columns;
    TCheckBoxes* settings;
};

#endif
