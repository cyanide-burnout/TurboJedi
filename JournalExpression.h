#ifndef JOURNALEXPRESSION_H
#define JOURNALEXPRESSION_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define PCRE2_CODE_UNIT_WIDTH  8
#include <pcre2.h>

#include <vector>
#include <string>

// Regular expression of PCRE2 with JIT, UTF-8 and text that is not quite UTF-8, the syntax of Perl and Python

class JournalExpression
{
  public:

    JournalExpression();
    JournalExpression(JournalExpression&& other);
    JournalExpression(const JournalExpression&) = delete;
    ~JournalExpression();

    JournalExpression& operator=(JournalExpression&& other);
    JournalExpression& operator=(const JournalExpression&) = delete;

    bool compile(const std::string& pattern, bool caseless, std::string* error = nullptr);
    bool empty() const;

    bool match(const std::string& text);

    // Next match from the offset on: byte ranges of the match and its groups, npos for a group that has not taken part
    bool search(const std::string& text, size_t offset, std::vector<std::pair<size_t, size_t>>& groups);

    // Where the search goes on after a match that has ended there, past a whole character after an empty one
    static size_t advance(const std::string& text, size_t start, size_t end);

  private:

    pcre2_code* code;
    pcre2_match_data* data;

    void release();
};

#endif
