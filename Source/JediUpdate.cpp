// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "Version.h"
#include "JediUpdate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <vector>
#include <sstream>
#include <algorithm>

#define CONNECT_TIMEOUT  5     // Seconds, a network without a way out tells nothing and is not waited for long
#define TRANSFER_TIMEOUT 10
#define CHECK_INTERVAL   86400  // Seconds between checks on start

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

static std::map<std::string, std::string> ReadRelease()
{
  FILE* file;
  char line[512];
  char* value;
  size_t length;
  std::map<std::string, std::string> fields;

  if ((file = fopen("/etc/os-release", "r")) == nullptr)
  {
    // Nothing to tell the distribution by
    return fields;
  }

  while (fgets(line, sizeof(line), file) != nullptr)
  {
    line[strcspn(line, "\r\n")] = '\0';

    if ((line[0] == '#') ||
        ((value = strchr(line, '=')) == nullptr))
    {
      // Comments and empty lines
      continue;
    }

    *value ++ = '\0';
    length    = strlen(value);

    if ((length >= 2) &&
        ((value[0] == '"') || (value[0] == '\'')) &&
        (value[length - 1] == value[0]))
    {
      // Quoted as the shell would take it
      value[length - 1] = '\0';
      value ++;
    }

    fields[line] = value;
  }

  fclose(file);
  return fields;
}

static bool Contains(const std::string& list, const char* word)
{
  std::string item;
  std::istringstream stream(list);

  while (stream >> item)
  {
    if (item == word)
    {
      return true;
    }
  }

  return false;
}

static std::string GetMajor(const std::string& version, const char* stops)
{
  // 13.1 is 13, so is 13/sid of a testing Debian
  return version.substr(0, version.find_first_of(stops));
}

// JediUpdate

JediUpdate::JediUpdate()
{
  ready   = false;
  multi   = nullptr;
  easy    = nullptr;
  startup = false;
  checked = 0;
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

  // A failure counts as well, a machine without a way out is not asked again before a day is over
  checked = time(nullptr);

  curl_multi_remove_handle(multi, easy);
  curl_easy_cleanup(easy);
  easy = nullptr;

  return true;
}

void JediUpdate::load(const std::string& path)
{
  FILE* file;
  char line[256];
  char* value;

  this->path = path;

  if ((path.empty()) ||
      ((file = fopen(path.c_str(), "r")) == nullptr))
  {
    // Nothing chosen yet
    return;
  }

  while (fgets(line, sizeof(line), file) != nullptr)
  {
    line[strcspn(line, "\r\n")] = '\0';

    if ((value = strchr(line, '=')) == nullptr)
    {
      // Neither a key nor a value
      continue;
    }

    *value ++ = '\0';

    if (strcmp(line, "startup") == 0) startup = (strcmp(value, "yes") == 0);
    if (strcmp(line, "checked") == 0) checked = strtoll(value, nullptr, 10);
    if (strcmp(line, "shown")   == 0) shown   = value;
  }

  fclose(file);
}

void JediUpdate::save()
{
  FILE* file;

  if ((path.empty()) ||
      ((file = fopen(path.c_str(), "w")) == nullptr))
  {
    // Kept for this run only
    return;
  }

  fprintf(file, "startup=%s\nchecked=%lld\nshown=%s\n", startup ? "yes" : "no", (long long)checked, shown.c_str());
  fclose(file);
}

bool JediUpdate::due() const
{
  return startup && (time(nullptr) - checked >= CHECK_INTERVAL);
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

std::string JediUpdate::system()
{
  FILE* file;
  char line[64];
  std::string id;
  std::string like;
  std::string version;
  std::string codename;
  std::map<std::string, std::string> fields;

  // The same as GetTarget() of install.sh, both have to name the distribution alike
  if ((fields = ReadRelease()).empty())
  {
    return std::string();
  }

  id       = fields["ID"];
  like     = fields["ID_LIKE"];
  version  = fields["VERSION_ID"];
  codename = fields["UBUNTU_CODENAME"];

  if ((id == "debian") || (id == "raspbian"))                  return "debian-" + GetMajor(version, ".");
  if (id == "ubuntu")                                          return "ubuntu-" + version;
  if (id == "fedora")                                          return "fc" + version;
  if ((id == "rhel") || (id == "almalinux") || (id == "rocky") ||
      (id == "centos") || (id == "ol") || (id == "eurolinux") ||
      (id == "circle") || (id == "navy"))                      return "el" + GetMajor(version, ".");
  if (id == "amzn")                                            return "amzn" + version;
  if (id == "opensuse-tumbleweed")                             return "tumbleweed";
  if ((id == "opensuse-leap") || (id == "sles") || (id == "sled")) return "leap" + GetMajor(version, ".");
  if (id == "arch")                                            return "arch";

  // Derivatives tell what they are made of
  if (Contains(like, "ubuntu"))
  {
    if (codename == "jammy")    return "ubuntu-22.04";
    if (codename == "noble")    return "ubuntu-24.04";
    if (codename == "resolute") return "ubuntu-26.04";
    return "ubuntu-" + codename;
  }

  if (Contains(like, "debian"))
  {
    version.clear();

    if ((file = fopen("/etc/debian_version", "r")) != nullptr)
    {
      // 13.1, or trixie/sid on testing
      if (fgets(line, sizeof(line), file) != nullptr)
      {
        line[strcspn(line, "\r\n")] = '\0';
        version = line;
      }

      fclose(file);
    }

    return "debian-" + GetMajor(version, "./");
  }

  if (Contains(like, "opensuse-tumbleweed"))                   return "tumbleweed";
  if (Contains(like, "suse"))                                  return "leap" + GetMajor(version, ".");
  if (Contains(like, "rhel") || Contains(like, "centos"))      return "el" + GetMajor(version, ".");
  if (Contains(like, "fedora"))                                return "fc" + version;
  if (Contains(like, "arch"))                                  return "arch";

  return id + "-" + version;
}
