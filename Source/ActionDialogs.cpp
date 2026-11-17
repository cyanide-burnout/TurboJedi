// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TScrollBar
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "ActionDialogs.h"
#include "PageDialog.h"
#include "PageView.h"
#include "Commands.h"
#include "JediClipboard.h"

#include <string.h>

#include <algorithm>

#define INPUT_LENGTH  4096  // Bytes of a line to edit

// Helpers

static std::vector<std::string> Split(const std::string& text)
{
  size_t start;
  size_t end;
  std::vector<std::string> lines;

  for (start = 0; start <= text.size(); start = end + 1)
  {
    end = std::min(text.find('\n', start), text.size());
    lines.push_back(text.substr(start, end - start));
  }

  return lines;
}

// Rows the text takes in a static text of the width, its words wrap there
static int CountRows(const std::vector<std::string>& lines, int width)
{
  int rows;

  rows = 0;

  for (const std::string& line : lines)
  {
    // Words may leave a row a little short, one more is spare
    rows += std::max(1, (strwidth(line) + width - 1) / width) + ((strwidth(line) > width) ? 1 : 0);
  }

  return rows;
}

// The words of the lines in rows of the width at most, a word wider than it on a row of its own; the spaces a line
// starts with stay
static std::vector<std::string> Wrap(const std::vector<std::string>& lines, int width)
{
  size_t start;
  size_t end;
  bool fresh;
  std::string row;
  std::string word;
  std::string next;
  std::vector<std::string> rows;

  for (const std::string& line : lines)
  {
    row   = std::string();
    fresh = true;

    for (start = 0; start < line.size(); start = end + 1)
    {
      end  = std::min(line.find(' ', start), line.size());
      word = line.substr(start, end - start);
      next = fresh ? word : row + " " + word;

      if ((!fresh) &&
          (!row.empty()) &&
          (!word.empty()) &&
          (strwidth(next) > width))
      {
        rows.push_back(row);
        next = word;
      }

      row   = next;
      fresh = false;
    }

    rows.push_back(row);
  }

  return rows;
}

static TRect Measure(const std::vector<std::string>& lines, int least, int rows)
{
  int width;
  int height;
  TRect desktop;

  desktop = TProgram::deskTop->getExtent();
  width   = least;

  for (const std::string& line : lines)
  {
    width = std::max(width, strwidth(line) + 4);
  }

  width  = std::min(width, desktop.b.x - 4);
  height = std::min(CountRows(lines, width - 4) + rows, desktop.b.y - 2);

  return TRect(0, 0, width, height);
}

// Text with buttons under it, the result is the command of the button
static ushort Ask(const char* title, const std::string& text, bool question)
{
  ushort result;
  TRect bounds;
  TDialog* dialog;
  TButton* button;
  std::vector<std::string> lines;

  lines  = Split(text);
  bounds = Measure(lines, 40, 7);
  dialog = new TDialog(bounds, title);

  dialog->options |= ofCentered;
  dialog->insert(new TStaticText(TRect(2, 2, bounds.b.x - 2, bounds.b.y - 4), text));

  if (question)
  {
    dialog->insert(new TButton(TRect(bounds.b.x / 2 - 12, bounds.b.y - 3, bounds.b.x / 2 - 2, bounds.b.y - 1), "~Y~es", cmYes, bfNormal));
    dialog->insert(button = new TButton(TRect(bounds.b.x / 2 + 2, bounds.b.y - 3, bounds.b.x / 2 + 12, bounds.b.y - 1), "~N~o", cmNo, bfDefault));
  }
  else
  {
    dialog->insert(button = new TButton(TRect(bounds.b.x / 2 - 5, bounds.b.y - 3, bounds.b.x / 2 + 5, bounds.b.y - 1), "O~K~", cmOK, bfDefault));
  }

  button->select();
  result = TProgram::deskTop->execView(dialog);
  TObject::destroy(dialog);

  return result;
}

// ChoiceList

class ChoiceList : public TListViewer
{
  public:

    ChoiceList(const TRect& bounds, TScrollBar* vertical, const std::vector<std::string>& items);

    void getText(char* destination, short item, short length) override;

  private:

    const std::vector<std::string>& items;
};

ChoiceList::ChoiceList(const TRect& bounds, TScrollBar* vertical, const std::vector<std::string>& items) :
  TListViewer(bounds, 1, nullptr, vertical),
  items(items)
{
  setRange(items.size());
}

void ChoiceList::getText(char* destination, short item, short length)
{
  if ((item >= 0) &&
      ((size_t)item < items.size()))
  {
    strnzcpy(destination, items[item].c_str(), length);
    return;
  }

  *destination = '\0';
}

// ChoiceDialog

class ChoiceDialog : public TDialog
{
  public:

    ChoiceDialog(const TRect& bounds, const char* title);

    void handleEvent(TEvent& event) override;
};

ChoiceDialog::ChoiceDialog(const TRect& bounds, const char* title) :
  TWindowInit(&TDialog::initFrame),
  TDialog(bounds, title)
{
}

void ChoiceDialog::handleEvent(TEvent& event)
{
  TDialog::handleEvent(event);

  if ((event.what == evBroadcast) &&
      (event.message.command == cmListItemSelected))
  {
    // Enter or a double click on an item
    endModal(cmOK);
    clearEvent(event);
  }
}

// AnswerDialog

// The text, a line for a question and Ask, Copy, Close: the keys that scroll the text do it while the line has the focus,
// Copy puts the whole text to the clipboard as it has come, not as it is wrapped
class AnswerDialog : public TDialog
{
  public:

    AnswerDialog(const TRect& bounds, const char* title, const std::string& text);

    void handleEvent(TEvent& event) override;

    TScrollBar* bar;

  private:

    std::string text;
};

AnswerDialog::AnswerDialog(const TRect& bounds, const char* title, const std::string& text) :
  TWindowInit(&TDialog::initFrame),
  TDialog(bounds, title),
  bar(nullptr),
  text(text)
{
}

void AnswerDialog::handleEvent(TEvent& event)
{
  int step;

  if ((event.what == evCommand) &&
      (event.message.command == cmPageCopy))
  {
    // A command of the user or the clipboard of Turbo Vision, either may block for a while
    CopyClipboardText(text);
    clearEvent(event);
    return;
  }

  if ((event.what == evKeyDown) &&
      (bar != nullptr))
  {
    switch (event.keyDown.keyCode)
    {
      case kbUp:    step = -1;               break;
      case kbDown:  step = 1;                break;
      case kbPgUp:  step = -bar->pgStep;     break;
      case kbPgDn:  step = bar->pgStep;      break;
      default:      step = 0;                break;
    }

    if (step != 0)
    {
      bar->setValue(bar->value + step);
      clearEvent(event);
      return;
    }
  }

  TDialog::handleEvent(event);
}

// Interface

void ShowActionText(const char* title, const std::string& text)
{
  TRect bounds;
  PageDialog* dialog;
  std::vector<std::string> lines;

  lines = Split(text);

  if (CountRows(lines, TProgram::deskTop->getExtent().b.x - 8) + 7 <= TProgram::deskTop->getExtent().b.y - 2)
  {
    Ask(title, text, false);
    return;
  }

  // Too long for a box, it scrolls
  bounds = TProgram::deskTop->getExtent();
  bounds.grow(-2, -1);
  dialog = new PageDialog(bounds, title, lines);
  TProgram::deskTop->execView(dialog);
  TObject::destroy(dialog);
}

bool AskActionText(const char* title, const std::string& text, const std::string& prompt, std::string& value)
{
  int width;
  int height;
  ushort result;
  TRect desktop;
  AnswerDialog* dialog;
  TInputLine* line;
  TScrollBar* bar;
  std::vector<std::string> rows;
  char data[INPUT_LENGTH + 1];

  // As wide as the desktop lets a text be read, as high as its rows and the line under them need
  desktop = TProgram::deskTop->getExtent();
  width   = std::max(std::min(100, desktop.b.x - 4), std::min(40, desktop.b.x));
  rows    = Wrap(Split(text), width - 5);
  height  = std::max(std::min((int)rows.size() + 8, desktop.b.y - 2), std::min(11, desktop.b.y));
  dialog  = new AnswerDialog(TRect(0, 0, width, height), title, text);
  bar     = new TScrollBar(TRect(width - 2, 1, width - 1, height - 7));
  line    = new TInputLine(TRect(3, height - 6, width - 3, height - 5), INPUT_LENGTH);

  dialog->options |= ofCentered;
  dialog->insert(bar);
  dialog->insert(new PageView(TRect(1, 1, width - 2, height - 7), nullptr, bar, rows, false));
  dialog->insert(line);
  dialog->insert(new TLabel(TRect(2, height - 7, width - 2, height - 6), prompt.c_str(), line));
  dialog->insert(new TButton(TRect(width / 2 - 17, height - 4, width / 2 - 7, height - 2), "~A~sk", cmOK, bfDefault));
  dialog->insert(new TButton(TRect(width / 2 - 5, height - 4, width / 2 + 5, height - 2), "~C~opy", cmPageCopy, bfNormal));
  dialog->insert(new TButton(TRect(width / 2 + 7, height - 4, width / 2 + 17, height - 2), "C~l~ose", cmCancel, bfNormal));
  dialog->bar = bar;

  strnzcpy(data, value.c_str(), sizeof(data));
  line->setData(data);
  line->select();

  result = TProgram::deskTop->execView(dialog);

  if (result == cmOK)
  {
    line->getData(data);
    value = data;
  }

  TObject::destroy(dialog);
  return result == cmOK;
}

bool AskActionConfirm(const std::string& text)
{
  return Ask("Confirm", text, true) == cmYes;
}

bool AskActionInput(const std::string& prompt, std::string& value)
{
  int width;
  ushort result;
  TRect bounds;
  TDialog* dialog;
  TInputLine* line;
  char data[INPUT_LENGTH + 1];

  bounds = TProgram::deskTop->getExtent();
  width  = std::min(std::max(60, std::max(strwidth(prompt), strwidth(value)) + 8), bounds.b.x - 4);
  dialog = new TDialog(TRect(0, 0, width, 9), "Input");
  line   = new TInputLine(TRect(3, 3, width - 3, 4), INPUT_LENGTH);

  dialog->options |= ofCentered;
  dialog->insert(line);
  dialog->insert(new TLabel(TRect(2, 2, width - 2, 3), prompt.c_str(), line));
  dialog->insert(new TButton(TRect(width / 2 - 12, 6, width / 2 - 2, 8), "O~K~", cmOK, bfDefault));
  dialog->insert(new TButton(TRect(width / 2 + 2, 6, width / 2 + 12, 8), "Cancel", cmCancel, bfNormal));

  strnzcpy(data, value.c_str(), sizeof(data));
  line->setData(data);
  line->select();

  result = TProgram::deskTop->execView(dialog);

  if (result == cmOK)
  {
    line->getData(data);
    value = data;
  }

  TObject::destroy(dialog);
  return result == cmOK;
}

int AskActionChoice(const std::string& title, const std::vector<std::string>& items, int initial)
{
  int width;
  int height;
  int chosen;
  ushort result;
  TRect desktop;
  TScrollBar* bar;
  ChoiceList* list;
  ChoiceDialog* dialog;

  desktop = TProgram::deskTop->getExtent();
  width   = std::max(40, strwidth(title) + 8);

  for (const std::string& item : items)
  {
    width = std::max(width, strwidth(item) + 7);
  }

  width  = std::min(width, desktop.b.x - 4);
  height = std::min<int>(items.size() + 7, desktop.b.y - 2);
  dialog = new ChoiceDialog(TRect(0, 0, width, height), title.c_str());
  bar    = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
  list   = new ChoiceList(TRect(2, 2, width - 3, height - 4), bar, items);

  dialog->options |= ofCentered;
  dialog->insert(bar);
  dialog->insert(list);
  dialog->insert(new TButton(TRect(width / 2 - 12, height - 3, width / 2 - 2, height - 1), "O~K~", cmOK, bfDefault));
  dialog->insert(new TButton(TRect(width / 2 + 2, height - 3, width / 2 + 12, height - 1), "Cancel", cmCancel, bfNormal));

  if ((initial >= 0) &&
      ((size_t)initial < items.size()))
  {
    list->focusItem(initial);
  }

  list->select();

  result = TProgram::deskTop->execView(dialog);
  chosen = list->focused;

  TObject::destroy(dialog);
  return ((result == cmOK) && (!items.empty())) ? chosen : -1;
}
