#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStringCollection
#define Uses_TEvent
#define Uses_TKeys
#include "PickDialog.h"

#include <string.h>

#define FILTER_LENGTH  128

// PickDialog

PickDialog::PickDialog(const char* title, const std::map<std::string, std::string>& values) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 60, 23), title)
{
  TScrollBar* bar;

  for (const std::pair<const std::string, std::string>& item : values)
  {
    // Labels are what the list shows and what the filter looks for
    labels.push_back(item.first);
  }

  options |= ofCentered;
  line     = new TInputLine(TRect(11, 2, 57, 3), FILTER_LENGTH);
  bar      = new TScrollBar(TRect(57, 4, 58, 19));
  box      = new TListBox(TRect(2, 4, 57, 19), 1, bar);

  insert(line);
  insert(new TLabel(TRect(2, 2, 11, 3), "~F~ilter", line));
  insert(bar);
  insert(box);
  insert(new TButton(TRect(16, 20, 28, 22), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(31, 20, 43, 22), "Cancel", cmCancel, bfNormal));

  update();
  line->select();
}

void PickDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

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

  if (text != line->data)
  {
    // Filter has changed
    text = line->data;
    update();
  }
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
    if (text.empty() ||
        (strcasestr(label.c_str(), text.c_str()) != nullptr))
    {
      // Case-insensitive, anywhere in the label
      collection->insert(newStr(label.c_str()));
    }
  }

  // The list destroys the previous collection
  box->newList(collection);
}
