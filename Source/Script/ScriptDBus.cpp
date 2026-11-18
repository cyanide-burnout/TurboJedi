// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: dbus() and services(), the values of D-Bus as Lua has them and back

#include "ScriptPrivate.h"

#define DBUS_TIMEOUT       25      // Seconds a call of dbus() may take by default, as sd-bus has it
#define DBUS_LIST_TIME     5000    // Milliseconds services() may take outside a menu
#define DBUS_TIMEOUT_LIMIT 86400   // Seconds a call of dbus() may be given at most
#define DBUS_SIGNATURE     255     // Characters of a signature, as D-Bus has it

std::map<int, Request> invocations;  // Calls of dbus() with a callback, their calls go by the same number

// D-Bus

// The length of the complete type a signature starts with, zero when it does not start with one
static size_t MeasureType(const char* signature)
{
  size_t inner;
  size_t length;

  switch (signature[0])
  {
    case 'y': case 'n': case 'q': case 'i': case 'u': case 'x': case 't': case 'd':
    case 'b': case 's': case 'o': case 'g': case 'h': case 'v':
      return 1;

    case 'a':
      if (signature[1] == '{')
      {
        // A key of a basic type and a value of one complete type
        if ((signature[2] == '\0') ||
            (strchr("ynqiuxtdbsog", signature[2]) == nullptr))
        {
          return 0;
        }

        inner = MeasureType(signature + 3);
        return ((inner > 0) && (signature[3 + inner] == '}')) ? (inner + 4) : 0;
      }

      inner = MeasureType(signature + 1);
      return (inner > 0) ? (inner + 1) : 0;

    case '(':
      for (length = 1; signature[length] != ')'; length += inner)
      {
        if ((inner = MeasureType(signature + length)) == 0)
        {
          return 0;
        }
      }

      return (length > 1) ? (length + 1) : 0;
  }

  return 0;
}

// A table of a struct, an array or a dictionary
static bool AppendContainer(lua_State* state, int index, const std::string& type, sd_bus_message* message, std::string& error)
{
  size_t at;
  size_t length;
  size_t size;
  lua_Integer count;
  lua_Integer number;
  std::string inner;
  const char* text;

  if ((type == "ay") &&
      (lua_type(state, index) == LUA_TSTRING))
  {
    // Bytes as a string of Lua
    text = lua_tolstring(state, index, &size);
    return sd_bus_message_append_array(message, 'y', text, size) >= 0;
  }

  if (lua_type(state, index) != LUA_TTABLE)
  {
    error = type + " takes a table" + ((type == "ay") ? " or a string" : "");
    return false;
  }

  if (type[0] == '(')
  {
    inner = type.substr(1, type.size() - 2);

    if (sd_bus_message_open_container(message, 'r', inner.c_str()) < 0)
    {
      error = "the struct " + type + " cannot be made";
      return false;
    }

    for (at = 0, number = 1; at < inner.size(); at += length, number ++)
    {
      length = MeasureType(inner.c_str() + at);
      lua_rawgeti(state, index, number);

      if (lua_isnil(state, -1))
      {
        error = "the struct " + type + " lacks its value " + std::to_string(number);
        return false;
      }

      if (!AppendValue(state, -1, inner.substr(at, length), message, error))
      {
        return false;
      }

      lua_pop(state, 1);
    }

    return sd_bus_message_close_container(message) >= 0;
  }

  inner = type.substr(1);

  if (sd_bus_message_open_container(message, 'a', inner.c_str()) < 0)
  {
    error = "the array " + type + " cannot be made";
    return false;
  }

  if (inner[0] == '{')
  {
    // A dictionary from the pairs of the table, a key of the type of the signature
    inner = inner.substr(1, inner.size() - 2);
    lua_pushnil(state);

    while (lua_next(state, index) != 0)
    {
      if ((sd_bus_message_open_container(message, 'e', inner.c_str()) < 0) ||
          (!AppendValue(state, -2, inner.substr(0, 1), message, error)) ||
          (!AppendValue(state, -1, inner.substr(1), message, error)) ||
          (sd_bus_message_close_container(message) < 0))
      {
        if (error.empty()) error = "the dictionary " + type + " cannot be made";
        return false;
      }

      lua_pop(state, 1);
    }

    return sd_bus_message_close_container(message) >= 0;
  }

  count = lua_rawlen(state, index);

  for (number = 1; number <= count; number ++)
  {
    lua_rawgeti(state, index, number);

    if (!AppendValue(state, -1, inner, message, error))
    {
      return false;
    }

    lua_pop(state, 1);
  }

  return sd_bus_message_close_container(message) >= 0;
}

// A value of Lua as the type of the signature has it: a mistake of the plugin is told in the error
bool AppendValue(lua_State* state, int index, const std::string& type, sd_bus_message* message, std::string& error)
{
  int flag;
  int whole;
  int result;
  bool pushed;
  size_t length;
  double real;
  lua_Integer value;
  uint64_t unsigned64;
  const char* text;
  std::string guess;

  // Bounds of the integers of D-Bus, t takes any integer of Lua as its bits
  static const struct { char code; lua_Integer low; lua_Integer high; } ranges[] =
  {
    { 'y', 0, 255 }, { 'n', -32768, 32767 }, { 'q', 0, 65535 }, { 'i', INT32_MIN, INT32_MAX }, { 'u', 0, UINT32_MAX },
    { 'x', LUA_MININTEGER, LUA_MAXINTEGER }, { 't', LUA_MININTEGER, LUA_MAXINTEGER }
  };

  index  = lua_absindex(state, index);
  pushed = false;

  if (!lua_checkstack(state, 8))
  {
    // Not raised: no object of C++ is left behind by an error of Lua
    error = "the value is nested too deep";
    return false;
  }

  switch (type[0])
  {
    case 'y': case 'n': case 'q': case 'i': case 'u': case 'x': case 't':
      value = lua_tointegerx(state, index, &whole);

      if ((lua_type(state, index) != LUA_TNUMBER) ||
          (!whole))
      {
        error = type + " takes an integer";
        return false;
      }

      for (const auto& range : ranges)
      {
        if ((range.code == type[0]) &&
            ((value < range.low) || (value > range.high)))
        {
          error = type + " takes an integer of " + std::to_string(range.low) + " to " + std::to_string(range.high);
          return false;
        }
      }

      switch (type[0])
      {
        case 'y': { uint8_t  item = value; result = sd_bus_message_append_basic(message, 'y', &item); break; }
        case 'n': { int16_t  item = value; result = sd_bus_message_append_basic(message, 'n', &item); break; }
        case 'q': { uint16_t item = value; result = sd_bus_message_append_basic(message, 'q', &item); break; }
        case 'i': { int32_t  item = value; result = sd_bus_message_append_basic(message, 'i', &item); break; }
        case 'u': { uint32_t item = value; result = sd_bus_message_append_basic(message, 'u', &item); break; }
        case 'x': { int64_t  item = value; result = sd_bus_message_append_basic(message, 'x', &item); break; }
        default:
          // The bits as they are: -1 is the largest one
          memcpy(&unsigned64, &value, sizeof(unsigned64));
          result = sd_bus_message_append_basic(message, 't', &unsigned64);
          break;
      }

      return result >= 0;

    case 'd':
      if (lua_type(state, index) != LUA_TNUMBER)
      {
        error = "d takes a number";
        return false;
      }

      real = lua_tonumber(state, index);
      return sd_bus_message_append_basic(message, 'd', &real) >= 0;

    case 'b':
      if (lua_type(state, index) != LUA_TBOOLEAN)
      {
        error = "b takes true or false";
        return false;
      }

      flag = lua_toboolean(state, index);
      return sd_bus_message_append_basic(message, 'b', &flag) >= 0;

    case 's': case 'o': case 'g':
      if (lua_type(state, index) != LUA_TSTRING)
      {
        error = type + " takes a string";
        return false;
      }

      text = lua_tolstring(state, index, &length);

      if (strlen(text) != length)
      {
        error = type + " takes a string without NUL";
        return false;
      }

      if (sd_bus_message_append_basic(message, type[0], text) < 0)
      {
        // Not UTF-8, a NUL inside, not an object path or not a signature
        error = std::string("'") + text + "' is not " + ((type[0] == 's') ? "text D-Bus takes" : (type[0] == 'o') ? "an object path" : "a signature");
        return false;
      }

      return true;

    case 'h':
      error = "h, a descriptor of a file, is not passed to D-Bus by plugins";
      return false;

    case 'v':
      if (lua_type(state, index) == LUA_TTABLE)
      {
        // { type = 'u', value = 5 } names the type itself
        lua_pushliteral(state, "type");
        lua_rawget(state, index);

        if ((lua_type(state, -1) != LUA_TSTRING) ||
            (lua_rawlen(state, -1) > DBUS_SIGNATURE) ||
            (MeasureType(lua_tostring(state, -1)) != lua_rawlen(state, -1)))
        {
          error = "a table in v names its type with type, as { type = 'u', value = 5 }";
          return false;
        }

        guess = lua_tostring(state, -1);
        lua_pop(state, 1);
        lua_pushliteral(state, "value");
        lua_rawget(state, index);
        index = lua_gettop(state);
        pushed = true;
      }
      else
      {
        switch (lua_type(state, index))
        {
          case LUA_TSTRING:  guess = "s"; break;
          case LUA_TBOOLEAN: guess = "b"; break;
          case LUA_TNUMBER:
            // As the cores of TetraPack guess it, what the options of a{sv} take most: i while it fits, t above, x
            // below, d for a float
            value = lua_tointeger(state, index);
            guess = !lua_isinteger(state, index) ? "d" : (value > INT32_MAX) ? "t" : (value < INT32_MIN) ? "x" : "i";
            break;
          default:
            error = "v takes a string, a number, true or false, or { type = ..., value = ... }";
            return false;
        }
      }

      if ((sd_bus_message_open_container(message, 'v', guess.c_str()) < 0) ||
          (!AppendValue(state, index, guess, message, error)) ||
          (sd_bus_message_close_container(message) < 0))
      {
        if (error.empty()) error = "the variant of " + guess + " cannot be made";
        return false;
      }

      if (pushed)
      {
        // The stack as it has been: a caller goes on by the index of its own value
        lua_pop(state, 1);
      }

      return true;
  }

  return AppendContainer(state, index, type, message, error);
}

// One value of a message at its place, pushed: 1, 0 at the end of its container, below zero for an error
int PushDBusValue(lua_State* state, sd_bus_message* message)
{
  int result;
  char type;
  size_t size;
  lua_Integer bits;
  const void* bytes;
  const char* contents;
  std::string inner;
  lua_Integer number;

  union
  {
    uint8_t y; int16_t n; uint16_t q; int32_t i; uint32_t u; int64_t x; uint64_t t; double d; int b; const char* s;
  } value;

  if (!lua_checkstack(state, 8))
  {
    return -E2BIG;
  }

  if ((result = sd_bus_message_peek_type(message, &type, &contents)) <= 0)
  {
    return result;
  }

  switch (type)
  {
    case 'y': case 'n': case 'q': case 'i': case 'u': case 'x': case 't': case 'd': case 'b': case 's': case 'o': case 'g':
      if ((result = sd_bus_message_read_basic(message, type, &value)) < 0)
      {
        return result;
      }

      switch (type)
      {
        case 'y': lua_pushinteger(state, value.y); break;
        case 'n': lua_pushinteger(state, value.n); break;
        case 'q': lua_pushinteger(state, value.q); break;
        case 'i': lua_pushinteger(state, value.i); break;
        case 'u': lua_pushinteger(state, value.u); break;
        case 'x': lua_pushinteger(state, value.x); break;
        case 't': memcpy(&bits, &value.t, sizeof(bits)); lua_pushinteger(state, bits); break;  // As string.unpack('J') has it
        case 'd': lua_pushnumber(state, value.d); break;
        case 'b': lua_pushboolean(state, value.b); break;
        default:  lua_pushstring(state, value.s); break;
      }

      return 1;

    case 'a':
      if (strcmp(contents, "y") == 0)
      {
        if ((result = sd_bus_message_read_array(message, 'y', &bytes, &size)) < 0)
        {
          return result;
        }

        lua_pushlstring(state, static_cast<const char*>(bytes), size);
        return 1;
      }

      if ((result = sd_bus_message_enter_container(message, 'a', contents)) < 0)
      {
        return result;
      }

      lua_newtable(state);

      if (contents[0] == '{')
      {
        inner = std::string(contents + 1, strlen(contents) - 2);

        while ((result = sd_bus_message_enter_container(message, 'e', inner.c_str())) > 0)
        {
          if (((result = PushDBusValue(state, message)) <= 0) ||
              ((result = PushDBusValue(state, message)) <= 0))
          {
            return (result < 0) ? result : -EBADMSG;
          }

          lua_rawset(state, -3);

          if ((result = sd_bus_message_exit_container(message)) < 0)
          {
            return result;
          }
        }
      }
      else
      {
        for (number = 1; (result = PushDBusValue(state, message)) > 0; number ++)
        {
          lua_rawseti(state, -2, number);
        }
      }

      return (result < 0) ? result : ((result = sd_bus_message_exit_container(message)) < 0) ? result : 1;

    case 'r':
      if ((result = sd_bus_message_enter_container(message, 'r', contents)) < 0)
      {
        return result;
      }

      lua_newtable(state);

      for (number = 1; (result = PushDBusValue(state, message)) > 0; number ++)
      {
        lua_rawseti(state, -2, number);
      }

      return (result < 0) ? result : ((result = sd_bus_message_exit_container(message)) < 0) ? result : 1;

    case 'v':
      if (((result = sd_bus_message_enter_container(message, 'v', contents)) < 0) ||
          ((result = PushDBusValue(state, message)) <= 0))
      {
        return (result < 0) ? result : -EBADMSG;
      }

      return ((result = sd_bus_message_exit_container(message)) < 0) ? result : 1;
  }

  // h and anything else unknown here
  return -ENOTSUP;
}

// The answer as the plugin gets it: a table of the values, an empty one for none, or nil, the name of the error, its text
int PushDBusAnswer(lua_State* state, sd_bus_message* reply, const JediDBusError& error)
{
  int top;
  int result;
  lua_Integer number;
  JediDBusError failure;

  if (reply == nullptr)
  {
    lua_pushnil(state);
    lua_pushstring(state, error.name.c_str());
    lua_pushstring(state, error.text.c_str());
    return 3;
  }

  top = lua_gettop(state);
  lua_newtable(state);

  for (number = 1; (result = PushDBusValue(state, reply)) > 0; number ++)
  {
    lua_rawseti(state, -2, number);
  }

  if (result < 0)
  {
    // A descriptor in it, as a rule
    lua_settop(state, top);
    SetDBusError(failure, -result, "the answer cannot be read by a plugin");
    return PushDBusAnswer(state, nullptr, failure);
  }

  // Three values either way, as fetch() gives: the values of the plugin go after them in a callback
  lua_pushnil(state);
  lua_pushnil(state);
  return 3;
}

// The bus of a request: system unless it says user
static int GetBusOption(lua_State* state, int index, const char* function, int& which)
{
  std::string bus;

  which = DBUS_SYSTEM;

  if (GetOption(state, index, "bus", bus))
  {
    if ((bus != "system") && (bus != "user"))
    {
      return Fail(state, "bus of %s is 'system' or 'user'", function);
    }

    which = (bus == "user") ? DBUS_USER : DBUS_SYSTEM;
  }

  return 0;
}

// dbus(request [, function, ...]): a method of a service on the bus, as fetch() asks a web service
int CallDBus(lua_State* state)
{
  int which;
  int number;
  int result;
  int arguments;
  bool later;
  size_t at;
  size_t length;
  double seconds;
  lua_Integer count;
  sd_bus* bus;
  sd_bus_message* message;
  sd_bus_message* reply;
  std::string service;
  std::string path;
  std::string interface;
  std::string method;
  std::string signature;
  std::string error;
  JediDBusError failure;
  Request item;

  later = (lua_type(state, 2) == LUA_TFUNCTION);

  if ((!later) &&
      (lua_type(state, 2) != LUA_TNONE) &&
      (lua_type(state, 2) != LUA_TNIL))
  {
    return Fail(state, "dbus() takes a function after the request");
  }

  if ((!later) &&
      ((phase & (PHASE_DO | PHASE_MENU)) == 0))
  {
    return Fail(state, "dbus() without a callback waits for the answer, it is used only by a handler making the menu, an action or a callback");
  }

  if (later &&
      ((phase & PHASE_WATCH) == 0))
  {
    return Fail(state, "dbus() with a callback is used by a plugin loaded, an action or a callback, not by a handler making the menu");
  }

  if ((lua_type(state, 1) != LUA_TTABLE) ||
      (!GetOption(state, 1, "service", service)) ||
      (!GetOption(state, 1, "path", path)) ||
      (!GetOption(state, 1, "method", method)))
  {
    return Fail(state, "dbus() takes a table of the call with service, path and method");
  }

  if (GetBusOption(state, 1, "dbus()", which) < 0)
  {
    return -1;
  }

  GetOption(state, 1, "interface", interface);
  GetOption(state, 1, "signature", signature);

  seconds = DBUS_TIMEOUT;
  lua_pushliteral(state, "timeout");
  lua_rawget(state, 1);

  if (!lua_isnil(state, -1))
  {
    // NaN fails every comparison: only a number above zero and up to a day passes
    if ((lua_type(state, -1) != LUA_TNUMBER) ||
        (!((seconds = lua_tonumber(state, -1)) > 0)) ||
        (seconds > DBUS_TIMEOUT_LIMIT))
    {
      return Fail(state, "timeout of dbus() is a number of seconds above zero, up to a day");
    }
  }

  lua_pop(state, 1);

  if (signature.size() > DBUS_SIGNATURE)
  {
    // D-Bus takes no longer one, and the measure goes as deep as the signature
    return Fail(state, "a signature of D-Bus has up to %d characters", DBUS_SIGNATURE);
  }

  for (at = 0; at < signature.size(); at += length)
  {
    if ((length = MeasureType(signature.c_str() + at)) == 0)
    {
      return Fail(state, "'%s' is not a signature of D-Bus", signature.c_str());
    }
  }

  lua_pushliteral(state, "arguments");
  lua_rawget(state, 1);

  if ((!lua_isnil(state, -1)) &&
      (lua_type(state, -1) != LUA_TTABLE))
  {
    return Fail(state, "arguments of dbus() is a table of the values of the signature");
  }

  count     = lua_istable(state, -1) ? (lua_Integer)lua_rawlen(state, -1) : 0;
  arguments = lua_gettop(state);

  if ((bus = GetPluginBus(which, failure)) == nullptr)
  {
    // The bus is not there: a refusal of the world
    return PushDBusAnswer(state, nullptr, failure);
  }

  if ((result = sd_bus_message_new_method_call(bus, &message, service.c_str(), path.c_str(), interface.empty() ? nullptr : interface.c_str(), method.c_str())) < 0)
  {
    return Fail(state, "dbus() takes the names D-Bus does: '%s', '%s', '%s', '%s'", service.c_str(), path.c_str(), interface.c_str(), method.c_str());
  }

  for (at = 0, number = 1; at < signature.size(); at += length, number ++)
  {
    length = MeasureType(signature.c_str() + at);

    if (number > count)
    {
      sd_bus_message_unref(message);
      return Fail(state, "argument %d of dbus() is missing, the signature '%s' has more", number, signature.c_str());
    }

    lua_rawgeti(state, arguments, number);

    if (!AppendValue(state, -1, signature.substr(at, length), message, error))
    {
      sd_bus_message_unref(message);
      return Fail(state, "argument %d of dbus(): %s", number, error.c_str());
    }

    lua_pop(state, 1);
  }

  if (count >= number)
  {
    sd_bus_message_unref(message);
    return Fail(state, "dbus() has more arguments than its signature '%s' takes", signature.c_str());
  }

  number = ++ serial;
  result = StartDBusCall(number, which, message, (uint64_t)(seconds * 1000000), failure);
  sd_bus_message_unref(message);

  if (!result)
  {
    return PushDBusAnswer(state, nullptr, failure);
  }

  if (later)
  {
    // The answer goes to the callback once no dialog is open
    item.callback       = KeepCallback(state, 2, 3);
    item.plugin         = plugin;
    invocations[number] = item;
    PushHandle(state, number, DBUS);
    return 1;
  }

  if (phase & PHASE_MENU)
  {
    // No dialog under a menu being made: its time is shared by all the handlers
    if (!WaitDBusCall(number, deadline))
    {
      CancelDBusCall(number);
      failure.name = SD_BUS_ERROR_NO_REPLY;
      failure.text = "the menu has no time left for the answer";
      return PushDBusAnswer(state, nullptr, failure);
    }
  }
  else if (RunSteps(Fit("Waiting for " + service, 46).c_str(), [number] (int64_t deadline) { return WaitDBusCall(number, deadline); }, 0, "Waiting") != STEPS_DONE)
  {
    CancelDBusCall(number);
    failure.name = "cancel";
    failure.text = "the call has been canceled";
    return PushDBusAnswer(state, nullptr, failure);
  }

  reply = nullptr;
  TakeDBusAnswer(number, &reply, failure);
  result = PushDBusAnswer(state, reply, failure);
  sd_bus_message_unref(reply);
  return result;
}

static void DropInvocation(lua_State* state, int number)
{
  std::map<int, Request>::iterator invocation;

  if ((invocation = invocations.find(number)) != invocations.end())
  {
    luaL_unref(state, LUA_REGISTRYINDEX, invocation->second.callback);
    invocations.erase(invocation);
  }

  CancelDBusCall(number);
}

int CancelInvocation(lua_State* state)
{
  DropInvocation(state, GetHandle(state));
  return 0;
}

// services { bus = ..., prefix = ... }: the names on the bus that have an owner, with the owner
int ListServices(lua_State* state)
{
  int which;
  lua_Integer number;
  std::string prefix;
  JediDBusError failure;
  std::vector<JediDBusService> list;

  if ((lua_type(state, 1) != LUA_TTABLE) &&
      (lua_type(state, 1) != LUA_TNONE) &&
      (lua_type(state, 1) != LUA_TNIL))
  {
    return Fail(state, "services() takes a table with bus and prefix, or nothing");
  }

  if (GetBusOption(state, 1, "services()", which) < 0)
  {
    return -1;
  }

  GetOption(state, 1, "prefix", prefix);

  if (!ListDBusServices(which, prefix, (phase & PHASE_MENU) ? deadline : (GetQueryMilliseconds() + DBUS_LIST_TIME), list, failure))
  {
    return PushDBusAnswer(state, nullptr, failure);
  }

  lua_createtable(state, list.size(), 0);
  number = 0;

  for (const JediDBusService& service : list)
  {
    lua_createtable(state, 0, 4);
    lua_pushstring(state, service.name.c_str());
    lua_setfield(state, -2, "name");

    if (service.pid != 0)
    {
      lua_pushinteger(state, service.pid);
      lua_setfield(state, -2, "pid");
      lua_pushinteger(state, service.uid);
      lua_setfield(state, -2, "uid");
    }

    if (!service.unit.empty())
    {
      lua_pushstring(state, service.unit.c_str());
      lua_setfield(state, -2, "unit");
    }

    lua_rawseti(state, -2, ++ number);
  }

  return 1;
}
