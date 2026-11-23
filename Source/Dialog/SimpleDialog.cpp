// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TLabel
#define Uses_TButton
#define Uses_TSItem
#define Uses_MsgBox
#define Uses_TStaticText
#define Uses_TFilterValidator
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include "SimpleDialog.h"
#include "PickDialog.h"
#include "PickButton.h"
#include "TextDialog.h"
#include "Commands.h"
#include "JournalMatcher.h"

#include <stdlib.h>
#include <string.h>

#include <map>
#include <vector>

// setData() of an input line reads the whole size of its data
static void SetLine(TInputLine* line, const std::string& text)
{
  std::vector<char> data(line->dataSize(), '\0');

  memcpy(data.data(), text.data(), std::min(text.size(), data.size() - 1));
  line->setData(data.data());
}

bool FitsSimpleFilter(const JournalFilter& filter)
{
  return filter.matches.empty() && (filter.texts.size() <= 1);
}

// SimpleDialog

SimpleDialog::SimpleDialog(const JournalFilter& filter) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 66, 19), "Filter")
{
  static const char* const labels[] = { "~U~nit", "~I~dentifier", "~C~ommand", "~T~ransport", "~B~oot", "U~s~er", "~M~achine", "~P~ID" };

  int y;
  int field;
  ushort value;
  std::string told;

  journals = filter;
  options |= ofCentered;

  for (field = 0; field <= JOURNAL_FIELD_COUNT; field ++)
  {
    // A row each, PID last; the arrow or Down opens the list of the values in the journal
    y = 2 + field;
    lines[field] = (field < JOURNAL_FIELD_COUNT) ?
      new TInputLine(TRect(15, y, 59, y + 1), JOURNAL_VALUE_LENGTH) :
      new TInputLine(TRect(15, y, 27, y + 1), PID_LENGTH, new TFilterValidator("0123456789"));
    insert(lines[field]);
    insert(new TLabel(TRect(2, y, 15, y + 1), labels[field], lines[field]));
    insert(new PickButton(TRect((field < JOURNAL_FIELD_COUNT) ? 59 : 27, y, (field < JOURNAL_FIELD_COUNT) ? 62 : 30, y + 1), lines[field], cmFilterList + field));
  }

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    SetLine(lines[field], filter.values[field]);
  }

  SetLine(lines[JOURNAL_FIELD_COUNT], (filter.pid != 0) ? std::to_string(filter.pid) : "");

  line  = new TInputLine(TRect(15, 11, 59, 12), TEXT_LENGTH);
  modes = new TCheckBoxes(TRect(15, 12, 59, 13),
    new TSItem("~N~ot",
    new TSItem("Re~g~ex",
    new TSItem("C~a~se", nullptr))));

  insert(line);
  insert(new TLabel(TRect(2, 11, 15, 12), "T~e~xt", line));
  insert(modes);

  if (!filter.texts.empty())
  {
    value =
      ((filter.texts[0].mode & JOURNAL_TEXT_EXCLUDE)    ? 1 : 0) |
      ((filter.texts[0].mode & JOURNAL_TEXT_EXPRESSION) ? 2 : 0) |
      ((filter.texts[0].mode & JOURNAL_TEXT_SENSITIVE)  ? 4 : 0);
    SetLine(line, filter.texts[0].text);
    modes->setData(&value);
  }

  // What of the filter this window does not show
  told = "Advanced... makes rules of any field, to keep or to leave out";

  if ((filter.levels != 0xff) || !filter.since.empty() || !filter.until.empty())
  {
    told = "The levels and the times of Advanced... stay as they are";
  }

  insert(new TStaticText(TRect(3, 14, 63, 15), told.c_str()));

  insert(new TButton(TRect(3,  16, 15, 18), "~O~K",          cmOK,          bfDefault));
  insert(new TButton(TRect(17, 16, 33, 18), "A~d~vanced...", cmFilterRules, bfNormal));
  insert(new TButton(TRect(35, 16, 47, 18), "C~l~ear",       cmFilterClear, bfNormal));
  insert(new TButton(TRect(49, 16, 61, 18), "Cancel",        cmCancel,      bfNormal));
  selectNext(False);
}

void SimpleDialog::handleEvent(TEvent& event)
{
  int field;

  TDialog::handleEvent(event);

  if (event.what != evCommand)
  {
    return;
  }

  if ((event.message.command >= cmFilterList) &&
      (event.message.command <= (cmFilterList + JOURNAL_FIELD_COUNT)))
  {
    pick(event.message.command - cmFilterList);
    clearEvent(event);
    return;
  }

  if (event.message.command == cmFilterRules)
  {
    // The whole filter with what has been typed here
    endModal(cmFilterRules);
    clearEvent(event);
    return;
  }

  if (event.message.command == cmFilterClear)
  {
    for (field = 0; field <= JOURNAL_FIELD_COUNT; field ++)
    {
      // Any source
      SetLine(lines[field], "");
    }

    SetLine(line, "");

    clearEvent(event);
  }
}

Boolean SimpleDialog::valid(ushort command)
{
  ushort value;
  JournalMatcher probe;
  char data[TEXT_LENGTH];

  if ((command == cmOK) ||
      (command == cmFilterRules))
  {
    line->getData(data);
    modes->getData(&value);

    if (!probe.set(data, (value & 2) ? JOURNAL_TEXT_EXPRESSION : 0))
    {
      // Stays open with what has been typed
      line->select();
      messageBox("Broken regular expression", mfError | mfOKButton);
      return False;
    }
  }

  return TDialog::valid(command);
}

void SimpleDialog::apply(JournalFilter& filter) const
{
  int field;
  ushort value;
  char data[TEXT_LENGTH];

  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    lines[field]->getData(data);
    filter.values[field] = data;
  }

  if ((!filter.values[JOURNAL_FIELD_UNIT].empty()) &&
      (filter.values[JOURNAL_FIELD_UNIT].find('.') == std::string::npos))
  {
    // Like systemctl and journalctl do
    filter.values[JOURNAL_FIELD_UNIT] += ".service";
  }

  lines[JOURNAL_FIELD_COUNT]->getData(data);
  filter.pid = atoi(data);

  line->getData(data);
  modes->getData(&value);
  filter.texts.clear();

  if (data[0] != '\0')
  {
    filter.texts.push_back(JournalText { data, (uint16_t)(
      ((value & 1) ? JOURNAL_TEXT_EXCLUDE    : 0) |
      ((value & 2) ? JOURNAL_TEXT_EXPRESSION : 0) |
      ((value & 4) ? JOURNAL_TEXT_SENSITIVE  : 0)) });
  }
}

void SimpleDialog::pick(int field)
{
  static const char* const titles[] = { "Units", "Identifiers", "Commands", "Transports", "Boots", "Users", "Machines", "Processes" };

  const char* label;
  PickDialog* dialog;
  std::map<std::string, std::string> values;  // Label to value
  std::set<std::string> running;              // Labels of processes alive now
  std::map<std::string, std::string>::iterator item;

  ListJournalValues(journals, field, values, running);

  // Processes of a journal from elsewhere do not run here
  dialog = new PickDialog(titles[field], values, (field == JOURNAL_FIELD_COUNT) ? &running : nullptr, journals.directory.empty() && !journals.merge);

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    SetLine(lines[field], item->second);

    // A focused button takes Enter over, back on the field Enter means OK
    lines[field]->select();
  }

  TObject::destroy(dialog);
}
