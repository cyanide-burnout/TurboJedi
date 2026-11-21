// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: on('invoke'), the methods of the plugins for Invoke() of the bus, what they do and their arguments

#include "ScriptPrivate.h"

std::map<std::pair<std::string, std::string>, PluginMethod> descriptions;  // Of on('invoke') by plugin and name
static const JournalFilter machine = { };     // The journal of this machine, of a method called without a window

// A text field of the options of on('invoke'): false and the error for one of another type, true for none or a text
static bool GetMethodText(lua_State* state, int index, const char* name, std::string& value, std::string& error)
{
  bool done;

  lua_pushstring(state, name);
  lua_rawget(state, index);
  done = lua_isnil(state, -1) || GetString(state, -1, value);
  lua_pop(state, 1);

  if (!done)
  {
    error = std::string(name) + " is a text";
  }

  return done;
}

// An argument of the options of on('invoke'), at the top of the stack under its name: its type, what it is, whether
// it has to be given
static bool ReadMethodArgument(lua_State* state, PluginArgument& argument, std::string& error)
{
  int index;
  int type;

  index = lua_gettop(state);

  if (lua_type(state, index) != LUA_TTABLE)
  {
    error = "argument " + argument.name + " is a table of type, description and required";
    return false;
  }

  if ((!GetMethodText(state, index, "type", argument.type, error)) ||
      (!GetMethodText(state, index, "description", argument.description, error)))
  {
    error = "argument " + argument.name + ": " + error;
    return false;
  }

  if ((argument.type != "s") && (argument.type != "u") && (argument.type != "i") && (argument.type != "t") &&
      (argument.type != "x") && (argument.type != "b") && (argument.type != "d") && (argument.type != "as") &&
      (argument.type != "a{sv}"))
  {
    error = "argument " + argument.name + " has type s, u, i, t, x, b, d, as or a{sv}, not '" + argument.type + "'";
    return false;
  }

  lua_pushliteral(state, "required");
  type              = lua_rawget(state, index);
  argument.required = lua_toboolean(state, -1);
  lua_pop(state, 1);

  if ((type != LUA_TNIL) &&
      (type != LUA_TBOOLEAN))
  {
    error = "required of argument " + argument.name + " is true or false";
    return false;
  }

  return true;
}

// The options of on('invoke') at the index: what the method does, its effect and its arguments. A method without them
// changes the system for all a caller knows
static bool ReadMethodOptions(lua_State* state, int index, PluginMethod& method, std::string& error)
{
  method.effect   = "system";
  method.declared = false;

  if (lua_isnone(state, index) ||
      lua_isnil(state, index))
  {
    return true;
  }

  if (lua_type(state, index) != LUA_TTABLE)
  {
    error = "the options are a table of description, effect and arguments";
    return false;
  }

  if ((!GetMethodText(state, index, "description", method.description, error)) ||
      (!GetMethodText(state, index, "effect", method.effect, error)))
  {
    return false;
  }

  if ((method.effect != "none") &&
      (method.effect != "screen") &&
      (method.effect != "system"))
  {
    error = "effect is 'none', 'screen' or 'system', not '" + method.effect + "'";
    return false;
  }

  lua_pushliteral(state, "arguments");

  if (lua_rawget(state, index) == LUA_TNIL)
  {
    lua_pop(state, 1);
    return true;
  }

  if (lua_type(state, -1) != LUA_TTABLE)
  {
    lua_pop(state, 1);
    error = "arguments is a table of the arguments by their names";
    return false;
  }

  // Its arguments are checked now, an empty table as well: such a method takes no values
  method.declared = true;

  for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1))
  {
    PluginArgument argument;

    if (lua_type(state, -2) != LUA_TSTRING)
    {
      lua_pop(state, 3);
      error = "the arguments are named by texts";
      return false;
    }

    argument.name     = lua_tostring(state, -2);
    argument.required = false;

    if (argument.name.empty() ||
        (argument.name[0] == '_'))
    {
      // The keys of TurboJedi, _window of the window a method runs in
      lua_pop(state, 3);
      error = "argument '" + argument.name + "': a name beginning with _ is of TurboJedi, an empty one of nothing";
      return false;
    }

    if (!ReadMethodArgument(state, argument, error))
    {
      lua_pop(state, 3);
      return false;
    }

    method.arguments.push_back(argument);
  }

  lua_pop(state, 1);

  // In the order of their names, a table of Lua has none of its own
  std::sort(method.arguments.begin(), method.arguments.end(), [] (const PluginArgument& one, const PluginArgument& other)
  {
    return one.name < other.name;
  });

  return true;
}

// Methods of a plugin that has failed to load: it offers nothing, as if it had none
void ForgetPluginMethods(const std::string& name)
{
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);
  lua_pushnil(state);
  lua_setfield(state, -2, name.c_str());
  lua_pop(state, 1);

  for (std::map<std::pair<std::string, std::string>, PluginMethod>::iterator item = descriptions.begin(); item != descriptions.end(); )
  {
    item = (item->first.first == name) ? descriptions.erase(item) : std::next(item);
  }
}

// on('invoke', name, func, options): func(values, context) for Invoke() of the bus, the table it returns is the
// answer; the options tell what it does
int OnInvoke(lua_State* state)
{
  std::string error;
  PluginMethod method;

  if ((lua_type(state, 1) != LUA_TSTRING) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('invoke') takes a name, a function and its options");
  }

  method.plugin = plugin;
  method.method = lua_tostring(state, 1);

  if (!ReadMethodOptions(state, 3, method, error))
  {
    return Fail(state, "on('invoke') of %s: %s", method.method.c_str(), error.c_str());
  }

  descriptions[{ plugin, method.method }] = method;
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);

  if (lua_getfield(state, -1, plugin.c_str()) != LUA_TTABLE)
  {
    // The first method of the plugin
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, -3, plugin.c_str());
  }

  lua_pushvalue(state, 2);
  lua_setfield(state, -2, lua_tostring(state, 1));
  lua_pop(state, 2);

  return 0;
}

std::vector<const PluginMethod*> ListPluginMethods()
{
  std::vector<const PluginMethod*> list;

  for (const std::pair<const std::pair<std::string, std::string>, PluginMethod>& item : descriptions)
  {
    list.push_back(&item.second);
  }

  return list;
}

const PluginMethod* FindPluginMethod(const std::string& name, const std::string& method)
{
  std::map<std::pair<std::string, std::string>, PluginMethod>::const_iterator item;

  item = descriptions.find({ name, method });
  return (item != descriptions.end()) ? &item->second : nullptr;
}

int InvokePluginMethod(const std::string& name, const std::string& method, sd_bus_message* call, sd_bus_message* reply, JournalView* view, int64_t deadline, std::string& error)
{
  int base;
  int result;
  bool done;
  int64_t late;

  if (!IsPluginIdle())
  {
    // The bus waits for it, this is not reached
    error = "Lua is busy";
    return PLUGIN_METHOD_FAILED;
  }

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);
  lua_getfield(state, LUA_REGISTRYINDEX, METHODS);

  if ((lua_getfield(state, -1, name.c_str()) != LUA_TTABLE) ||
      (lua_getfield(state, -1, method.c_str()) != LUA_TFUNCTION))
  {
    lua_settop(state, base);
    return PLUGIN_METHOD_NONE;
  }

  // Trace, the function, the values of the caller as a table, the context of the window
  lua_replace(state, base + 2);
  lua_settop(state, base + 2);

  if ((result = PushDBusValue(state, call)) <= 0)
  {
    error = std::string("the values cannot be read: ") + strerror((result < 0) ? -result : EBADMSG);
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  for (lua_pushnil(state); lua_next(state, base + 3) != 0; )
  {
    // The keys of TurboJedi are not of the method, the bus has taken them
    lua_pop(state, 1);

    if ((lua_type(state, -1) == LUA_TSTRING) &&
        (lua_tostring(state, -1)[0] == '_'))
    {
      lua_pushvalue(state, -1);
      lua_pushnil(state);
      lua_rawset(state, base + 3);
    }
  }

  if (view != nullptr) PushWindowContext(state, view);
  else                 PushContext(state, machine);

  // As an action of the window: its journal is the one queries, marks and watches take
  phase   = PHASE_ACTION;
  plugin  = name;
  origin  = view;
  basis   = (view != nullptr) ? &view->getFilter() : &machine;
  invoked = deadline;
  done    = (lua_pcall(state, 2, 1, base + 1) == LUA_OK);
  phase   = PHASE_IDLE;
  origin  = nullptr;
  basis   = nullptr;
  invoked = 0;

  if ((late = GetQueryMilliseconds() - deadline) > 0)
  {
    // Past the 30 seconds of the call: a caller that keeps to them has given up, the answer may go nowhere
    LogConsole(name, "method " + method + " has answered " + std::to_string((late + 999) / 1000) + " s past the 30 s of its call", true);
  }

  if (!done)
  {
    error = GetError(state);
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  if (lua_isnil(state, -1))
  {
    // No answer is an empty one
    lua_pop(state, 1);
    lua_newtable(state);
  }

  if (!AppendValue(state, lua_gettop(state), "a{sv}", reply, error))
  {
    error = "the answer is not a{sv}: " + error;
    lua_settop(state, base);
    return PLUGIN_METHOD_FAILED;
  }

  lua_settop(state, base);
  return PLUGIN_METHOD_DONE;
}
