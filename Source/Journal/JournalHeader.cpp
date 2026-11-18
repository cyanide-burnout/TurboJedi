// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TDrawBuffer
#include "JournalHeader.h"
#include "JournalView.h"

// JournalHeader

JournalHeader::JournalHeader(const TRect& bounds, JournalView* view) :
  TView(bounds)
{
  this->view = view;
  growMode   = gfGrowHiX;
}

void JournalHeader::draw()
{
  TDrawBuffer buffer;
  TColorAttr color;

  // Black on cyan like a table heading, apart from the lines below
  color = TColorAttr(TColorBIOS(0x0), TColorBIOS(0x3));

  buffer.moveChar(0, ' ', color, size.x);
  buffer.moveStr(0, view->heading(), color, size.x);
  writeLine(0, 0, size.x, 1, buffer);
}
