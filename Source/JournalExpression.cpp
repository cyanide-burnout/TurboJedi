// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalExpression.h"

#include <string.h>

// JournalExpression

JournalExpression::JournalExpression()
{
  code = nullptr;
  data = nullptr;
}

JournalExpression::JournalExpression(JournalExpression&& other)
{
  code       = other.code;
  data       = other.data;
  other.code = nullptr;
  other.data = nullptr;
}

JournalExpression::~JournalExpression()
{
  release();
}

JournalExpression& JournalExpression::operator=(JournalExpression&& other)
{
  if (this != &other)
  {
    release();
    code       = other.code;
    data       = other.data;
    other.code = nullptr;
    other.data = nullptr;
  }

  return *this;
}

bool JournalExpression::compile(const std::string& pattern, bool caseless, std::string* error)
{
  int number;
  PCRE2_SIZE offset;
  pcre2_code* compiled;
  PCRE2_UCHAR message[256];

  // Messages are mostly UTF-8, a broken sequence must not fail the match of the whole line
  compiled = pcre2_compile((PCRE2_SPTR)pattern.data(), pattern.size(), PCRE2_UTF | PCRE2_MATCH_INVALID_UTF | (caseless ? PCRE2_CASELESS : 0), &number, &offset, nullptr);

  if (compiled == nullptr)
  {
    if (error != nullptr)
    {
      pcre2_get_error_message(number, message, sizeof(message));
      *error = (const char*)message;
    }

    return false;
  }

  release();

  // Without JIT the interpreter does the same, only slower
  code = compiled;
  data = pcre2_match_data_create_from_pattern(code, nullptr);
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);

  return true;
}

bool JournalExpression::empty() const
{
  return code == nullptr;
}

bool JournalExpression::match(const std::string& text)
{
  return (code != nullptr) && (pcre2_match(code, (PCRE2_SPTR)text.data(), text.size(), 0, 0, data, nullptr) >= 0);
}

bool JournalExpression::search(const std::string& text, size_t offset, std::vector<std::pair<size_t, size_t>>& groups)
{
  int count;
  int index;
  PCRE2_SIZE* vector;

  groups.clear();

  if ((code == nullptr) ||
      (offset > text.size()) ||
      ((count = pcre2_match(code, (PCRE2_SPTR)text.data(), text.size(), offset, 0, data, nullptr)) < 0))
  {
    return false;
  }

  // Count of zero means the vector is too short for all groups, it holds as many as it can
  vector = pcre2_get_ovector_pointer(data);
  count  = (count > 0) ? count : (int)pcre2_get_ovector_count(data);

  for (index = 0; index < count; index ++)
  {
    groups.emplace_back(
      (vector[index * 2] != PCRE2_UNSET) ? vector[index * 2]     : std::string::npos,
      (vector[index * 2] != PCRE2_UNSET) ? vector[index * 2 + 1] : std::string::npos);
  }

  return true;
}

size_t JournalExpression::advance(const std::string& text, size_t start, size_t end)
{
  if (end > start)
  {
    return end;
  }

  for (end ++; (end < text.size()) && ((text[end] & 0xc0) == 0x80); end ++)
  {
    // Continuation bytes of UTF-8
  }

  return end;
}

void JournalExpression::release()
{
  if (data != nullptr) pcre2_match_data_free(data);
  if (code != nullptr) pcre2_code_free(code);

  code = nullptr;
  data = nullptr;
}
