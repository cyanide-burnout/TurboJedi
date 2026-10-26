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

PageDialog::PageDialog(const TRect& bounds, const char* title, const std::vector<std::string>& lines, bool choosing) :
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

  insert(page);

  if (choosing)
  {
    // Enter opens a window of the field under the cursor
    insert(new TButton(TRect(extent.b.x / 2 - 19, extent.b.y + 1, extent.b.x / 2 - 7, extent.b.y + 3), "~F~ilter", cmPageFilter, bfDefault));
    insert(new TButton(TRect(extent.b.x / 2 - 6,  extent.b.y + 1, extent.b.x / 2 + 6, extent.b.y + 3), "~C~opy",   cmPageCopy,   bfNormal));
    insert(new TButton(TRect(extent.b.x / 2 + 7,  extent.b.y + 1, extent.b.x / 2 + 19, extent.b.y + 3), "C~l~ose", cmOK,         bfNormal));
  }
  else
  {
    insert(new TButton(TRect(extent.b.x / 2 - 13, extent.b.y + 1, extent.b.x / 2 - 1, extent.b.y + 3), "~C~opy",   cmPageCopy, bfNormal));
    insert(new TButton(TRect(extent.b.x / 2 + 1,  extent.b.y + 1, extent.b.x / 2 + 13, extent.b.y + 3), "C~l~ose", cmOK,       bfDefault));
  }

  page->select();
}

void PageDialog::handleEvent(TEvent& event)
{
  if (((event.what == evCommand) && (event.message.command == cmPageCopy)) ||
      ((event.what == evKeyDown) && (event.keyDown.keyCode == kbCtrlIns)))
  {
    copy();
    clearEvent(event);
    return;
  }

  if ((event.what == evCommand) &&
      (event.message.command == cmPageFilter))
  {
    // Filter button, Enter or a double click
    endModal(cmPageFilter);
    clearEvent(event);
    return;
  }

  TDialog::handleEvent(event);
}

int PageDialog::chosen()
{
  return (page->cursor >= 0) ? (int)page->origins[page->cursor] : -1;
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
