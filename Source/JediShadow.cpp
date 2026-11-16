// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TProgram
#define Uses_TDeskTop
#include "JediShadow.h"

#include <stdio.h>
#include <string.h>

// Of Turbo Vision, the size and the color its views draw the shadows with
extern TPoint shadowSize;
extern TColorAttr shadowAttr;

static const char* const names[] = { "off", "light", "medium", "dark" };

static int level = SHADOW_MEDIUM;
static std::string file;

void LoadJediShadow(const std::string& path)
{
  FILE* stream;
  char line[256];
  char* value;
  int index;

  file = path;

  if ((path.empty()) ||
      ((stream = fopen(path.c_str(), "r")) == nullptr))
  {
    // Nothing chosen yet
    return;
  }

  while (fgets(line, sizeof(line), stream) != nullptr)
  {
    line[strcspn(line, "\r\n")] = '\0';

    if ((value = strchr(line, '=')) == nullptr)
    {
      // Neither a key nor a value
      continue;
    }

    *value ++ = '\0';

    for (index = 0; (index < 4) && (strcmp(line, "shadows") == 0); index ++)
    {
      if (strcmp(value, names[index]) == 0)
      {
        level = index;
      }
    }
  }

  fclose(stream);
}

void SetJediShadow(int value)
{
  FILE* stream;

  level = value;
  ApplyJediShadow();

  if ((!file.empty()) &&
      ((stream = fopen(file.c_str(), "w")) != nullptr))
  {
    fprintf(stream, "shadows=%s\n", names[level]);
    fclose(stream);
  }

  if (TProgram::deskTop != nullptr)
  {
    // Shadows lie on the views under the windows, the desktop draws them all again past its buffer
    TProgram::deskTop->redraw();
  }
}

int GetJediShadow()
{
  return level;
}

void ApplyJediShadow()
{
  // None: no room either. Light: gray letters on dark gray; medium: dark gray on black as Turbo Vision has it; dark:
  // black of RGB, which a theme of the terminal does not turn gray as it may the black of the palette
  shadowSize = (level == SHADOW_OFF) ? TPoint { 0, 0 } : TPoint { 2, 1 };

  switch (level)
  {
    case SHADOW_LIGHT:  shadowAttr = TColorAttr(TColorBIOS(0x7), TColorBIOS(0x8));                          break;
    case SHADOW_DARK:   shadowAttr = TColorAttr(TColorRGB(0x30, 0x30, 0x30), TColorRGB(0x00, 0x00, 0x00));  break;
    default:            shadowAttr = 0x08;                                                                  break;
  }
}
