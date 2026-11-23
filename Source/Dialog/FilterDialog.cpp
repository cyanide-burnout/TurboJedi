// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TSItem
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_MsgBox
#include "FilterDialog.h"
#include "PickDialog.h"
#include "PickButton.h"
#include "TimeDialog.h"
#include "TextDialog.h"
#include "Commands.h"
#include "JournalMatcher.h"

#include <stdlib.h>
#include <string.h>

#include <map>
#include <set>

#define NAME_LENGTH  65

// The names a field of Source and PID are typed by, the keys of the title first
static const struct { const char* name; int field; } aliases[] =
{
  { "unit",       JOURNAL_FIELD_UNIT       },
  { "id",         JOURNAL_FIELD_IDENTIFIER },
  { "identifier", JOURNAL_FIELD_IDENTIFIER },
  { "comm",       JOURNAL_FIELD_COMMAND    },
  { "command",    JOURNAL_FIELD_COMMAND    },
  { "via",        JOURNAL_FIELD_TRANSPORT  },
  { "transport",  JOURNAL_FIELD_TRANSPORT  },
  { "boot",       JOURNAL_FIELD_BOOT       },
  { "user",       JOURNAL_FIELD_USER       },
  { "machine",    JOURNAL_FIELD_MACHINE    },
  { "pid",        JOURNAL_FIELD_COUNT      }
};

static const char* const keys[] = { "unit", "id", "comm", "via", "boot", "user", "machine", "pid" };

// A field of Source or PID by its name, -1 for any other field
static int FindAlias(const std::string& name)
{
  for (const auto& alias : aliases)
  {
    if (strcasecmp(name.c_str(), alias.name) == 0)
    {
      return alias.field;
    }
  }

  return -1;
}

// setData() of an input line reads the whole size of its data, a short string would be read past its end
static void SetLine(TInputLine* line, const std::string& text)
{
  std::vector<char> data(line->dataSize(), '\0');

  memcpy(data.data(), text.data(), std::min(text.size(), data.size() - 1));
  line->setData(data.data());
}

static std::string Trim(const char* text)
{
  std::string value;
  size_t start;
  size_t end;

  value = text;
  start = value.find_first_not_of(' ');
  end   = value.find_last_not_of(' ');

  return (start == std::string::npos) ? std::string() : value.substr(start, end - start + 1);
}

// RuleList

RuleList::RuleList(const TRect& bounds, TScrollBar* bar, const std::vector<std::string>& items) :
  TListViewer(bounds, 1, nullptr, bar),
  items(items)
{
}

void RuleList::getText(char* dest, short item, short limit)
{
  if ((item < 0) ||
      ((size_t)item >= items.size()))
  {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, items[item].c_str(), limit - 1);
  dest[limit - 1] = '\0';
}

void RuleList::handleEvent(TEvent& event)
{
  if ((event.what == evKeyDown) &&
      (event.keyDown.keyCode == kbDel))
  {
    message(owner, evCommand, cmFilterRemove, this);
    clearEvent(event);
    return;
  }

  TListViewer::handleEvent(event);
}

void RuleList::selectItem(short item)
{
  if ((item >= 0) &&
      (item < range))
  {
    focusItem(item);
    message(owner, evCommand, cmFilterEdit, this);
  }
}

// FilterDialog

FilterDialog::FilterDialog(const JournalFilter& filter, int start) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 76, 22), "Filter")
{
  int field;
  ushort value;
  TScrollBar* bar;

  TDialog::options |= ofCentered;
  journals = filter;

  name     = new TInputLine(TRect(9, 2, 23, 3), NAME_LENGTH);
  negation = new TCheckBoxes(TRect(28, 2, 36, 3), new TSItem("not", nullptr));
  this->value = new TInputLine(TRect(37, 2, 61, 3), JOURNAL_VALUE_LENGTH);

  insert(name);
  insert(new TLabel(TRect(2, 2, 8, 3), "~F~ield", name));
  insert(new PickButton(TRect(23, 2, 26, 3), name, cmFilterFields));
  insert(negation);
  insert(this->value);
  insert(new PickButton(TRect(61, 2, 64, 3), this->value, cmFilterValues));
  insert(new TButton(TRect(66, 2, 74, 4), "~A~dd", cmFilterAddField, bfNormal));

  line  = new TInputLine(TRect(9, 4, 35, 5), TEXT_LENGTH);
  modes = new TCheckBoxes(TRect(35, 4, 66, 5),
    new TSItem("not",
    new TSItem("~R~egex",
    new TSItem("~C~ase", nullptr))));

  insert(line);
  insert(new TLabel(TRect(2, 4, 8, 5), "~T~ext", line));
  insert(modes);
  insert(new TButton(TRect(66, 4, 74, 6), "A~d~d", cmFilterAddText, bfNormal));

  bar    = new TScrollBar(TRect(46, 7, 47, 15));
  list   = new RuleList(TRect(3, 7, 46, 15), bar, items);
  levels = new TCheckBoxes(TRect(50, 7, 72, 15),
    new TSItem("Emergency",
    new TSItem("Alert",
    new TSItem("Critical",
    new TSItem("Error",
    new TSItem("Warning",
    new TSItem("Notice",
    new TSItem("Informational",
    new TSItem("Debug", nullptr)))))))));

  insert(bar);
  insert(list);
  insert(new TLabel(TRect(2, 6, 48, 7), "R~u~les, an entry has to pass all of them", list));
  // The letters of the levels would take the keys of the labels, Alt-V and the arrows go to them
  insert(levels);
  insert(new TLabel(TRect(49, 6, 60, 7), "Le~v~els", levels));

  since = new TInputLine(TRect(9, 16, 30, 17), TIME_LENGTH);
  until = new TInputLine(TRect(38, 16, 59, 17), TIME_LENGTH);

  insert(since);
  insert(new TLabel(TRect(2, 16, 8, 17), "~S~ince", since));
  insert(until);
  insert(new TLabel(TRect(31, 16, 37, 17), "U~n~til", until));
  insert(new TButton(TRect(61, 16, 74, 18), "T~i~me...", cmFilterTime, bfNormal));

  insert(new TButton(TRect(3,  19, 15, 21), "~O~K",        cmOK,           bfDefault));
  insert(new TButton(TRect(17, 19, 29, 21), "Re~m~ove",    cmFilterRemove, bfNormal));
  insert(new TButton(TRect(31, 19, 43, 21), "C~l~ear",     cmFilterClear,  bfNormal));
  insert(new TButton(TRect(45, 19, 59, 21), "Sim~p~le...", cmFilterSimple, bfNormal));
  insert(new TButton(TRect(61, 19, 73, 21), "Cancel",      cmCancel,       bfNormal));

  // The rules in the order the title writes them
  for (field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (!filter.values[field].empty())
    {
      rules.push_back(Rule { field, false, filter.values[field], JournalText { } });
    }
  }

  if (filter.pid != 0)
  {
    rules.push_back(Rule { JOURNAL_FIELD_COUNT, false, std::to_string(filter.pid), JournalText { } });
  }

  for (const std::string& match : filter.matches)
  {
    rules.push_back(Rule { -1, IsJournalExclusion(match), match, JournalText { } });
  }

  for (const JournalText& text : filter.texts)
  {
    rules.push_back(Rule { -2, (text.mode & JOURNAL_TEXT_EXCLUDE) != 0, "", text });
  }

  value = filter.levels;
  levels->setData(&value);
  SetLine(since, filter.since);
  SetLine(until, filter.until);

  update();
  ((start == FILTER_LEVELS) ? (TView*)levels : (TView*)name)->select();
}

void FilterDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

  if (event.what != evCommand)
  {
    return;
  }

  switch (event.message.command)
  {
    case cmFilterFields:    pickField();     break;
    case cmFilterValues:    pickValue();     break;
    case cmFilterAddField:  addField(false); break;
    case cmFilterAddText:   addText(false);  break;
    case cmFilterRemove:    remove();        break;
    case cmFilterEdit:      edit();          break;
    case cmFilterClear:     clear();         break;
    case cmFilterTime:      pickTime();      break;
    case cmFilterSimple:    endModal(cmFilterSimple); break;
    default:                                 return;
  }

  clearEvent(event);
}

Boolean FilterDialog::valid(ushort command)
{
  uint64_t stamp;
  char text[TIME_LENGTH];

  if (command != cmOK)
  {
    // Cancel goes as it is
    return TDialog::valid(command);
  }

  // What is typed and not added goes along, a unit typed and OK work as they always have
  if ((!addField(true)) ||
      (!addText(true)))
  {
    return False;
  }

  for (TInputLine* time : { since, until })
  {
    time->getData(text);

    if ((text[0] != '\0') &&
        (!ParseJournalTime(text, &stamp)))
    {
      time->select();
      fail("Unknown time format");
      return False;
    }
  }

  return TDialog::valid(command);
}

void FilterDialog::apply(JournalFilter& filter) const
{
  ushort value;
  char text[TIME_LENGTH];

  for (std::string& field : filter.values)
  {
    field.clear();
  }

  filter.pid = 0;
  filter.matches.clear();
  filter.texts.clear();

  for (const Rule& rule : rules)
  {
    switch (rule.field)
    {
      case -2:                   filter.texts.push_back(rule.text);                 break;
      case -1:                   filter.matches.push_back(rule.value);              break;
      case JOURNAL_FIELD_COUNT:  filter.pid = atoi(rule.value.c_str());             break;
      default:                   filter.values[rule.field] = rule.value;           break;
    }
  }

  levels->getData(&value);
  filter.levels = value;
  since->getData(text);
  filter.since = text;
  until->getData(text);
  filter.until = text;
}

void FilterDialog::update()
{
  items.clear();

  for (const Rule& rule : rules)
  {
    items.push_back(describe(rule));
  }

  list->setRange(items.size());
  list->drawView();
}

std::string FilterDialog::describe(const Rule& rule) const
{
  switch (rule.field)
  {
    case -2:  return FormatJournalText(rule.text);
    case -1:  return rule.value;
    default:  return std::string(keys[rule.field]) + "=" + rule.value;
  }
}

bool FilterDialog::addField(bool quiet)
{
  int field;
  bool any;
  bool taken;
  ushort checked;
  std::string key;
  std::string text;
  std::string match;
  char data[JOURNAL_VALUE_LENGTH + NAME_LENGTH];

  name->getData(data);
  key = Trim(data);
  this->value->getData(data);
  text = data;
  negation->getData(&checked);

  if (text.empty())
  {
    // Nothing to add, OK goes on; Add tells what is missing
    if (!quiet)
    {
      this->value->select();
      fail("Type or choose a value of the field");
    }

    return quiet;
  }

  if (key.empty())
  {
    name->select();
    fail("Type or choose a field: unit, id, comm, via, boot, user, machine, pid or a field of the journal");
    return false;
  }

  field = FindAlias(key);
  any   = (text == "*");

  if ((field == JOURNAL_FIELD_UNIT) &&
      (!any) &&
      (text.find('.') == std::string::npos))
  {
    // Like systemctl and journalctl do
    text += ".service";
  }

  if ((field == JOURNAL_FIELD_COUNT) &&
      (!any) &&
      ((text.find_first_not_of("0123456789") != std::string::npos) || (atoi(text.c_str()) <= 0)))
  {
    this->value->select();
    fail("PID is a number");
    return false;
  }

  // The first value of a field of Source shown is the field itself, as the Source dialog has set it; another value of it,
  // a value left out and * of any value are raw matches
  if ((field >= 0) && (!checked) && (!any))
  {
    taken = false;

    for (const Rule& rule : rules)
    {
      taken |= (rule.field == field);
    }

    if (!taken)
    {
      rules.push_back(Rule { field, false, text, JournalText { } });
      SetLine(this->value, "");
      update();
      return true;
    }
  }

  if ((field == JOURNAL_FIELD_USER) &&
      (!any))
  {
    // The journal knows numbers only
    if ((text = GetJournalUserIdentifier(text)).empty())
    {
      this->value->select();
      fail("There is no such user");
      return false;
    }
  }

  key   = (field == JOURNAL_FIELD_COUNT) ? "_PID" : ((field >= 0) ? JournalFieldNames[field] : key);
  match = key + (checked ? "!=" : "=") + text;

  if (!CheckJournalMatch(match))
  {
    name->select();
    fail("A field of the journal is named by A-Z, 0-9 and _, not a digit first");
    return false;
  }

  rules.push_back(Rule { -1, checked != 0, match, JournalText { } });
  SetLine(this->value, "");
  update();
  return true;
}

bool FilterDialog::addText(bool quiet)
{
  ushort checked;
  JournalText text;
  JournalMatcher probe;
  char data[TEXT_LENGTH];

  line->getData(data);
  modes->getData(&checked);

  if (data[0] == '\0')
  {
    if (!quiet)
    {
      line->select();
      fail("Type a text");
    }

    return quiet;
  }

  text.text = data;
  text.mode =
    ((checked & 1) ? JOURNAL_TEXT_EXCLUDE    : 0) |
    ((checked & 2) ? JOURNAL_TEXT_EXPRESSION : 0) |
    ((checked & 4) ? JOURNAL_TEXT_SENSITIVE  : 0);

  if (!probe.set(text.text, text.mode))
  {
    // Stays open with what has been typed
    line->select();
    fail("Broken regular expression");
    return false;
  }

  rules.push_back(Rule { -2, (text.mode & JOURNAL_TEXT_EXCLUDE) != 0, "", text });
  SetLine(line, "");
  update();
  return true;
}

void FilterDialog::remove()
{
  short focused;

  focused = list->focused;

  if ((focused < 0) ||
      ((size_t)focused >= rules.size()))
  {
    return;
  }

  rules.erase(rules.begin() + focused);
  update();
  list->focusItem(std::min<short>(focused, std::max<short>(0, (short)rules.size() - 1)));
}

void FilterDialog::edit()
{
  short focused;
  size_t equal;
  ushort checked;
  Rule rule;

  focused = list->focused;

  if ((focused < 0) ||
      ((size_t)focused >= rules.size()))
  {
    return;
  }

  rule = rules[focused];

  // What is typed in the line of the rule and not added yet goes in first instead of being lost
  if (!((rule.field == -2) ? addText(true) : addField(true)))
  {
    return;
  }

  rules.erase(rules.begin() + focused);
  update();
  list->focusItem(std::min<short>(focused, std::max<short>(0, (short)rules.size() - 1)));

  if (rule.field == -2)
  {
    checked =
      ((rule.text.mode & JOURNAL_TEXT_EXCLUDE)    ? 1 : 0) |
      ((rule.text.mode & JOURNAL_TEXT_EXPRESSION) ? 2 : 0) |
      ((rule.text.mode & JOURNAL_TEXT_SENSITIVE)  ? 4 : 0);

    SetLine(line, rule.text.text);
    modes->setData(&checked);
    line->select();
    return;
  }

  checked = rule.exclude ? 1 : 0;

  if (rule.field == -1)
  {
    // FIELD=VALUE or FIELD!=VALUE, a name never holds = or !
    equal = rule.value.find('=');
    SetLine(name, rule.value.substr(0, rule.exclude ? (equal - 1) : equal));
    SetLine(this->value, rule.value.substr(equal + 1));
  }
  else
  {
    SetLine(name, keys[rule.field]);
    SetLine(this->value, rule.value);
  }

  negation->setData(&checked);
  this->value->select();
}

void FilterDialog::clear()
{
  ushort value;

  value = 0xff;
  rules.clear();
  SetLine(name, "");
  SetLine(this->value, "");
  SetLine(line, "");
  SetLine(since, "");
  SetLine(until, "");
  levels->setData(&value);
  update();
}

void FilterDialog::pickField()
{
  const char* label;
  PickDialog* dialog;
  std::set<std::string> names;
  std::map<std::string, std::string> values;
  std::map<std::string, std::string>::iterator item;

  // The keys of the title for the fields of Source and PID, the names of the journal for the rest
  for (int field = 0; field <= JOURNAL_FIELD_COUNT; field ++)
  {
    values.emplace(std::string(keys[field]) + "  " + ((field < JOURNAL_FIELD_COUNT) ? JournalFieldNames[field] : "_PID"), keys[field]);
  }

  ListJournalFields(journals, names);

  for (const std::string& field : names)
  {
    values.emplace(field, field);
  }

  dialog = new PickDialog("Fields", values);

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    SetLine(name, item->second);
    this->value->select();
  }

  TObject::destroy(dialog);
}

void FilterDialog::pickValue()
{
  static const char* const titles[] = { "Units", "Identifiers", "Commands", "Transports", "Boots", "Users", "Machines", "Processes" };

  int field;
  std::string key;
  const char* label;
  PickDialog* dialog;
  std::set<std::string> running;
  std::map<std::string, std::string> values;
  std::map<std::string, std::string>::iterator item;
  char data[JOURNAL_VALUE_LENGTH + NAME_LENGTH];

  name->getData(data);
  key = Trim(data);

  if (key.empty())
  {
    name->select();
    fail("Choose a field first");
    return;
  }

  if ((field = FindAlias(key)) >= 0)
  {
    ListJournalValues(journals, field, values, running);
  }
  else
  {
    ListJournalFieldValues(journals, key, values);
  }

  // Processes of a journal from elsewhere do not run here
  dialog = new PickDialog((field >= 0) ? titles[field] : key.c_str(), values,
    (field == JOURNAL_FIELD_COUNT) ? &running : nullptr, journals.directory.empty() && !journals.merge);

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    SetLine(this->value, item->second);
    this->value->select();
  }

  TObject::destroy(dialog);
}

void FilterDialog::pickTime()
{
  bool utc;
  std::string from;
  std::string to;
  std::string written;
  TimeData data;
  char text[TIME_LENGTH];

  // In the zone the range is written in, as Ctrl-T opens it
  memset(&data, 0, sizeof(data));
  since->getData(text);
  from = text;
  until->getData(text);
  to = text;

  data.utc = GetJournalTimeZone(from, &utc) ? utc : (GetJournalTimeZone(to, &utc) ? utc : ((journals.columns & JOURNAL_COLUMN_UTC) != 0));
  ConvertJournalTime(from, false, data.utc, written);
  strncpy(data.since, written.c_str(), sizeof(data.since) - 1);
  ConvertJournalTime(to, false, data.utc, written);
  strncpy(data.until, written.c_str(), sizeof(data.until) - 1);

  if (TProgram::application->executeDialog(new TimeDialog(), &data) == cmOK)
  {
    // Checked once more by OK of this window
    SetLine(since, data.since);
    SetLine(until, data.until);
  }
}

void FilterDialog::fail(const char* text)
{
  messageBox(text, mfError | mfOKButton);
}
