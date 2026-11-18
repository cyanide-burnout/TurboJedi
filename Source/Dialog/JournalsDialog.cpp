// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TSItem
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TEvent
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_MsgBox
#include "JournalsDialog.h"
#include "DirectoryDialog.h"
#include "Commands.h"

#include <string.h>
#include <sys/stat.h>

// JournalsDialog

JournalsDialog::JournalsDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 58, 16), "Journals")
{
  options |= ofCentered;
  buttons  = new TRadioButtons(TRect(3, 3, 55, 6),
    new TSItem("~T~his machine",
    new TSItem("~A~ll machines there are, like journalctl --merge",
    new TSItem("~D~irectory of journal files", nullptr))));
  line     = new TInputLine(TRect(3, 8, 40, 9), JOURNALS_PATH_LENGTH);

  // Inserted in the order of JournalsData
  insert(buttons);
  insert(new TLabel(TRect(2, 2, 30, 3), "~R~ead the journals of", buttons));
  insert(line);
  insert(new TLabel(TRect(2, 7, 30, 8), "~P~ath of the directory", line));
  insert(new TButton(TRect(42, 8, 55, 10), "~B~rowse...", cmJournalBrowse, bfNormal));
  insert(new TStaticText(TRect(3, 10, 55, 11), "Such as /var/log/journal/remote"));
  insert(new TButton(TRect(16, 12, 28, 14), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(30, 12, 42, 14), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

void JournalsDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

  if ((event.what == evCommand) &&
      (event.message.command == cmJournalBrowse))
  {
    browse();
    clearEvent(event);
  }
}

void JournalsDialog::browse()
{
  ushort scope;
  DirectoryDialog* dialog;

  // From the path there is, or from where journals live
  dialog = new DirectoryDialog((line->data[0] != '\0') ? line->data : "/var/log/journal");

  if (TProgram::deskTop->execView(dialog) == cmOK)
  {
    strncpy(line->data, dialog->selection().c_str(), JOURNALS_PATH_LENGTH - 1);
    line->data[JOURNALS_PATH_LENGTH - 1] = '\0';
    line->drawView();

    // A chosen directory is what the window reads then
    scope = JOURNALS_DIRECTORY;
    buttons->setData(&scope);
    buttons->drawView();
  }

  TObject::destroy(dialog);
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
