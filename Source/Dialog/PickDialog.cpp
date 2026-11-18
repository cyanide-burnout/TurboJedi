// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStringCollection
#define Uses_TSItem
#define Uses_TEvent
#define Uses_TKeys
#include "PickDialog.h"

#include <string.h>

#define FILTER_LENGTH  128

// PickDialog

PickDialog::PickDialog(const char* title, const std::map<std::string, std::string>& values, const std::set<std::string>* running, bool checked) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 60, (running != nullptr) ? 25 : 23), title)
{
  int y;
  TScrollBar* bar;

  this->running = running;

  for (const std::pair<const std::string, std::string>& item : values)
  {
    // Labels are what the list shows and what the filter looks for
    labels.push_back(item.first);
  }

  // The check box gets rows of its own between the list and the buttons
  options |= ofCentered;
  only     = checked;
  y        = (running != nullptr) ? 22 : 20;
  line     = new TInputLine(TRect(11, 2, 57, 3), FILTER_LENGTH);
  bar      = new TScrollBar(TRect(57, 4, 58, 19));
  box      = new TListBox(TRect(2, 4, 57, 19), 1, bar);
  boxes    = nullptr;

  insert(line);
  insert(new TLabel(TRect(2, 2, 11, 3), "~F~ilter", line));
  insert(bar);
  insert(box);

  if (running != nullptr)
  {
    boxes = new TCheckBoxes(TRect(2, 20, 22, 21), new TSItem("~R~unning only", nullptr));
    boxes->setData(&only);
    insert(boxes);
  }

  insert(new TButton(TRect(16, y, 28, y + 2), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(31, y, 43, y + 2), "Cancel", cmCancel, bfNormal));

  update();
  line->select();
}

void PickDialog::handleEvent(TEvent& event)
{
  ushort value;

  TDialog::handleEvent(event);

  value = only;

  if ((event.what == evKeyDown) &&
      (current == line) &&
      ((event.keyDown.keyCode == kbUp)   ||
       (event.keyDown.keyCode == kbDown) ||
       (event.keyDown.keyCode == kbPgUp) ||
       (event.keyDown.keyCode == kbPgDn)))
  {
    // Typing stays in the filter while arrows walk the list
    box->handleEvent(event);
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmListItemSelected))
  {
    // Double click or Space on an item
    endModal(cmOK);
    clearEvent(event);
  }

  if (boxes != nullptr)
  {
    // The check box changes on a click or Space
    boxes->getData(&value);
  }

  if ((text != line->data) ||
      ((boxes != nullptr) && (value != only)))
  {
    // Filter has changed
    text = line->data;
    only = value;
    update();
  }
}

void PickDialog::shutDown()
{
  // A list box destroys the collection it is given only when it gets another one
  box->newList(nullptr);
  TDialog::shutDown();
}

const char* PickDialog::selection()
{
  return (box->range > 0) ? static_cast<const char*>(box->list()->at(box->focused)) : nullptr;
}

void PickDialog::update()
{
  TStringCollection* collection;

  collection = new TStringCollection(labels.size() + 1, 16);

  for (const std::string& label : labels)
  {
    if ((text.empty() || (strcasestr(label.c_str(), text.c_str()) != nullptr)) &&
        ((running == nullptr) || (!only) || (running->count(label) != 0)))
    {
      // Case-insensitive, anywhere in the label
      collection->insert(newStr(label.c_str()));
    }
  }

  // The list destroys the previous collection
  box->newList(collection);
}
