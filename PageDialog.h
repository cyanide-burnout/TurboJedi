#ifndef PAGEDIALOG_H
#define PAGEDIALOG_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDialog
#include <tvision/tv.h>

#include <vector>
#include <string>

#include "PageView.h"

// Read-only text in a dialog almost as large as the desktop, Copy puts all of it to the clipboard

class PageDialog : public TDialog
{
  public:

    PageDialog(const TRect& bounds, const char* title, const std::vector<std::string>& lines);

    void handleEvent(TEvent& event) override;

  private:

    PageView* page;

    void copy();
};

#endif
