#ifndef TEXTDIALOG_H
#define TEXTDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TCheckBoxes
#include <tvision/tv.h>

#define TEXT_LENGTH  256

struct TextData
{
  char text[TEXT_LENGTH];
  ushort options;          // JOURNAL_TEXT_*
};

// Text of the filter with Exclude, or of Find without it; OK does not close it on a broken expression

class TextDialog : public TDialog
{
  public:

    TextDialog(const char* title, const char* label, bool exclude);

    void getData(void* record) override;
    void setData(void* record) override;
    Boolean valid(ushort command) override;

  private:

    bool exclude;
    TInputLine* line;
    TCheckBoxes* boxes;
};

#endif
