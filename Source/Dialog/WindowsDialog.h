#ifndef WINDOWSDIALOG_H
#define WINDOWSDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TWindow
#define Uses_TInputLine
#define Uses_TListBox
#include <tvision/tv.h>

#include <vector>
#include <string>

// Windows of the desktop to go to, the focused one first and the one before it under the cursor

class WindowsDialog : public TDialog
{
  public:

    WindowsDialog(const std::vector<TWindow*>& windows);

    void handleEvent(TEvent& event) override;
    void shutDown() override;
    TWindow* selection();

  private:

    std::vector<TWindow*> windows;
    std::vector<std::string> labels;
    std::vector<TWindow*> shown;  // Windows of the lines of the list
    std::string text;
    TInputLine* line;
    TListBox* box;

    void update();
};

#endif
