// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "Version.h"
#include "JediUpdate.h"

#include <stdlib.h>
#include <string.h>

#include <vector>
#include <algorithm>

#define CONNECT_TIMEOUT  5     // Seconds, a network without a way out tells nothing and is not waited for long
#define TRANSFER_TIMEOUT 10

// Helpers

static std::vector<long> ParseVersion(const std::string& text)
{
  const char* position;
  char* end;
  std::vector<long> numbers;

  if (text.compare(0, 1, "v") != 0)
  {
    // Unknown, or a commit of a clone without tags that may start with a digit
    return numbers;
  }

  // v1.2.3 and what goes after it, such as -5-gabcdef of a build after the release
  position = text.c_str() + 1;

  while ((*position >= '0') &&
         (*position <= '9'))
  {
    numbers.push_back(strtol(position, &end, 10));
    position = end + ((*end == '.') ? 1 : 0);

    if (*end != '.')
    {
      // The release number is over
      break;
    }
  }

  return numbers;
}

// JediUpdate

JediUpdate::JediUpdate()
{
  ready = false;
  multi = nullptr;
  easy  = nullptr;
}

JediUpdate::~JediUpdate()
{
  if (easy != nullptr)
  {
    // Left running when the application quits
    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy);
  }

  if (multi != nullptr)
  {
    curl_multi_cleanup(multi);
  }

  if (ready)
  {
    curl_global_cleanup();
  }
}

bool JediUpdate::start()
{
  if ((easy != nullptr) ||
      ((!ready) && (!(ready = (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)))) ||
      ((multi == nullptr) && ((multi = curl_multi_init()) == nullptr)) ||
      ((easy = curl_easy_init()) == nullptr))
  {
    // One check at a time; libcurl is set up on the first check only, TurboJedi does not touch the network otherwise
    return false;
  }

  tag.clear();
  error.clear();

  // The answer is a redirect to releases/tag/<tag>, it is not followed and has no body to read
  curl_easy_setopt(easy, CURLOPT_URL,            JEDI_UPDATE_REPOSITORY "/releases/latest");
  curl_easy_setopt(easy, CURLOPT_NOBODY,         1L);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT,      "TurboJedi/" VERSION);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, (long)CONNECT_TIMEOUT);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT,        (long)TRANSFER_TIMEOUT);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL,       1L);

  if (curl_multi_add_handle(multi, easy) != CURLM_OK)
  {
    curl_easy_cleanup(easy);
    easy = nullptr;
    return false;
  }

  return true;
}

bool JediUpdate::busy() const
{
  return easy != nullptr;
}

bool JediUpdate::poll()
{
  int running;
  int left;
  long code;
  CURLcode result;
  CURLMsg* message;
  const char* location;
  const char* found;

  if ((easy == nullptr) ||
      (curl_multi_perform(multi, &running) != CURLM_OK) ||
      (running > 0))
  {
    // Nothing started or still on the way
    return false;
  }

  result = CURLE_OK;

  while ((message = curl_multi_info_read(multi, &left)) != nullptr)
  {
    if (message->msg == CURLMSG_DONE)
    {
      // The only transfer there is
      result = message->data.result;
    }
  }

  code     = 0;
  location = nullptr;

  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL,  &location);

  if (result != CURLE_OK)
  {
    // No network, no name of the host, a timeout
    error = curl_easy_strerror(result);
  }
  else if ((location == nullptr) ||
           ((found = strstr(location, "/releases/tag/")) == nullptr))
  {
    // Rate limited or GitHub has changed
    error = "GitHub has answered with HTTP " + std::to_string(code);
  }
  else
  {
    tag = found + strlen("/releases/tag/");
  }

  curl_multi_remove_handle(multi, easy);
  curl_easy_cleanup(easy);
  easy = nullptr;

  return true;
}

int JediUpdate::compare(const std::string& one, const std::string& other)
{
  size_t index;
  std::vector<long> first;
  std::vector<long> second;

  first  = ParseVersion(one);
  second = ParseVersion(other);

  for (index = 0; index < std::max(first.size(), second.size()); index ++)
  {
    // Missing numbers are zeros: v0.9 is v0.9.0
    if (((index < first.size()) ? first[index] : 0) != ((index < second.size()) ? second[index] : 0))
    {
      return (((index < first.size()) ? first[index] : 0) < ((index < second.size()) ? second[index] : 0)) ? -1 : 1;
    }
  }

  return 0;
}
