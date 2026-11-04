#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TCheckBoxes
#include <tvision/tv.h>

#include <vector>
#include <string>

// Answer of Check for updates with the choice of a check on start. A newer release comes with its link and the command
// of the install script, each copied by a button of its own; without a link the release is not newer and Close stays alone.
// As wide as the text within room columns, a narrower one wraps the lines and the buttons still copy them whole

class UpdateDialog : public TDialog
{
  public:

    UpdateDialog(const std::vector<std::string>& lines, const std::string& link, const std::string& command, bool chosen, int room);

    void handleEvent(TEvent& event) override;
    bool startup();  // Check on start as chosen in the box

  private:

    std::string link;
    std::string command;
    TCheckBoxes* choice;
};

#endif
