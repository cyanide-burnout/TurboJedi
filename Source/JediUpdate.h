#ifndef JEDIUPDATE_H
#define JEDIUPDATE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <time.h>

#include <string>

#include <curl/curl.h>

#define JEDI_UPDATE_REPOSITORY  "https://github.com/cyanide-burnout/TurboJedi"
#define JEDI_UPDATE_COMMAND     "curl -fsSL https://raw.githubusercontent.com/cyanide-burnout/TurboJedi/master/install.sh | sh"

// Latest release of GitHub, asked when the user wants to know or on start once a day when the user has chosen so:
// the redirect of releases/latest names its tag, nothing else is read. The transfer goes on in idle() of the application,
// the journal windows keep following. The choice and the last answer are kept in a file of their own, not in a layout,
// which may be passed to others

class JediUpdate
{
  public:

    JediUpdate();
    ~JediUpdate();

    bool start();     // false while a check is running already or when it cannot begin
    bool busy() const;
    bool poll();      // true once the check is over, tag or error tell how it went

    std::string tag;    // Of the latest release, such as v0.10
    std::string error;  // Empty when the tag has been found

    bool startup;       // Checks on start, off unless chosen
    time_t checked;     // When a check has been over the last time, whatever its answer
    std::string shown;  // Newer release told about by a check on start, it is not told again

    void load(const std::string& path);  // Leaves the defaults when the file is not there
    void save();
    bool due() const;                    // A check on start has been chosen and the last check is a day old

    // Release numbers of two versions such as v0.9 and v0.9-5-gabcdef compared, a build after a release is that release
    static int compare(const std::string& one, const std::string& other);

  private:

    std::string path;

    bool ready;  // libcurl is set up
    CURLM* multi;
    CURL* easy;
};

#endif
