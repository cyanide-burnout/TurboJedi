// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TGroup
#include "PickButton.h"

// The colors of the arrow and the sides of a history, and its look: the arrow between half blocks of CP437
#define cpPickButton  "\x16\x17"
#define PICK_ICON     "\xDE~\x19~\xDD"

// PickButton

PickButton::PickButton(const TRect& bounds, TInputLine* link, ushort command) :
  TView(bounds)
{
  this->link    = link;
  this->command = command;
  options      |= ofPostProcess;
}

void PickButton::draw()
{
  TDrawBuffer buffer;

  buffer.moveCStr(0, PICK_ICON, getColor(0x0102));
  writeLine(0, 0, size.x, size.y, buffer);
}

void PickButton::handleEvent(TEvent& event)
{
  TView::handleEvent(event);

  if ((event.what == evMouseDown) ||
      ((event.what == evKeyDown) && (ctrlToArrow(event.keyDown.keyCode) == kbDown) && (link->state & sfFocused)))
  {
    if (link->focus())
    {
      // The dialog opens its list
      message(owner, evCommand, command, this);
    }

    clearEvent(event);
  }
}

TPalette& PickButton::getPalette() const
{
  static TPalette palette(cpPickButton, sizeof(cpPickButton) - 1);

  return palette;
}
