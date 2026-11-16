#ifndef JEDISHADOW_H
#define JEDISHADOW_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TView
#include <tvision/tv.h>

#include <string>

// Shadows of all windows and dialogs alike: none, lighter or darker than the ones of Turbo Vision

#define SHADOW_OFF     0
#define SHADOW_LIGHT   1
#define SHADOW_MEDIUM  2  // Of Turbo Vision, the default
#define SHADOW_DARK    3

// The level chosen before, of a file of key=value lines; a missing file leaves the default
void LoadJediShadow(const std::string& path);

// A new level, drawn at once and kept in the file for the next start
void SetJediShadow(int level);
int GetJediShadow();

// The size and the color of the level again, after Turbo Vision has set the ones of the screen
void ApplyJediShadow();

#endif
