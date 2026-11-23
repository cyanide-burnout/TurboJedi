#ifndef PICKBUTTON_H
#define PICKBUTTON_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TView
#define Uses_TInputLine
#include <tvision/tv.h>

// The arrow of a list beside an input line, one row as the one of a history: a click or Down in the line sends the command
// to the dialog, which opens the list

class PickButton : public TView
{
  public:

    PickButton(const TRect& bounds, TInputLine* link, ushort command);

    void draw() override;
    void handleEvent(TEvent& event) override;
    TPalette& getPalette() const override;

  private:

    TInputLine* link;
    ushort command;
};

#endif
