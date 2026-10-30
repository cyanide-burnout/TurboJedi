#ifndef JOURNALESCAPES_H
#define JOURNALESCAPES_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

#include "JournalHighlighter.h"

struct JournalTint
{
  size_t start;  // Bytes of the text
  size_t end;
  JournalPaint paint;
};

// Escape sequences of a terminal in the bytes from start to end of a message: SGR becomes tints of the text, the rest goes
// along with control characters, tab and CR keep their room as spaces. The paint goes on to the next line as on a terminal
void TakeJournalEscapes(const std::string& message, size_t start, size_t end, std::string& text, std::vector<JournalTint>& tints, JournalPaint& paint);

// Text of a value without its sequences, line breaks stay
std::string TakeJournalEscapes(const std::string& value);

#endif
