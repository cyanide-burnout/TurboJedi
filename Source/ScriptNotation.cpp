// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "ScriptNotation.h"

extern "C"
{
#include <lauxlib.h>
}

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <set>
#include <vector>
#include <algorithm>

static const char* const Keywords[] =
{
  "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or",
  "repeat", "return", "then", "true", "until", "while"
};

// Writing

std::string QuoteNotation(const std::string& text)
{
  char code[8];
  std::string quoted;

  quoted = "'";

  for (unsigned char character : text)
  {
    switch (character)
    {
      case '\\': quoted += "\\\\"; break;
      case '\'': quoted += "\\'";  break;
      case '\n': quoted += "\\n";  break;
      case '\r': quoted += "\\r";  break;
      case '\t': quoted += "\\t";  break;

      default:
        if ((character < 0x20) || (character == 0x7f))
        {
          // The line of a layout has no room for control characters, the bytes of UTF-8 go as they are
          snprintf(code, sizeof(code), "\\%03u", character);
          quoted += code;
          break;
        }

        quoted += character;
    }
  }

  return quoted + "'";
}

static bool IsName(const std::string& text)
{
  if (text.empty() ||
      (!(isalpha(static_cast<unsigned char>(text[0])) || (text[0] == '_'))))
  {
    return false;
  }

  for (char character : text)
  {
    if (!(isalnum(static_cast<unsigned char>(character)) || (character == '_')))
    {
      return false;
    }
  }

  return std::find_if(std::begin(Keywords), std::end(Keywords), [&text] (const char* keyword) { return text == keyword; }) == std::end(Keywords);
}

static bool WriteValue(lua_State* state, int index, int depth, std::set<const void*>& path, std::string& text, std::string& error)
{
  char number[64];
  size_t length;
  const char* data;
  lua_Integer count;
  lua_Integer key;
  std::string part;
  std::vector<std::pair<std::string, std::string>> fields;

  index = lua_absindex(state, index);

  if (text.size() > NOTATION_SIZE)
  {
    error = "it is larger than " + std::to_string(NOTATION_SIZE) + " bytes";
    return false;
  }

  switch (lua_type(state, index))
  {
    case LUA_TBOOLEAN:
      text += lua_toboolean(state, index) ? "true" : "false";
      return true;

    case LUA_TNUMBER:
      if (lua_isinteger(state, index))
      {
        snprintf(number, sizeof(number), "%lld", (long long)lua_tointeger(state, index));
        text += number;
        return true;
      }

      if (!isfinite(lua_tonumber(state, index)))
      {
        error = "a number is not finite";
        return false;
      }

      // Read back as a float, not as an integer
      snprintf(number, sizeof(number), "%.17g", (double)lua_tonumber(state, index));
      text += number;
      text += (strpbrk(number, ".eEn") == nullptr) ? ".0" : "";
      return true;

    case LUA_TSTRING:
      data  = lua_tolstring(state, index, &length);
      text += QuoteNotation(std::string(data, length));
      return true;

    case LUA_TTABLE:
      break;

    default:
      error = std::string("a value is a ") + luaL_typename(state, index) + ", only strings, numbers, booleans and tables are kept";
      return false;
  }

  if (depth >= NOTATION_DEPTH)
  {
    error = "tables are nested deeper than " + std::to_string(NOTATION_DEPTH);
    return false;
  }

  if (!path.insert(lua_topointer(state, index)).second)
  {
    error = "a table contains itself";
    return false;
  }

  luaL_checkstack(state, 4, "notation");

  // The list part goes first by position, the other keys after it in their order
  for (count = 0; lua_rawgeti(state, index, count + 1) != LUA_TNIL; count ++)
  {
    lua_pop(state, 1);
  }

  lua_pop(state, 1);
  text += "{";

  for (key = 1; key <= count; key ++)
  {
    text += (key > 1) ? ", " : " ";
    lua_rawgeti(state, index, key);

    if (!WriteValue(state, -1, depth + 1, path, text, error))
    {
      lua_pop(state, 1);
      return false;
    }

    lua_pop(state, 1);
  }

  for (lua_pushnil(state); lua_next(state, index) != 0; lua_pop(state, 1))
  {
    if ((lua_type(state, -2) == LUA_TNUMBER) &&
        (lua_isinteger(state, -2)) &&
        ((key = lua_tointeger(state, -2)) >= 1) &&
        (key <= count))
    {
      // Written by position
      continue;
    }

    if (lua_type(state, -2) == LUA_TSTRING)
    {
      data = lua_tolstring(state, -2, &length);
      part = std::string(data, length);
      part = IsName(part) ? part : ("[" + QuoteNotation(part) + "]");
    }
    else if ((lua_type(state, -2) == LUA_TNUMBER) &&
             (lua_isinteger(state, -2)))
    {
      part = "[" + std::to_string((long long)lua_tointeger(state, -2)) + "]";
    }
    else
    {
      error = "a key is neither a string nor an integer";
      lua_pop(state, 2);
      return false;
    }

    part += " = ";

    if (!WriteValue(state, -1, depth + 1, path, part, error))
    {
      lua_pop(state, 2);
      return false;
    }

    fields.emplace_back(part.substr(0, part.find(" = ")), part);
  }

  // The same table is the same text whatever order the keys have come in
  std::sort(fields.begin(), fields.end());

  for (const std::pair<std::string, std::string>& field : fields)
  {
    text += (text.back() == '{') ? " " : ", ";
    text += field.second;
  }

  text += (text.back() == '{') ? "}" : " }";
  path.erase(lua_topointer(state, index));

  if (text.size() > NOTATION_SIZE)
  {
    error = "it is larger than " + std::to_string(NOTATION_SIZE) + " bytes";
    return false;
  }

  return true;
}

bool WriteNotation(lua_State* state, int index, std::string& text, std::string& error)
{
  std::set<const void*> path;

  text.clear();
  return WriteValue(state, index, 0, path, text, error);
}

// Reading

static void SkipSpace(const std::string& text, size_t& position)
{
  while ((position < text.size()) &&
         (isspace(static_cast<unsigned char>(text[position]))))
  {
    position ++;
  }
}

static bool ReadString(const std::string& text, size_t& position, std::string& value, std::string& error)
{
  char quote;
  char character;
  unsigned code;
  int digits;

  quote = text[position ++];
  value.clear();

  while (position < text.size())
  {
    character = text[position ++];

    if (character == quote)
    {
      return true;
    }

    if (character == '\n')
    {
      break;
    }

    if (character != '\\')
    {
      value += character;
      continue;
    }

    if (position >= text.size())
    {
      break;
    }

    character = text[position ++];

    switch (character)
    {
      case 'n':  value += '\n'; continue;
      case 'r':  value += '\r'; continue;
      case 't':  value += '\t'; continue;
      case 'a':  value += '\a'; continue;
      case 'b':  value += '\b'; continue;
      case 'f':  value += '\f'; continue;
      case 'v':  value += '\v'; continue;
      case '\\': value += '\\'; continue;
      case '\'': value += '\''; continue;
      case '"':  value += '"';  continue;

      case 'x':
        if ((position + 2 > text.size()) ||
            (!isxdigit(static_cast<unsigned char>(text[position]))) ||
            (!isxdigit(static_cast<unsigned char>(text[position + 1]))))
        {
          error = "\\x takes two hex digits";
          return false;
        }

        value    += static_cast<char>(strtoul(text.substr(position, 2).c_str(), nullptr, 16));
        position += 2;
        continue;
    }

    if (isdigit(static_cast<unsigned char>(character)))
    {
      // Up to three decimal digits of a byte
      code = character - '0';

      for (digits = 1; (digits < 3) && (position < text.size()) && isdigit(static_cast<unsigned char>(text[position])); digits ++)
      {
        code = code * 10 + (text[position ++] - '0');
      }

      if (code > 255)
      {
        error = "a decimal escape is larger than 255";
        return false;
      }

      value += static_cast<char>(code);
      continue;
    }

    error = std::string("unknown escape \\") + character;
    return false;
  }

  error = "a string has no end";
  return false;
}

static bool ReadValue(lua_State* state, const std::string& text, size_t& position, int depth, std::string& error)
{
  int count;
  size_t start;
  char* end;
  std::string value;
  long long integer;
  double number;

  SkipSpace(text, position);

  if (position >= text.size())
  {
    error = "a value is missing";
    return false;
  }

  luaL_checkstack(state, 4, "notation");

  if ((text[position] == '\'') ||
      (text[position] == '"'))
  {
    if (!ReadString(text, position, value, error))
    {
      return false;
    }

    lua_pushlstring(state, value.data(), value.size());
    return true;
  }

  if ((text[position] == '-') ||
      (isdigit(static_cast<unsigned char>(text[position]))))
  {
    // An integer unless it has a point or an exponent
    start = position;
    position ++;

    while ((position < text.size()) &&
           ((isalnum(static_cast<unsigned char>(text[position]))) || (text[position] == '.') ||
            (((text[position] == '-') || (text[position] == '+')) && ((text[position - 1] == 'e') || (text[position - 1] == 'E')))))
    {
      position ++;
    }

    value = text.substr(start, position - start);

    if (value.find_first_of(".eE") == std::string::npos)
    {
      errno   = 0;
      integer = strtoll(value.c_str(), &end, 10);

      if ((*end == '\0') && (errno == 0))
      {
        lua_pushinteger(state, integer);
        return true;
      }
    }
    else
    {
      number = strtod(value.c_str(), &end);

      if (*end == '\0')
      {
        lua_pushnumber(state, number);
        return true;
      }
    }

    error = "a number " + value + " cannot be read";
    return false;
  }

  if (text.compare(position, 4, "true") == 0)
  {
    position += 4;
    lua_pushboolean(state, true);
    return true;
  }

  if (text.compare(position, 5, "false") == 0)
  {
    position += 5;
    lua_pushboolean(state, false);
    return true;
  }

  if (text[position] != '{')
  {
    error = std::string("unexpected ") + text[position];
    return false;
  }

  if (depth >= NOTATION_DEPTH)
  {
    error = "tables are nested deeper than " + std::to_string(NOTATION_DEPTH);
    return false;
  }

  position ++;
  count = 0;
  lua_newtable(state);

  for (;;)
  {
    SkipSpace(text, position);

    if (position >= text.size())
    {
      error = "a table has no end";
      lua_pop(state, 1);
      return false;
    }

    if (text[position] == '}')
    {
      position ++;
      return true;
    }

    if (text[position] == '[')
    {
      // [key] = value: a string or an integer
      position ++;

      if (!ReadValue(state, text, position, NOTATION_DEPTH, error))
      {
        lua_pop(state, 1);
        return false;
      }

      if (!((lua_type(state, -1) == LUA_TSTRING) || lua_isinteger(state, -1)))
      {
        error = "a key is neither a string nor an integer";
        lua_pop(state, 2);
        return false;
      }

      SkipSpace(text, position);

      if ((position >= text.size()) ||
          (text[position] != ']'))
      {
        error = "] is missing after a key";
        lua_pop(state, 2);
        return false;
      }

      position ++;
      SkipSpace(text, position);

      if ((position >= text.size()) ||
          (text[position] != '='))
      {
        error = "= is missing after a key";
        lua_pop(state, 2);
        return false;
      }

      position ++;
    }
    else if (isalpha(static_cast<unsigned char>(text[position])) || (text[position] == '_'))
    {
      // name = value, or true and false by position
      start = position;

      while ((position < text.size()) &&
             (isalnum(static_cast<unsigned char>(text[position])) || (text[position] == '_')))
      {
        position ++;
      }

      value = text.substr(start, position - start);
      SkipSpace(text, position);

      if ((position < text.size()) &&
          (text[position] == '=') &&
          ((position + 1 >= text.size()) || (text[position + 1] != '=')))
      {
        position ++;
        lua_pushlstring(state, value.data(), value.size());
      }
      else
      {
        position = start;
        lua_pushinteger(state, ++ count);
      }
    }
    else
    {
      // A value by position
      lua_pushinteger(state, ++ count);
    }

    if (!ReadValue(state, text, position, depth + 1, error))
    {
      lua_pop(state, 2);
      return false;
    }

    lua_rawset(state, -3);
    SkipSpace(text, position);

    if ((position < text.size()) &&
        ((text[position] == ',') || (text[position] == ';')))
    {
      position ++;
    }
    else if ((position < text.size()) &&
             (text[position] != '}'))
    {
      error = ", or } is missing in a table";
      lua_pop(state, 1);
      return false;
    }
  }
}

bool ReadNotation(lua_State* state, const std::string& text, size_t& position, std::string& error)
{
  if (text.size() > NOTATION_SIZE * 2)
  {
    error = "the text is too large";
    return false;
  }

  return ReadValue(state, text, position, 0, error);
}
