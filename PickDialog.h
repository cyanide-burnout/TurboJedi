#ifndef PICKDIALOG_H
#define PICKDIALOG_H

#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TListBox
#include <tvision/tv.h>

#include <map>
#include <vector>
#include <string>

class PickDialog : public TDialog
{
  public:

    PickDialog(const char* title, const std::map<std::string, std::string>& values);

    void handleEvent(TEvent& event) override;
    const char* selection();

  private:

    std::vector<std::string> labels;
    std::string text;
    TInputLine* line;
    TListBox* box;

    void update();
};

#endif
