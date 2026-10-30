// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "JournalEscapes.h"

#include <stdlib.h>

#include <algorithm>

// Helpers

static bool GetEscapeColor(const std::vector<int>& codes, size_t& index, TColor& color)
{
  // 5;N of 256 colors or 2;R;G;B after 38 or 48, the codes taken are skipped
  if ((index + 2 < codes.size()) &&
      (codes[index + 1] == 5))
  {
    index += 2;
    color  = TColorXTerm(codes[index]);
    return true;
  }

  if ((index + 4 < codes.size()) &&
      (codes[index + 1] == 2))
  {
    index += 4;
    color  = TColorRGB(codes[index - 2], codes[index - 1], codes[index]);
    return true;
  }

  // Cut short, nothing after it can be told apart
  index = codes.size();
  return false;
}

static void ApplyEscape(const std::string& parameters, JournalPaint& paint)
{
  // ANSI order of the colors, bright foregrounds over the blue window as grc rules have them, backgrounds stay dark
  static const uint8_t colors[] = { 0x0, 0x4, 0x2, 0x6, 0x1, 0x5, 0x3, 0x7 };

  size_t index;
  std::vector<int> codes;

  for (index = 0; index <= parameters.size(); index ++)
  {
    // Empty parameters count as zero, colons of the newer form are taken as semicolons
    if ((index == 0) || (parameters[index - 1] == ';') || (parameters[index - 1] == ':'))
    {
      codes.push_back(atoi(parameters.c_str() + index));
    }
  }

  for (index = 0; index < codes.size(); index ++)
  {
    switch (codes[index])
    {
      case 0:  paint = JournalPaint {};                                      break;
      case 1:  paint.style |=  slBold;                                       break;
      case 3:  paint.style |=  slItalic;                                     break;
      case 4:  paint.style |=  slUnderline;                                  break;
      case 5:  paint.style |=  slBlink;                                      break;
      case 7:  paint.style |=  slReverse;                                    break;
      case 22: paint.style &= ~slBold;                                       break;
      case 23: paint.style &= ~slItalic;                                     break;
      case 24: paint.style &= ~slUnderline;                                  break;
      case 25: paint.style &= ~slBlink;                                      break;
      case 27: paint.style &= ~slReverse;                                    break;
      case 39: paint.foreground = false;                                     break;
      case 49: paint.background = false;                                     break;
      case 38: paint.foreground |= GetEscapeColor(codes, index, paint.fore); break;
      case 48: paint.background |= GetEscapeColor(codes, index, paint.back); break;

      case 30 ... 37:
      case 90 ... 97:
        paint.foreground = true;
        paint.fore       = TColorBIOS(colors[codes[index] % 10] | 0x8);
        break;

      case 40 ... 47:
        paint.background = true;
        paint.back       = TColorBIOS(colors[codes[index] % 10]);
        break;

      case 100 ... 107:
        paint.background = true;
        paint.back       = TColorBIOS(colors[codes[index] % 10] | 0x8);
        break;
    }
  }
}

// Escape sequences

void TakeJournalEscapes(const std::string& message, size_t start, size_t end, std::string& text, std::vector<JournalTint>& tints, JournalPaint& paint)
{
  size_t index;
  size_t last;
  size_t from;
  unsigned char character;

  from = text.size();

  for (index = start; index < end; index ++)
  {
    character = message[index];

    if ((character == '\x1b') &&
        (index + 1 < end) &&
        (message[index + 1] == '['))
    {
      // CSI: parameters up to the final byte, SGR changes the paint, the rest is of no use in a list
      for (last = index + 2; (last < end) && ((static_cast<unsigned char>(message[last]) < 0x40) || (static_cast<unsigned char>(message[last]) > 0x7e)); last ++);

      if ((last < end) &&
          (message[last] == 'm'))
      {
        if ((text.size() > from) &&
            (paint.foreground || paint.background || paint.style))
        {
          // Text so far in the paint before the change
          tints.push_back({ from, text.size(), paint });
        }

        ApplyEscape(message.substr(index + 2, last - index - 2), paint);
        from = text.size();
      }

      index = last;
      continue;
    }

    if ((character == '\x1b') &&
        (index + 1 < end) &&
        (message[index + 1] == ']'))
    {
      // OSC (a title, a link) up to BEL or ST
      for (last = index + 2; (last < end) && (message[last] != '\x07') && ((message[last] != '\x1b') || (last + 1 >= end) || (message[last + 1] != '\\')); last ++);
      index = last + ((last < end) && (message[last] == '\x1b'));
      continue;
    }

    if (character == '\x1b')
    {
      // The other sequences: intermediate bytes, ESC ( B for one, then the final byte
      for (index ++; (index + 1 < end) && (message[index] >= 0x20) && (message[index] <= 0x2f); index ++);
      continue;
    }

    if ((character == '\t') ||
        (character == '\r'))
    {
      // Control characters would be drawn as glyphs, these keep their room
      text += ' ';
      continue;
    }

    if ((character >= 0x20) &&
        (character != 0x7f))
    {
      text += character;
    }
  }

  if ((text.size() > from) &&
      (paint.foreground || paint.background || paint.style))
  {
    // The paint goes on to the next line of the message, as on a terminal
    tints.push_back({ from, text.size(), paint });
  }
}

std::string TakeJournalEscapes(const std::string& value)
{
  size_t start;
  size_t end;
  std::string text;
  JournalPaint paint;
  std::vector<JournalTint> tints;

  paint = JournalPaint {};

  for (start = 0; start <= value.size(); start = end + 1)
  {
    // Line by line, the breaks stay where they are and the colors go
    end   = std::min(value.find('\n', start), value.size());
    text += (start != 0) ? "\n" : "";
    TakeJournalEscapes(value, start, end, text, tints, paint);
  }

  return text;
}
