// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TButton
#define Uses_TEvent
#define Uses_TKeys
#include "PageDialog.h"
#include "Commands.h"
#include "JediClipboard.h"

// PageDialog

PageDialog::PageDialog(const TRect& bounds, const char* title, const std::vector<std::string>& lines, bool choosing, const std::vector<bool>& choosable) :
  TWindowInit(&TDialog::initFrame),
  TDialog(bounds, title)
{
  TRect extent;
  TScrollBar* horizontal;
  TScrollBar* vertical;

  flags     |= wfGrow;
  growMode   = gfGrowAll | gfGrowRel;
  horizontal = standardScrollBar(sbHorizontal | sbHandleKeyboard);
  vertical   = standardScrollBar(sbVertical   | sbHandleKeyboard);

  // Buttons take the last rows inside the frame
  extent = getExtent();
  extent.grow(-1, -1);
  extent.b.y -= 3;
  page        = new PageView(extent, horizontal, vertical, lines, choosing);
  show        = nullptr;
  hide        = nullptr;

  page->choosable = choosable;
  insert(page);

  if (choosing)
  {
    // Enter opens a window of the field under the cursor, Hide one without its value
    show = new TButton(TRect(extent.b.x / 2 - 25, extent.b.y + 1, extent.b.x / 2 - 13, extent.b.y + 3), "~S~how", cmPageShow, bfDefault);
    hide = new TButton(TRect(extent.b.x / 2 - 12, extent.b.y + 1, extent.b.x / 2,      extent.b.y + 3), "~H~ide", cmPageHide, bfNormal);
    insert(show);
    insert(hide);
    insert(new TButton(TRect(extent.b.x / 2 + 1,  extent.b.y + 1, extent.b.x / 2 + 13, extent.b.y + 3), "~C~opy",   cmPageCopy,   bfNormal));
    insert(new TButton(TRect(extent.b.x / 2 + 14, extent.b.y + 1, extent.b.x / 2 + 26, extent.b.y + 3), "C~l~ose", cmOK,         bfNormal));
  }
  else
  {
    insert(new TButton(TRect(extent.b.x / 2 - 13, extent.b.y + 1, extent.b.x / 2 - 1, extent.b.y + 3), "~C~opy",   cmPageCopy, bfNormal));
    insert(new TButton(TRect(extent.b.x / 2 + 1,  extent.b.y + 1, extent.b.x / 2 + 13, extent.b.y + 3), "C~l~ose", cmOK,       bfDefault));
  }

  page->select();
  follow();
}

void PageDialog::handleEvent(TEvent& event)
{
  bool changed;

  if (((event.what == evCommand) && (event.message.command == cmPageCopy)) ||
      ((event.what == evKeyDown) && (event.keyDown.keyCode == kbCtrlIns)))
  {
    copy();
    clearEvent(event);
    return;
  }

  if ((event.what == evCommand) &&
      ((event.message.command == cmPageShow) || (event.message.command == cmPageHide)))
  {
    // Show, Enter or a double click, or Hide; nothing on a line that may not be chosen
    if (page->allowed())
    {
      endModal(event.message.command);
    }

    clearEvent(event);
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmPageCursor))
  {
    follow();
    clearEvent(event);
    return;
  }

  changed = (event.what == evBroadcast) && (event.message.command == cmCommandSetChanged);

  TDialog::handleEvent(event);

  if (changed)
  {
    // A button takes its state of the set of commands then, the ones of Show and Hide are told by the cursor
    follow();
  }
}

int PageDialog::chosen()
{
  return (page->cursor >= 0) ? (int)page->origins[page->cursor] : -1;
}

void PageDialog::follow()
{
  for (TButton* button : { show, hide })
  {
    if (button != nullptr)
    {
      // A command past 255 cannot be disabled in Turbo Vision, the button is turned off by its state
      button->setState(sfDisabled, !page->allowed());
      button->drawView();
    }
  }
}

void PageDialog::copy()
{
  std::string text;

  for (const std::string& line : page->lines)
  {
    // Lines as they are shown, values of several lines included
    text += line;
    text += '\n';
  }

  // A command of the user or the clipboard of Turbo Vision, either may block for a while
  CopyClipboardText(text);
}
