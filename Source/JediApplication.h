#ifndef JEDIAPPLICATION_H
#define JEDIAPPLICATION_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TApplication
#define Uses_TMenuBar
#define Uses_TStatusLine
#define Uses_TFileDialog
#include <tvision/tv.h>

#include <stdint.h>

#include <string>

#include "JediClock.h"
#ifdef USE_LIBCURL
#include "JediUpdate.h"
#endif
#include "JournalTools.h"
#include "JournalWindow.h"

class JediApplication : public TApplication
{
  public:

    JediApplication(const JournalFilter& filter, int history, const std::string& layout);

    void getEvent(TEvent& event) override;
    void handleEvent(TEvent& event) override;
    void idle() override;

    bool save(const std::string& path);

  private:

    JournalFilter filter;
    int history;

    JediClock* clock;
    TView* focused;
    short number;
    MouseEventType held;  // The mouse with a button held, buttons are zero when none is
    uint64_t repeat;      // Milliseconds of Turbo Vision when the held button repeats
    std::string place;    // Path of the layout opened or saved the last time, empty before
#ifdef USE_LIBCURL
    JediUpdate update;    // Latest release on GitHub, asked from the Help menu
#endif

    void open(const JournalFilter& filter);
    void clone();
    void chooseWindow();
    bool load(const std::string& path, std::string& error);
    void recall(TFileDialog* dialog, bool named);
    void chooseOpen();
    void chooseSave();
    void showHelp();
    void showAbout();
#ifdef USE_LIBCURL
    void checkUpdate();
    void showUpdate();
#endif
    void timeline(JournalWindow* current);

    static TStatusLine* initStatusLine(TRect bounds);
    static TMenuBar* initMenuBar(TRect bounds);
};

#endif
