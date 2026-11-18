// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "Version.h"
#include "JediFetch.h"

#include <ctype.h>
#include <string.h>

#include <map>

#include <curl/curl.h>

#define FETCH_LIMIT      (16 << 20)  // Bytes of an answer kept, a larger one is an error
#define CONNECT_TIMEOUT  10          // Seconds, unless the whole transfer is given less
#define REDIRECT_LIMIT   5

struct Transfer
{
  CURL* easy;
  struct curl_slist* list;
  bool done;
  bool over;  // The answer has gone past FETCH_LIMIT
  CURLcode result;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  char error[CURL_ERROR_SIZE];
};

static bool ready;  // libcurl is set up, on the first request only
static CURLM* multi;
static std::map<int, Transfer> transfers;

static size_t WriteBody(char* data, size_t size, size_t count, void* context)
{
  Transfer* transfer;

  transfer = static_cast<Transfer*>(context);

  if (transfer->body.size() + size * count > FETCH_LIMIT)
  {
    // Less than asked ends the transfer
    transfer->over = true;
    return 0;
  }

  transfer->body.append(data, size * count);
  return size * count;
}

static size_t WriteHeader(char* data, size_t size, size_t count, void* context)
{
  size_t length;
  size_t colon;
  size_t start;
  std::string line;
  std::string name;
  std::string value;
  Transfer* transfer;

  transfer = static_cast<Transfer*>(context);
  length   = size * count;
  line.assign(data, length);

  while ((!line.empty()) &&
         ((line.back() == '\r') || (line.back() == '\n')))
  {
    line.pop_back();
  }

  if (line.compare(0, 5, "HTTP/") == 0)
  {
    // A new answer: a redirect or 100 Continue was before it
    transfer->headers.clear();
    return length;
  }

  if ((colon = line.find(':')) == std::string::npos)
  {
    return length;
  }

  name  = line.substr(0, colon);
  start = line.find_first_not_of(" \t", colon + 1);
  value = (start != std::string::npos) ? line.substr(start) : std::string();

  for (char& character : name)
  {
    character = tolower(static_cast<unsigned char>(character));
  }

  transfer->headers.emplace_back(name, value);
  return length;
}

static void Drop(std::map<int, Transfer>::iterator transfer)
{
  curl_multi_remove_handle(multi, transfer->second.easy);
  curl_easy_cleanup(transfer->second.easy);
  curl_slist_free_all(transfer->second.list);
  transfers.erase(transfer);
}

bool StartFetch(int number, const JediRequest& request, std::string& error)
{
  CURL* easy;
  std::string line;
  Transfer* transfer;

  if (((!ready) && (!(ready = (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)))) ||
      ((multi == nullptr) && ((multi = curl_multi_init()) == nullptr)) ||
      ((easy = curl_easy_init()) == nullptr))
  {
    error = "libcurl cannot be set up";
    return false;
  }

  transfer = &transfers[number];
  transfer->easy     = easy;
  transfer->list     = nullptr;
  transfer->done     = false;
  transfer->over     = false;
  transfer->result   = CURLE_OK;
  transfer->error[0] = '\0';

  for (const std::pair<std::string, std::string>& header : request.headers)
  {
    // An empty value leaves a header of libcurl out, as Accept: of the command curl does
    line = header.first + (header.second.empty() ? ":" : ": " + header.second);
    transfer->list = curl_slist_append(transfer->list, line.c_str());
  }

  curl_easy_setopt(easy, CURLOPT_URL,            request.url.c_str());
  curl_easy_setopt(easy, CURLOPT_PRIVATE,        transfer);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER,    transfer->error);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION,  WriteBody);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA,      transfer);
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, WriteHeader);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA,     transfer);
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER,     transfer->list);
  curl_easy_setopt(easy, CURLOPT_USERAGENT,      "TurboJedi/" VERSION);
  curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_MAXREDIRS,      (long)REDIRECT_LIMIT);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL,       1L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT,        (long)request.timeout);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, (long)(((request.timeout > 0) && (request.timeout < CONNECT_TIMEOUT)) ? request.timeout : CONNECT_TIMEOUT));

  // Only the web, a plugin does not read a file:// of this machine nor follow a redirect there
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR,       "http,https");
  curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS,       (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif

  if (request.post)
  {
    // Copied, the request of the caller may be gone before the transfer is over
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request.body.size());
    curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS,      request.body.data());
  }

  if (request.method == "HEAD")
  {
    curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
  }
  else if ((!request.method.empty()) &&
           (request.method != (request.post ? "POST" : "GET")))
  {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, request.method.c_str());
  }

  if (curl_multi_add_handle(multi, easy) != CURLM_OK)
  {
    curl_easy_cleanup(easy);
    curl_slist_free_all(transfer->list);
    transfers.erase(number);
    error = "libcurl cannot start the transfer";
    return false;
  }

  return true;
}

void PollFetches(int wait)
{
  int running;
  int left;
  char* context;
  CURLMsg* message;

  if (transfers.empty())
  {
    return;
  }

  if (wait > 0)
  {
    curl_multi_poll(multi, nullptr, 0, wait, nullptr);
  }

  curl_multi_perform(multi, &running);

  while ((message = curl_multi_info_read(multi, &left)) != nullptr)
  {
    if ((message->msg == CURLMSG_DONE) &&
        (curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &context) == CURLE_OK))
    {
      reinterpret_cast<Transfer*>(context)->done   = true;
      reinterpret_cast<Transfer*>(context)->result = message->data.result;
    }
  }
}

bool HasFetches()
{
  return !transfers.empty();
}

bool IsFetchDone(int number)
{
  std::map<int, Transfer>::iterator transfer;

  return ((transfer = transfers.find(number)) != transfers.end()) && transfer->second.done;
}

bool TakeFetch(int number, JediAnswer& answer)
{
  std::map<int, Transfer>::iterator transfer;

  if (((transfer = transfers.find(number)) == transfers.end()) ||
      (!transfer->second.done))
  {
    return false;
  }

  answer.code = 0;
  answer.text.clear();
  curl_easy_getinfo(transfer->second.easy, CURLINFO_RESPONSE_CODE, &answer.code);

  if (transfer->second.result == CURLE_OK)
  {
    answer.how     = "done";
    answer.body    = std::move(transfer->second.body);
    answer.headers = std::move(transfer->second.headers);
  }
  else if (transfer->second.over)
  {
    answer.how  = "error";
    answer.text = "the answer is larger than " + std::to_string(FETCH_LIMIT >> 20) + " MiB";
  }
  else
  {
    // The buffer tells more than the code, such as the name that has not been found
    answer.how  = (transfer->second.result == CURLE_OPERATION_TIMEDOUT) ? "timeout" : "error";
    answer.text = (transfer->second.error[0] != '\0') ? transfer->second.error : curl_easy_strerror(transfer->second.result);
  }

  Drop(transfer);
  return true;
}

void CancelFetch(int number)
{
  std::map<int, Transfer>::iterator transfer;

  if ((transfer = transfers.find(number)) != transfers.end())
  {
    Drop(transfer);
  }
}

void CloseFetches()
{
  while (!transfers.empty())
  {
    Drop(transfers.begin());
  }

  if (multi != nullptr)
  {
    curl_multi_cleanup(multi);
    multi = nullptr;
  }

  if (ready)
  {
    curl_global_cleanup();
    ready = false;
  }
}
