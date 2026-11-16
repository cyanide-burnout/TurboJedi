// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TText
#define Uses_MsgBox
#include "WatchWindow.h"
#include "JournalWatch.h"
#include "JediScript.h"
#include "Commands.h"

#include <time.h>
#include <stdio.h>
#include <ctype.h>

#include <vector>
#include <algorithm>

#define TIME_WIDTH    19  // 2026-11-15 19:36:22
#define PLUGIN_WIDTH  12
#define TITLE_WIDTH   24

// Helpers

static std::string FormatMoment(uint64_t microseconds)
{
  time_t seconds;
  struct tm local;
  char text[32];

  if (microseconds == 0)
  {
    // Nothing has come yet
    return std::string(TIME_WIDTH, ' ');
  }

  seconds = microseconds / 1000000;
  GetPluginUniversal() ? gmtime_r(&seconds, &local) : localtime_r(&seconds, &local);
  strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  return text;
}

static std::string FormatClock(uint64_t microseconds)
{
  time_t seconds;
  struct tm local;
  char text[16];

  seconds = microseconds / 1000000;
  GetPluginUniversal() ? gmtime_r(&seconds, &local) : localtime_r(&seconds, &local);
  strftime(text, sizeof(text), "%H:%M:%S", &local);
  return text;
}

// Cut to the width on the screen, between whole characters, and padded up to it
static std::string FitText(std::string text, int width)
{
  int room;

  while ((strwidth(text) > width) &&
         (!text.empty()))
  {
    do
    {
      text.pop_back();
    }
    while ((!text.empty()) &&
           ((static_cast<unsigned char>(text.back()) & 0xc0) == 0x80));

    if ((!text.empty()) &&
        (static_cast<unsigned char>(text.back()) >= 0xc0))
    {
      // The lead byte of the character cut
      text.pop_back();
    }
  }

  room = width - strwidth(text);
  return text + std::string(std::max(0, room), ' ');
}

// What a watch tells in the list besides its status: why it does not read, and the gaps it has had
static std::string DescribeWatch(const JournalWatch* watch)
{
  std::string text;

  switch (watch->state)
  {
    case WATCH_FAILED:  text = "failed: " + watch->reason;  break;
    case WATCH_STOPPED: text = "stopped: " + watch->reason; break;
    default:            text = watch->restoring() ? "waiting for entries" : watch->status; break;
  }

  if (watch->gaps > 0)
  {
    // A watch with losses does not look whole
    text += " [gaps " + std::to_string(watch->gaps) + ", last " + FormatClock(watch->gap.since) + "\xe2\x80\x93" + FormatClock(watch->gap.till) + (watch->gap.uncertain ? "?" : "") + "]";
  }

  return text;
}

// The horizontal bar of a view to the widest row: the columns scrolled away come back within it, ← and → move it
static int ScrollRows(TScrollBar* bar, int left, int widest, int width)
{
  int range;

  range = std::max(0, widest - width);
  left  = std::clamp(left, 0, range);

  if ((bar->value != left) || (bar->maxVal != range))
  {
    // Changed only when it differs, the bar tells the view about a change again
    bar->setParams(left, 0, range, std::max(1, width - 1), 1);
  }

  return left;
}

// Rows of the views go by the palette of a scroller in a blue window: normal text, then the cursor
static const char ScrollerColors[] = "\x06\x07";

// WatchListView

WatchListView::WatchListView(const TRect& bounds, TScrollBar* horizontal) :
  TView(bounds)
{
  bar       = horizontal;
  chosen    = 0;
  top       = 0;
  left      = 0;
  helpCtx   = hcWatchList;
  growMode  = gfGrowHiX | gfGrowHiY;
  options  |= ofSelectable;
  eventMask |= evBroadcast;
}

void WatchListView::setState(ushort state, Boolean enable)
{
  TView::setState(state, enable);

  if (state & (sfActive | sfFocused))
  {
    // The cursor is drawn only in the window in front
    drawView();
  }
}

int WatchListView::current() const
{
  // A watch removed meanwhile is none
  return (FindPluginWatch(chosen) != nullptr) ? chosen : 0;
}

void WatchListView::choose(int number)
{
  chosen = number;
  drawView();
}

void WatchListView::move(int index)
{
  std::vector<JournalWatch*> watches;

  watches = ListPluginWatches();

  if (watches.empty())
  {
    return;
  }

  index  = std::clamp<int>(index, 0, watches.size() - 1);
  chosen = watches[index]->number;
  drawView();
}

void WatchListView::draw()
{
  int y;
  int index;
  int current;
  bool front;
  bool fresh;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr cursor;
  TColorAttr title;
  TColorAttr color;
  int widest;
  std::string row;
  std::string heading;
  std::vector<std::string> rows;
  std::vector<JournalWatch*> watches;

  watches = ListPluginWatches();
  normal  = mapColor(1);
  cursor  = mapColor(2);
  title   = normal;
  front   = (owner != nullptr) && (owner->state & sfSelected);
  fresh   = false;
  current = -1;

  title.setForeground(TColorBIOS(0x0e));

  for (index = 0; index < (int)watches.size(); index ++)
  {
    // The list has been looked at
    fresh = fresh || watches[index]->fresh;
    watches[index]->fresh = false;

    if (watches[index]->number == chosen)
    {
      current = index;
    }
  }

  if ((current < 0) &&
      (!watches.empty()))
  {
    // The one under the cursor has gone, the cursor goes to the top
    current = 0;
    chosen  = watches[0]->number;
  }

  // The cursor stays on the screen, a header row on top
  top = std::clamp(top, std::max(0, current - (size.y - 2)), std::max(0, current));

  // Whole rows first, the window shows them from the columns scrolled away on
  heading = "  " + FitText(GetPluginUniversal() ? "Date/time UTC" : "Date/time", TIME_WIDTH) + "  " + FitText("Plugin", PLUGIN_WIDTH) + "  " + FitText("Watch", TITLE_WIDTH) + "  Status";
  widest  = strwidth(heading);

  for (JournalWatch* watch : watches)
  {
    rows.push_back(std::string((watch->unread() > 0) ? "* " : "  ") + FormatMoment(watch->latest) + "  " + FitText(watch->plugin, PLUGIN_WIDTH) + "  " + FitText(watch->title, TITLE_WIDTH) + "  " + DescribeWatch(watch));
    widest = std::max(widest, (int)strwidth(rows.back()));
  }

  left = ScrollRows(bar, left, widest, size.x);

  buffer.moveChar(0, ' ', title, size.x);
  buffer.moveStr(0, heading, title, size.x, left);
  writeLine(0, 0, size.x, 1, buffer);

  for (y = 1; y < size.y; y ++)
  {
    index = top + y - 1;
    buffer.moveChar(0, ' ', normal, size.x);

    if (index < (int)watches.size())
    {
      const JournalWatch* watch = watches[index];

      color = normal;

      if (watch->unread() > 0)    color.setForeground(TColorBIOS(0x0f));
      if (watch->state == WATCH_FAILED)  color.setForeground(TColorBIOS(0x0c));
      if (watch->state == WATCH_STOPPED) color.setForeground(TColorBIOS(0x08));

      if (watch->raised)
      {
        // Raised by the plugin: red and blinking until its lines are read, red alone where the terminal cannot blink
        color = TColorAttr(TColorBIOS(0x0f), TColorBIOS(0x04), slBlink);
      }

      if (front && (index == current))
      {
        // A raised one stays red under the cursor
        color = cursor;

        if (watch->raised)
        {
          color.setForeground(TColorBIOS(0x04));
          color.setStyle(slBlink);
        }
      }

      buffer.moveChar(0, ' ', color, size.x);
      buffer.moveStr(0, rows[index], color, size.x, left);
    }

    writeLine(0, y, size.x, 1, buffer);
  }

  if (fresh)
  {
    // The star of the status line goes out
    MarkPluginWatchesChanged();
  }
}

TPalette& WatchListView::getPalette() const
{
  static TPalette palette(ScrollerColors, sizeof(ScrollerColors) - 1);

  return palette;
}

void WatchListView::handleEvent(TEvent& event)
{
  ushort key;
  int index;
  int count;
  std::vector<JournalWatch*> watches;
  JournalWatch* watch;

  TView::handleEvent(event);

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalWatchesChanged))
  {
    // Never cleared, every window of watches takes it
    drawView();
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmScrollBarChanged) &&
      (event.message.infoPtr == bar))
  {
    left = bar->value;
    drawView();
    return;
  }

  watches = ListPluginWatches();
  count   = watches.size();
  index   = std::find_if(watches.begin(), watches.end(), [this] (JournalWatch* item) { return item->number == chosen; }) - watches.begin();

  if (event.what == evMouseDown)
  {
    // A click chooses the row, a double click opens its lines
    TPoint where = makeLocal(event.mouse.where);

    if ((where.y > 0) &&
        (top + where.y - 1 < count))
    {
      move(top + where.y - 1);

      if (event.mouse.eventFlags & meDoubleClick)
      {
        ShowWatchLog(chosen);
      }
    }

    clearEvent(event);
    return;
  }

  if (event.what == evMouseWheel)
  {
    move(index + ((event.mouse.wheel == mwDown) ? 3 : (event.mouse.wheel == mwUp) ? -3 : 0));
    clearEvent(event);
    return;
  }

  if (((event.what == evCommand) && (event.message.command == cmWatchUniversal)) ||
      ((event.what == evKeyDown) && (toupper(event.keyDown.charScan.charCode) == 'U')))
  {
    // One zone for every window of watches
    SetPluginUniversal(!GetPluginUniversal());
    clearEvent(event);
    return;
  }

  // The items of the status line of these windows are commands, they do what their keys do
  if ((event.what == evCommand) &&
      ((event.message.command == cmWatchOpen) || (event.message.command == cmWatchRemove)))
  {
    key = (event.message.command == cmWatchOpen) ? kbEnter : kbDel;
  }
  else if (event.what == evKeyDown)
  {
    key = event.keyDown.keyCode;
  }
  else
  {
    return;
  }

  switch (key)
  {
    case kbUp:    move(index - 1);             break;
    case kbDown:  move(index + 1);             break;
    case kbPgUp:  move(index - (size.y - 1));  break;
    case kbPgDn:  move(index + (size.y - 1));  break;
    case kbHome:  move(0);                     break;
    case kbEnd:   move(count - 1);             break;

    case kbEnter:
      if (index < count)
      {
        ShowWatchLog(chosen);
      }
      break;

    case kbDel:
    case kbBack:
    case kbCtrlH:
      if ((index < count) &&
          ((watch = watches[index]) != nullptr) &&
          (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Remove the watch %s of %s? Its lines go along.", watch->title.c_str(), watch->plugin.c_str()) == cmYes))
      {
        RemovePluginWatch(chosen);
      }
      break;

    default:
      return;
  }

  clearEvent(event);
}

// WatchLogView

uint64_t WatchLogView::current() const
{
  return chosen;
}

WatchLogView::WatchLogView(const TRect& bounds, TScrollBar* horizontal, int number) :
  TView(bounds)
{
  this->number = number;
  bar          = horizontal;
  chosen       = 0;
  top          = 0;
  left         = 0;
  helpCtx      = hcWatchLines;
  growMode     = gfGrowHiX | gfGrowHiY;
  options     |= ofSelectable;
  eventMask   |= evBroadcast;
}

void WatchLogView::setState(ushort state, Boolean enable)
{
  TView::setState(state, enable);

  if (state & (sfActive | sfFocused))
  {
    drawView();
  }
}

TPalette& WatchLogView::getPalette() const
{
  static TPalette palette(ScrollerColors, sizeof(ScrollerColors) - 1);

  return palette;
}

// Lines of a watch, the newest first, a text of several lines a row each
struct WatchRow
{
  size_t line;  // Index in the lines of the watch
  size_t start; // Byte of the text the row begins at
  size_t end;
};

static std::vector<WatchRow> ArrangeRows(const JournalWatch* watch)
{
  size_t start;
  size_t end;
  std::vector<WatchRow> rows;

  for (size_t index = watch->lines.size(); index > 0; index --)
  {
    const std::string& text = watch->lines[index - 1].text;

    for (start = 0; ; start = end + 1)
    {
      end = std::min(text.find('\n', start), text.size());
      rows.push_back({ index - 1, start, end });

      if (end >= text.size())
      {
        break;
      }
    }
  }

  return rows;
}

void WatchLogView::move(int index)
{
  JournalWatch* watch;
  std::vector<WatchRow> rows;

  if (((watch = FindPluginWatch(number)) == nullptr) ||
      ((rows = ArrangeRows(watch)).empty()))
  {
    return;
  }

  // By rows, a line of several rows is chosen by any of them
  index  = std::clamp<int>(index, 0, rows.size() - 1);
  chosen = watch->lines[rows[index].line].number;
  drawView();
}

void WatchLogView::draw()
{
  int y;
  int first;
  int last;
  int index;
  int widest;
  bool front;
  bool read;
  TDrawBuffer buffer;
  TColorAttr normal;
  TColorAttr cursor;
  TColorAttr color;
  std::string row;
  JournalWatch* watch;
  std::vector<WatchRow> rows;

  normal = mapColor(1);
  cursor = mapColor(2);
  front  = (owner != nullptr) && (owner->state & sfSelected);
  read   = false;
  first  = -1;
  last   = -1;

  if ((watch = FindPluginWatch(number)) != nullptr)
  {
    rows = ArrangeRows(watch);
  }

  for (index = 0; index < (int)rows.size(); index ++)
  {
    // The rows of the line under the cursor
    if (watch->lines[rows[index].line].number == chosen)
    {
      first = (first < 0) ? index : first;
      last  = index;
    }
  }

  if ((first < 0) &&
      (!rows.empty()))
  {
    // Not chosen yet or gone: the newest
    first  = 0;
    last   = 0;
    chosen = watch->lines[rows[0].line].number;

    while ((last + 1 < (int)rows.size()) && (rows[last + 1].line == rows[0].line))
    {
      last ++;
    }
  }

  top = std::clamp(top, std::max(0, last - (size.y - 1)), std::max(0, first));

  // The widest row of the watch, the window shows them from the columns scrolled away on
  widest = 0;

  for (index = 0; index < (int)rows.size(); index ++)
  {
    widest = std::max<int>(widest, TIME_WIDTH + 4 + strwidth(watch->lines[rows[index].line].text.substr(rows[index].start, rows[index].end - rows[index].start)));
  }

  left = ScrollRows(bar, left, widest, size.x);

  for (y = 0; y < size.y; y ++)
  {
    index = top + y;
    buffer.moveChar(0, ' ', normal, size.x);

    if (index < (int)rows.size())
    {
      JournalWatchLine& line = watch->lines[rows[index].line];

      color = normal;

      if (line.unread)
      {
        color.setForeground(TColorBIOS(0x0f));
      }

      if (front && (index >= first) && (index <= last))
      {
        color = cursor;
      }

      // The first row of a line has its mark and time, the next ones go under its text
      row = (rows[index].start == 0) ? (std::string(line.unread ? "* " : "  ") + FormatMoment(line.moment) + "  ") : std::string(TIME_WIDTH + 4, ' ');
      row += line.text.substr(rows[index].start, rows[index].end - rows[index].start);

      buffer.moveChar(0, ' ', color, size.x);
      buffer.moveStr(0, row, color, size.x, left);

      if (line.unread)
      {
        // Shown, so read
        line.unread = false;
        read        = true;
      }
    }

    writeLine(0, y, size.x, 1, buffer);
  }

  if (read)
  {
    if (watch->unread() == 0)
    {
      // Nothing left to tell about
      watch->raised = false;
    }

    MarkPluginWatchesChanged();
  }
}

void WatchLogView::handleEvent(TEvent& event)
{
  ushort key;
  int index;
  int count;
  JournalWatch* watch;
  std::vector<WatchRow> rows;
  std::deque<JournalWatchLine>::iterator line;

  TView::handleEvent(event);

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalWatchesChanged))
  {
    drawView();
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmScrollBarChanged) &&
      (event.message.infoPtr == bar))
  {
    left = bar->value;
    drawView();
    return;
  }

  if ((watch = FindPluginWatch(number)) == nullptr)
  {
    return;
  }

  if ((event.what == evCommand) &&
      (event.message.command == cmJournalClear))
  {
    // F8 of the menu, as a journal window clears
    watch->clear();
    MarkPluginWatchesChanged();
    drawView();
    clearEvent(event);
    return;
  }

  rows  = ArrangeRows(watch);
  count = rows.size();
  index = std::find_if(rows.begin(), rows.end(), [this, watch] (const WatchRow& row) { return watch->lines[row.line].number == chosen; }) - rows.begin();
  line  = std::find_if(watch->lines.begin(), watch->lines.end(), [this] (const JournalWatchLine& item) { return item.number == chosen; });

  if (event.what == evMouseDown)
  {
    TPoint where = makeLocal(event.mouse.where);

    if (top + where.y < count)
    {
      move(top + where.y);
      line = std::find_if(watch->lines.begin(), watch->lines.end(), [this] (const JournalWatchLine& item) { return item.number == chosen; });

      if ((event.mouse.eventFlags & meDoubleClick) &&
          (line != watch->lines.end()) &&
          (!line->entry.cursor.empty()))
      {
        ShowPluginWatchEntry(watch, line->entry);
      }
    }

    clearEvent(event);
    return;
  }

  if (event.what == evMouseWheel)
  {
    top = std::clamp(top + ((event.mouse.wheel == mwDown) ? 3 : (event.mouse.wheel == mwUp) ? -3 : 0), 0, std::max(0, count - size.y));
    drawView();
    clearEvent(event);
    return;
  }

  if (((event.what == evCommand) && (event.message.command == cmWatchUniversal)) ||
      ((event.what == evKeyDown) && (toupper(event.keyDown.charScan.charCode) == 'U')))
  {
    // One zone for every window of watches
    SetPluginUniversal(!GetPluginUniversal());
    clearEvent(event);
    return;
  }

  // The items of the status line of these windows are commands, they do what their keys do
  if ((event.what == evCommand) &&
      ((event.message.command == cmWatchOpen) || (event.message.command == cmWatchRemove)))
  {
    key = (event.message.command == cmWatchOpen) ? kbEnter : kbDel;
  }
  else if (event.what == evKeyDown)
  {
    key = event.keyDown.keyCode;
  }
  else
  {
    return;
  }

  switch (key)
  {
    case kbUp:
      // To the first row of the line before
      while ((index > 0) && (index < count) && (watch->lines[rows[index - 1].line].number == chosen)) index --;
      move(index - 1);
      break;

    case kbDown:
      while ((index + 1 < count) && (watch->lines[rows[index + 1].line].number == chosen)) index ++;
      move(index + 1);
      break;

    case kbPgUp:  move(index - size.y);  break;
    case kbPgDn:  move(index + size.y);  break;
    case kbHome:  move(0);               break;
    case kbEnd:   move(count - 1);       break;

    case kbEnter:
      if ((line != watch->lines.end()) &&
          (!line->entry.cursor.empty()))
      {
        // To the entry of the line in a window
        ShowPluginWatchEntry(watch, line->entry);
      }
      break;

    case kbDel:
    case kbBack:
    case kbCtrlH:
      if (line != watch->lines.end())
      {
        // The cursor goes on to the next one
        watch->erase(chosen);
        move(index);
        MarkPluginWatchesChanged();
      }
      break;

    default:
      return;
  }

  clearEvent(event);
}

// WatchWindow

WatchWindow::WatchWindow(const TRect& bounds) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, "Watches", wnNoNumber)
{
  TRect extent;

  // Flat as a journal window
  growMode = gfGrowAll | gfGrowRel;
  state   &= ~sfShadow;
  extent   = getExtent();
  extent.grow(-1, -1);
  view     = new WatchListView(extent, standardScrollBar(sbHorizontal | sbHandleKeyboard));

  insert(view);
  view->select();
}

void WatchWindow::handleEvent(TEvent& event)
{
  if ((event.what == evKeyDown) &&
      (event.keyDown.keyCode == kbEsc))
  {
    // As the window of an output does
    clearEvent(event);
    close();
    return;
  }

  TWindow::handleEvent(event);
}

// WatchLogWindow

WatchLogWindow::WatchLogWindow(const TRect& bounds, int number) :
  TWindowInit(&TWindow::initFrame),
  TWindow(bounds, nullptr, wnNoNumber)
{
  TRect extent;
  JournalWatch* watch;

  if ((watch = FindPluginWatch(number)) != nullptr)
  {
    caption = watch->plugin + ": " + watch->title;
  }

  growMode = gfGrowAll | gfGrowRel;
  state   &= ~sfShadow;
  extent   = getExtent();
  extent.grow(-1, -1);
  view     = new WatchLogView(extent, standardScrollBar(sbHorizontal | sbHandleKeyboard), number);

  insert(view);
  view->select();
}

const char* WatchLogWindow::getTitle(short size)
{
  static std::string title;

  title = ((short)caption.size() > size) && (size > 3) ? (caption.substr(0, size - 3) + "...") : caption;
  return title.c_str();
}

void WatchLogWindow::handleEvent(TEvent& event)
{
  if ((event.what == evKeyDown) &&
      (event.keyDown.keyCode == kbEsc))
  {
    clearEvent(event);
    close();
    return;
  }

  if ((event.what == evBroadcast) &&
      (event.message.command == cmJournalWatchesChanged) &&
      (FindPluginWatch(view->number) == nullptr))
  {
    // The watch has been removed, its lines with it
    close();
    return;
  }

  TWindow::handleEvent(event);
}

// Interface

static Boolean IsWatchWindow(TView* view, void*)
{
  return dynamic_cast<WatchWindow*>(view) != nullptr;
}

static Boolean IsWatchLogWindow(TView* view, void* number)
{
  WatchLogWindow* window;

  window = dynamic_cast<WatchLogWindow*>(view);
  return (window != nullptr) && (window->view->number == *static_cast<int*>(number));
}

static TRect GetWatchBounds()
{
  TRect bounds;
  int width;
  int height;

  // In the middle of the desktop, not over all of it: the journals stay in sight
  bounds = TProgram::deskTop->getExtent();
  width  = std::min(110, bounds.b.x - bounds.a.x);
  height = std::min(18, bounds.b.y - bounds.a.y);
  bounds.a.x += (bounds.b.x - bounds.a.x - width) / 2;
  bounds.a.y += (bounds.b.y - bounds.a.y - height) / 2;
  bounds.b.x  = bounds.a.x + width;
  bounds.b.y  = bounds.a.y + height;
  return bounds;
}

void ShowWatchList(int number)
{
  WatchWindow* window;

  if ((window = static_cast<WatchWindow*>(TProgram::deskTop->firstThat(IsWatchWindow, nullptr))) == nullptr)
  {
    window = new WatchWindow(GetWatchBounds());
    TProgram::deskTop->insert(window);
  }

  window->select();

  if (number != 0)
  {
    window->view->choose(number);
  }
}

void ShowWatchLog(int number)
{
  TRect bounds;
  WatchLogWindow* window;

  if ((window = static_cast<WatchLogWindow*>(TProgram::deskTop->firstThat(IsWatchLogWindow, &number))) == nullptr)
  {
    // A little lower than the list, so that both are seen
    bounds = GetWatchBounds();
    bounds.move(2, 1);
    window = new WatchLogWindow(bounds, number);
    TProgram::deskTop->insert(window);
  }

  window->select();
}

void PlaceWatchList(const TRect& bounds)
{
  TProgram::deskTop->insert(new WatchWindow(bounds));
}

void PlaceWatchLog(int number, const TRect& bounds)
{
  TProgram::deskTop->insert(new WatchLogWindow(bounds, number));
}

std::string DescribeWatches()
{
  size_t unread;
  bool fresh;
  std::vector<JournalWatch*> watches;

  watches = ListPluginWatches();
  unread  = 0;
  fresh   = false;

  if (watches.empty())
  {
    return std::string();
  }

  for (JournalWatch* watch : watches)
  {
    unread += (watch->unread() > 0);
    fresh   = fresh || watch->fresh;
  }

  return "Watch " + std::to_string(unread) + "/" + std::to_string(watches.size()) + (fresh ? " *" : "");
}
