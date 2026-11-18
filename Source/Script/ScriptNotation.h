#ifndef SCRIPTNOTATION_H
#define SCRIPTNOTATION_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

extern "C"
{
#include <lua.h>
}

#include <string>

// Data of plugins in the notation of a table of Lua, the way a layout keeps it on one line: strings, numbers, booleans and
// tables of them, keys of strings and integers. Read by its own parser, never by load(): a layout comes from anyone and
// runs no code

#define NOTATION_DEPTH  8      // Tables in tables
#define NOTATION_SIZE   65536  // Bytes of the text

// The value at the index as text; false and the reason for what cannot be kept: a function, a cycle, a key of another type
bool WriteNotation(lua_State* state, int index, std::string& text, std::string& error);

// One value from the text at the position, pushed; the position goes past it. False and the reason, nothing pushed then
bool ReadNotation(lua_State* state, const std::string& text, size_t& position, std::string& error);

// A string the way WriteNotation() writes one, for the parts of a layout made without Lua
std::string QuoteNotation(const std::string& text);

#endif
