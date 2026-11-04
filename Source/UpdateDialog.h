#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#include <tvision/tv.h>

#include <string>

// Newer release found by Check for updates: its link and the command of the install script, each copied by a button of its own.
// As wide as the text within room columns, a narrower one wraps the lines and the buttons still copy them whole

class UpdateDialog : public TDialog
{
  public:

    UpdateDialog(const std::string& heading, const std::string& link, const std::string& command, int room);

    void handleEvent(TEvent& event) override;

  private:

    std::string link;
    std::string command;
};

#endif
