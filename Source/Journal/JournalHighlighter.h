#ifndef JOURNALHIGHLIGHTER_H
#define JOURNALHIGHLIGHTER_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TColorAttr
#include <tvision/tv.h>

#include <map>
#include <memory>
#include <vector>
#include <string>

#include "JournalExpression.h"

#define JOURNAL_HIGHLIGHT_BUILTIN  "builtin"
#define JOURNAL_HIGHLIGHT_OFF      "off"

// Rules of grc (generic colouriser): regexp=, colours= for the match and its groups, count=once|more|stop,
// skip=, entries apart by lines of - or =; the built-in rules are written the same way

struct JournalPaint
{
  bool reset;               // Back to the color of the line
  bool foreground;
  bool background;
  TColor fore;
  TColor back;
  ushort style;

  TColorAttr apply(TColorAttr base) const;
};

struct JournalStroke
{
  size_t start;  // Bytes of the text
  size_t end;
  const JournalPaint* paint;
};

class JournalHighlighter
{
  public:

    // Shared by the windows, nullptr for off or when the file cannot be read
    static std::shared_ptr<JournalHighlighter> get(const std::string& name, std::string* error = nullptr);

    // Labels with names to choose from: built-in, off and the rule files of grc there are
    static void list(std::map<std::string, std::string>& values);

    // Strokes in the order of the text, later rules have painted over earlier ones
    void apply(const std::string& text, std::vector<JournalStroke>& strokes);

  private:

    enum Count
    {
      COUNT_MORE,
      COUNT_ONCE,
      COUNT_STOP
    };

    struct Rule
    {
      JournalExpression expression;
      std::vector<JournalPaint> paints;  // For the match, then for its groups
      Count count;
    };

    std::vector<Rule> rules;
    std::vector<const JournalPaint*> marks;
    std::vector<std::pair<size_t, size_t>> groups;

    bool parse(const std::string& text);
};

#endif
