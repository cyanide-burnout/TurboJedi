#ifndef JOURNALMATCHER_H
#define JOURNALMATCHER_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <stdint.h>

#include <vector>
#include <string>

#include "JournalTools.h"
#include "JournalExpression.h"

// Text of the filter or of Find: plain or a regular expression, case sensitive or not, JOURNAL_TEXT_* tell which;
// leaving out the matching entries is up to the one who asks

class JournalMatcher
{
  public:

    JournalMatcher();

    // False on a broken expression, the matcher stays as it was then
    bool set(const std::string& text, uint16_t mode);
    bool empty() const;

    const std::string& getText() const;
    uint16_t getMode() const;

    bool match(const std::string& text);
    void locate(const std::string& text, std::vector<std::pair<size_t, size_t>>& ranges);

  private:

    std::string pattern;
    uint16_t mode;
    JournalExpression expression;
    std::vector<std::pair<size_t, size_t>> groups;
};

#endif
