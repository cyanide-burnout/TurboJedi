// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TCollection
#define Uses_TEvent
#define Uses_TKeys
#include "WindowsDialog.h"

#include <string.h>

#define FILTER_LENGTH  128
#define TITLE_LENGTH   256

// Keeps the order it is given in, the labels belong to the dialog

class LabelCollection : public TCollection
{
  public:

    LabelCollection(ccIndex limit) :
      TCollection(limit, 16)
    {
      shouldDelete = False;
    }

  private:

    void* readItem(ipstream&) override
    {
      return nullptr;
    }

    void writeItem(void*, opstream&) override
    {
    }
};

// WindowsDialog

WindowsDialog::WindowsDialog(const std::vector<TWindow*>& windows) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 72, 23), "Windows")
{
  std::string number;
  TScrollBar* bar;

  this->windows = windows;

  for (TWindow* window : windows)
  {
    // Number to go there by Alt and the digit, then the title with the filter
    number = (window->number != wnNoNumber) ? std::to_string(window->number) : "";
    labels.push_back(number + std::string(3 - number.size(), ' ') + window->getTitle(TITLE_LENGTH));
  }

  options |= ofCentered;
  line     = new TInputLine(TRect(11, 2, 69, 3), FILTER_LENGTH);
  bar      = new TScrollBar(TRect(69, 4, 70, 19));
  box      = new TListBox(TRect(2, 4, 69, 19), 1, bar);

  insert(line);
  insert(new TLabel(TRect(2, 2, 11, 3), "~F~ilter", line));
  insert(bar);
  insert(box);
  insert(new TButton(TRect(22, 20, 34, 22), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(37, 20, 49, 22), "Cancel", cmCancel, bfNormal));

  update();

  if (box->range > 1)
  {
    // Enter goes back to the window used before, as switching of windows does elsewhere
    box->focusItem(1);
  }

  line->select();
}

void WindowsDialog::handleEvent(TEvent& event)
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

void WindowsDialog::shutDown()
{
  // A list box destroys the collection it is given only when it gets another one
  box->newList(nullptr);
  TDialog::shutDown();
}

TWindow* WindowsDialog::selection()
{
  return (box->range > 0) ? shown[box->focused] : nullptr;
}

void WindowsDialog::update()
{
  LabelCollection* collection;

  collection = new LabelCollection(labels.size() + 1);
  shown.clear();

  for (size_t index = 0; index < labels.size(); index ++)
  {
    if (text.empty() ||
        (strcasestr(labels[index].c_str(), text.c_str()) != nullptr))
    {
      // Case-insensitive, anywhere in the number or the title
      collection->insert(const_cast<char*>(labels[index].c_str()));
      shown.push_back(windows[index]);
    }
  }

  // The list destroys the previous collection
  box->newList(collection);
}
