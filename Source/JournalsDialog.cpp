// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TSItem
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_MsgBox
#include "JournalsDialog.h"

#include <sys/stat.h>

// JournalsDialog

JournalsDialog::JournalsDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 58, 16), "Journals")
{
  TInputLine* line;
  TRadioButtons* buttons;

  options |= ofCentered;
  buttons  = new TRadioButtons(TRect(3, 3, 55, 6),
    new TSItem("~T~his machine",
    new TSItem("~A~ll machines there are, like journalctl --merge",
    new TSItem("~D~irectory of journal files", nullptr))));
  line     = new TInputLine(TRect(3, 8, 55, 9), JOURNALS_PATH_LENGTH);

  // Inserted in the order of JournalsData
  insert(buttons);
  insert(new TLabel(TRect(2, 2, 30, 3), "~R~ead the journals of", buttons));
  insert(line);
  insert(new TLabel(TRect(2, 7, 30, 8), "~P~ath of the directory", line));
  insert(new TStaticText(TRect(3, 10, 55, 11), "Such as /var/log/journal/remote"));
  insert(new TButton(TRect(16, 12, 28, 14), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(30, 12, 42, 14), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

Boolean JournalsDialog::valid(ushort command)
{
  struct stat status;
  JournalsData data;

  if (command != cmOK)
  {
    // Cancel goes as it is
    return TDialog::valid(command);
  }

  getData(&data);

  if ((data.scope == JOURNALS_DIRECTORY) &&
      ((stat(data.directory, &status) != 0) || !S_ISDIR(status.st_mode)))
  {
    // Stays open with what has been typed
    messageBox("There is no such directory", mfError | mfOKButton);
    return False;
  }

  return TDialog::valid(command);
}
