#ifndef DIRECTORYDIALOG_H
#define DIRECTORYDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TListBox
#include <tvision/tv.h>

#include <string>

// A directory to choose: Space or a double click opens the one in the list, OK takes the path in the line;
// unlike TChDirDialog it leaves the current directory of the process alone

class DirectoryDialog : public TDialog
{
  public:

    DirectoryDialog(const std::string& path);

    void handleEvent(TEvent& event) override;
    void shutDown() override;
    std::string selection();

  private:

    TInputLine* line;
    TListBox* box;

    void load(const std::string& path);
};

#endif
