// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TButton
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TEvent
#include "UpdateDialog.h"
#include "Commands.h"
#include "JediClipboard.h"

#include <algorithm>

#define BUTTONS_WIDTH  44  // Copy URL, Copy command and Close with the gaps between them
#define CLOSE_WIDTH    12  // Close alone
#define CHOICE_WIDTH   33  // Check box of the check on start

// UpdateDialog

UpdateDialog::UpdateDialog(const std::vector<std::string>& lines, const std::string& link, const std::string& command, bool chosen, int room) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 0, 0), "Update")
{
  int x;
  int rows;
  int width;
  int buttons;
  int columns;
  ushort value;
  TRect bounds;
  std::string text;

  this->link    = link;
  this->command = command;

  // Without a link the release is not newer, Close is the only button
  buttons = link.empty() ? CLOSE_WIDTH : BUTTONS_WIDTH;
  width   = std::max(buttons, CHOICE_WIDTH) + 4;

  for (const std::string& line : lines)
  {
    // Frame and a column of room on both sides
    width = std::max(width, strwidth(line) + 6);
  }

  width   = std::min(width, std::max(room, std::max(buttons, CHOICE_WIDTH) + 4));
  columns = width - 6;
  rows    = 0;

  for (const std::string& line : lines)
  {
    // A wrapped line takes the rows it needs and one more, words do not break where the columns end
    rows += (strwidth(line) <= columns) ? 1 : (strwidth(line) / columns + 2);
    text += line + "\n";
  }

  // Not inserted yet, the frame grows along and ofCentered puts the dialog in the middle
  options |= ofCentered;
  bounds   = TRect(0, 0, width, rows + 9);
  changeBounds(bounds);

  insert(new TStaticText(TRect(3, 2, width - 3, rows + 2), text));

  // The choice goes along with every answer of a check, that is when updates are on the mind; the box draws a space
  // in front of its mark, it stands a column to the left so that the mark is under the text
  choice = new TCheckBoxes(TRect(2, rows + 3, 2 + CHOICE_WIDTH, rows + 4), new TSItem("Check for updates on ~s~tart", nullptr));
  value  = chosen ? 1 : 0;
  choice->setData(&value);
  insert(choice);

  x = (width - buttons) / 2;

  if (!link.empty())
  {
    insert(new TButton(TRect(x,      rows + 5, x + 12, rows + 7), "Copy ~U~RL",     cmUpdateCopyLink,    bfNormal));
    insert(new TButton(TRect(x + 14, rows + 5, x + 30, rows + 7), "Copy co~m~mand", cmUpdateCopyCommand, bfNormal));
    x += 32;
  }

  insert(new TButton(TRect(x, rows + 5, x + 12, rows + 7), "~C~lose", cmOK, bfDefault));

  selectNext(False);
}

void UpdateDialog::handleEvent(TEvent& event)
{
  if ((event.what == evCommand) &&
      ((event.message.command == cmUpdateCopyLink) ||
       (event.message.command == cmUpdateCopyCommand)))
  {
    // Exactly the one wanted, without the text around it; the dialog stays for the other one
    CopyClipboardText((event.message.command == cmUpdateCopyLink) ? link : command);
    clearEvent(event);
    return;
  }

  TDialog::handleEvent(event);
}

bool UpdateDialog::startup()
{
  ushort value;

  choice->getData(&value);
  return value != 0;
}
