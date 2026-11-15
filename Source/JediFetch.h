#ifndef JEDIFETCH_H
#define JEDIFETCH_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <string>
#include <vector>

// Requests of plugins over HTTP and HTTPS, all on one multi handle of libcurl moved on between the events: a plugin asks
// a web service without a command and without its token on a command line

struct JediRequest
{
  std::string url;
  std::string method;  // GET when empty, POST when there is a body
  std::string body;
  bool post;           // The body is sent, even an empty one
  std::vector<std::pair<std::string, std::string>> headers;
  int timeout;         // Seconds of the whole transfer, zero for no limit
};

struct JediAnswer
{
  std::string how;   // done, error, timeout or cancel
  long code;         // Status of HTTP, an answer of 404 is done as well
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;  // Of the last answer after the redirects, names in lower case
  std::string text;  // What has gone wrong
};

// A request under a number of the caller, false with the error when libcurl cannot take it
bool StartFetch(int number, const JediRequest& request, std::string& error);

// Moves the transfers on, waits up to the milliseconds for any of them first
void PollFetches(int wait);

bool HasFetches();
bool IsFetchDone(int number);

// The answer of a transfer that is done, and the transfer is gone; false while it goes on
bool TakeFetch(int number, JediAnswer& answer);

void CancelFetch(int number);
void CloseFetches();

#endif
