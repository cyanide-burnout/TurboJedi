#ifndef WATCHWINDOW_H
#define WATCHWINDOW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TWindow
#define Uses_TView
#define Uses_TPalette
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <stdint.h>

#include <string>

// The watches of the plugins in a list, the one with the newest line on top, and the lines of a watch, the newest on top.
// Neither keeps anything of its own: the watches live in the engine of the plugins, the windows show them as they are
// whenever they change. A line is read once it has been shown; the list, once it has been looked at

class WatchListView : public TView
{
  public:

    WatchListView(const TRect& bounds, TScrollBar* horizontal);

    void draw() override;
    TPalette& getPalette() const override;
    void handleEvent(TEvent& event) override;
    void setState(ushort state, Boolean enable) override;

    void choose(int number);  // Puts the cursor on a watch

  private:

    int chosen;  // The watch under the cursor, kept by its number while the order changes
    int top;     // Index of the first row shown
    int left;    // Columns scrolled away, the rows are wider than the window
    TScrollBar* bar;

    void move(int index);
};

class WatchLogView : public TView
{
  public:

    WatchLogView(const TRect& bounds, TScrollBar* horizontal, int number);

    void draw() override;
    TPalette& getPalette() const override;
    void handleEvent(TEvent& event) override;
    void setState(ushort state, Boolean enable) override;

    int number;  // Of the watch

  private:

    uint64_t chosen;  // The line under the cursor by its number
    int top;          // Index of the first row shown, a line of several rows takes them all
    int left;         // Columns scrolled away
    TScrollBar* bar;

    void move(int index);
};

class WatchWindow : public TWindow
{
  public:

    WatchWindow(const TRect& bounds);

    WatchListView* view;

    void handleEvent(TEvent& event) override;
};

class WatchLogWindow : public TWindow
{
  public:

    WatchLogWindow(const TRect& bounds, int number);

    WatchLogView* view;

    const char* getTitle(short size) override;
    void handleEvent(TEvent& event) override;

  private:

    std::string caption;
};

// Brings the list of watches to the front, or opens it, with the cursor on a watch unless it is zero
void ShowWatchList(int number);

// The same for the lines of a watch
void ShowWatchLog(int number);

// Text for the status line: Watch 2/5 with the watches that have unread lines and all of them, * when something has come
// since the list has been looked at; empty without watches
std::string DescribeWatches();

#endif
