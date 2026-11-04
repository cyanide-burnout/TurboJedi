#ifndef JEDIUPDATE_H
#define JEDIUPDATE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <string>

#include <curl/curl.h>

#define JEDI_UPDATE_REPOSITORY  "https://github.com/cyanide-burnout/TurboJedi"
#define JEDI_UPDATE_COMMAND     "curl -fsSL https://raw.githubusercontent.com/cyanide-burnout/TurboJedi/master/install.sh | sh"

// Latest release of GitHub, asked only when the user wants to know: the redirect of releases/latest names its tag,
// nothing else is read. The transfer goes on in idle() of the application, the journal windows keep following

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

    // Release numbers of two versions such as v0.9 and v0.9-5-gabcdef compared, a build after a release is that release
    static int compare(const std::string& one, const std::string& other);

  private:

    bool ready;  // libcurl is set up
    CURLM* multi;
    CURL* easy;
};

#endif
