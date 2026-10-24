#ifndef PICKDIALOG_H
#define PICKDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TListBox
#define Uses_TCheckBoxes
#include <tvision/tv.h>

#include <map>
#include <set>
#include <vector>
#include <string>

class PickDialog : public TDialog
{
  public:

    // With running labels a check box leaves the others out, checked tells how it starts
    PickDialog(const char* title, const std::map<std::string, std::string>& values, const std::set<std::string>* running = nullptr, bool checked = false);

    void handleEvent(TEvent& event) override;
    const char* selection();

  private:

    std::vector<std::string> labels;
    const std::set<std::string>* running;
    std::string text;
    ushort only;
    TInputLine* line;
    TListBox* box;
    TCheckBoxes* boxes;

    void update();
};

#endif
