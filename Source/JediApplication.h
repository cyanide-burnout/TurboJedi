#ifndef JEDIAPPLICATION_H
#define JEDIAPPLICATION_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TApplication
#define Uses_TMenuBar
#define Uses_TStatusLine
#include <tvision/tv.h>

#include <string>

#include "JediClock.h"
#include "JournalTools.h"

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

    void open(const JournalFilter& filter);
    bool load(const std::string& path, std::string& error);
    void chooseOpen();
    void chooseSave();
    void showHelp();
    void showAbout();

    static TStatusLine* initStatusLine(TRect bounds);
    static TMenuBar* initMenuBar(TRect bounds);
};

#endif
