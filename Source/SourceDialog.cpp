// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TLabel
#define Uses_TButton
#define Uses_TFilterValidator
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include "SourceDialog.h"
#include "PickDialog.h"
#include "Commands.h"

#include <string.h>

#include <map>

// SourceDialog

SourceDialog::SourceDialog(const std::string& directory) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 66, 23), "Source")
{
  static const char* const labels[] = { "~U~nit", "~I~dentifier", "C~o~mmand", "~T~ransport", "~B~oot", "U~s~er", "~M~achine" };

  int y;
  int field;

  this->directory = directory;
  options        |= ofCentered;

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    // Inserted in the order of SourceData
    y = 2 + field * 2;
    lines[field] = new TInputLine(TRect(15, y, 50, y + 1), JOURNAL_VALUE_LENGTH);
    insert(lines[field]);
    insert(new TLabel(TRect(2, y, 15, y + 1), labels[field], lines[field]));
    insert(new TButton(TRect(51, y, 63, y + 2), "List...", cmJournalSourceList + field, bfNormal));
  }

  // PID follows the fields of the journal
  y = 2 + JOURNAL_FIELD_COUNT * 2;
  lines[JOURNAL_FIELD_COUNT] = new TInputLine(TRect(15, y, 27, y + 1), PID_LENGTH, new TFilterValidator("0123456789"));
  insert(lines[JOURNAL_FIELD_COUNT]);
  insert(new TLabel(TRect(2, y, 15, y + 1), "~P~ID", lines[JOURNAL_FIELD_COUNT]));
  insert(new TButton(TRect(51, y, 63, y + 2), "List...", cmJournalSourceList + JOURNAL_FIELD_COUNT, bfNormal));

  y += 3;
  insert(new TButton(TRect(9, y, 21, y + 2), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(25, y, 37, y + 2), "C~l~ear", cmJournalSourceClear, bfNormal));
  insert(new TButton(TRect(41, y, 53, y + 2), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}

void SourceDialog::handleEvent(TEvent& event)
{
  int field;
  char empty[JOURNAL_VALUE_LENGTH];

  TDialog::handleEvent(event);

  if (event.what == evCommand)
  {
    if ((event.message.command >= cmJournalSourceList) &&
        (event.message.command <= (cmJournalSourceList + JOURNAL_FIELD_COUNT)))
    {
      pick(event.message.command - cmJournalSourceList);
      clearEvent(event);
    }

    if (event.message.command == cmJournalSourceClear)
    {
      memset(empty, 0, sizeof(empty));

      for (field = 0; field <= JOURNAL_FIELD_COUNT; field ++)
      {
        // Any source
        lines[field]->setData(empty);
      }

      clearEvent(event);
    }
  }
}

void SourceDialog::pick(int field)
{
  static const char* const titles[] = { "Units", "Identifiers", "Commands", "Transports", "Boots", "Users", "Machines", "Processes" };

  char value[JOURNAL_VALUE_LENGTH];
  const char* label;
  PickDialog* dialog;
  std::map<std::string, std::string> values;  // Label to value
  std::set<std::string> running;              // Labels of processes alive now
  std::map<std::string, std::string>::iterator item;

  ListJournalValues(directory, field, values, running);

  // Processes of a journal from elsewhere do not run here
  dialog = new PickDialog(titles[field], values, (field == JOURNAL_FIELD_COUNT) ? &running : nullptr, directory.empty());

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    memset(value, 0, sizeof(value));
    strncpy(value, item->second.c_str(), sizeof(value) - 1);
    lines[field]->setData(value);

    // A focused button takes Enter over, back on the field Enter means OK
    lines[field]->select();
  }

  TObject::destroy(dialog);
}
