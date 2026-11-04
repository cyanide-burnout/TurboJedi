// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TButton
#define Uses_TStaticText
#define Uses_TEvent
#include "UpdateDialog.h"
#include "Commands.h"
#include "JediClipboard.h"

#include <vector>
#include <algorithm>

#define BUTTONS_WIDTH  44  // Copy URL, Copy command and Close with the gaps between them

// UpdateDialog

UpdateDialog::UpdateDialog(const std::string& heading, const std::string& link, const std::string& command, int room) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 0, 0), "Update")
{
  int x;
  int rows;
  int width;
  int columns;
  TRect bounds;
  std::string text;
  std::vector<std::string> lines;

  this->link    = link;
  this->command = command;

  lines.push_back(heading);
  lines.push_back("");
  lines.push_back("The release and its packages:");
  lines.push_back("  " + link);
  lines.push_back("");
  lines.push_back("Debian, Ubuntu, Fedora, RHEL, Amazon Linux, openSUSE and Arch update with the install script:");
  lines.push_back("  " + command);

  width = BUTTONS_WIDTH;

  for (const std::string& line : lines)
  {
    // Frame and a column of room on both sides
    width = std::max(width, strwidth(line) + 6);
  }

  width   = std::min(width, std::max(room, BUTTONS_WIDTH + 4));
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
  bounds   = TRect(0, 0, width, rows + 7);
  changeBounds(bounds);

  insert(new TStaticText(TRect(3, 2, width - 3, rows + 2), text));

  x = (width - BUTTONS_WIDTH) / 2;

  insert(new TButton(TRect(x,      rows + 3, x + 12, rows + 5), "Copy ~U~RL",     cmUpdateCopyLink,    bfNormal));
  insert(new TButton(TRect(x + 14, rows + 3, x + 30, rows + 5), "Copy co~m~mand", cmUpdateCopyCommand, bfNormal));
  insert(new TButton(TRect(x + 32, rows + 3, x + 44, rows + 5), "~C~lose",        cmOK,                bfDefault));

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
