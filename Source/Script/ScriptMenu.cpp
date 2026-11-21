// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: on('actions'), on('menu'), menu:add() and menu:sub(); the Actions menu of an entry and the Plugins
// menu of the menu bar

#include "ScriptPrivate.h"

#define ACTION_LIMIT       100  // Actions of a menu, the ones of its submenus among them
#define SUBMENU_DEPTH      3    // Submenus in a submenu

#define cmJournalAction    1200  // The first item of the menu, one per item

#define MENU_TIME          200     // Milliseconds the queries of all handlers of a menu may take together
#define MENU_SCAN          10000   // Entries they may look at together

// on('actions', func): func(entry, menu, context) when the Actions menu is made
int OnActions(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TFUNCTION)
  {
    return Fail(state, "on('actions') takes a function");
  }

  lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);
  lua_pushvalue(state, 1);
  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_getfield(state, LUA_REGISTRYINDEX, NAMES);
  lua_pushlstring(state, plugin.data(), plugin.size());
  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_pop(state, 2);

  return 0;
}

// on('menu', title, func, ...): an item of the Plugins menu of the menu bar, func(context, ...) when it is chosen; a
// title of a list, { 'Topics', 'New topic...' }, puts it into the submenus its first strings name
int OnMenu(lua_State* state)
{
  int top;
  lua_Integer count;

  top = lua_gettop(state);

  if (((lua_type(state, 1) != LUA_TSTRING) &&
       (lua_type(state, 1) != LUA_TTABLE)) ||
      (lua_type(state, 2) != LUA_TFUNCTION))
  {
    return Fail(state, "on('menu') takes a title and a function");
  }

  if (lua_type(state, 1) == LUA_TTABLE)
  {
    count = lua_rawlen(state, 1);

    if ((count < 1) ||
        (count > SUBMENU_DEPTH + 1))
    {
      return Fail(state, "the title of on('menu') is a list of 1 to %d strings, the submenus and the item", SUBMENU_DEPTH + 1);
    }

    for (lua_Integer number = 1; number <= count; number ++)
    {
      if ((lua_rawgeti(state, 1, number) != LUA_TSTRING) ||
          (lua_rawlen(state, -1) == 0))
      {
        return Fail(state, "the title of on('menu') is a list of strings, none of them empty");
      }

      lua_pop(state, 1);
    }
  }

  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  if (lua_rawlen(state, -1) >= PLUGIN_MENU_LIMIT)
  {
    return Fail(state, "the Plugins menu holds up to %d items", PLUGIN_MENU_LIMIT);
  }

  // The item keeps its values the way menu:add() does, nil in the middle included
  lua_createtable(state, top - 2, 5);

  if (lua_type(state, 1) == LUA_TTABLE)
  {
    // The path without the title, copied: the list of the plugin may change afterwards
    count = lua_rawlen(state, 1);
    lua_rawgeti(state, 1, count);
    lua_setfield(state, -2, "title");
    lua_createtable(state, count - 1, 0);

    for (lua_Integer number = 1; number < count; number ++)
    {
      lua_rawgeti(state, 1, number);
      lua_rawseti(state, -2, number);
    }

    lua_setfield(state, -2, "path");
  }
  else
  {
    lua_pushvalue(state, 1);
    lua_setfield(state, -2, "title");
  }

  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "action");
  lua_pushinteger(state, top - 2);
  lua_setfield(state, -2, "n");
  lua_pushlstring(state, plugin.data(), plugin.size());
  lua_setfield(state, -2, "plugin");

  for (int index = 3; index <= top; index ++)
  {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, index - 2);
  }

  lua_rawseti(state, -2, lua_rawlen(state, -2) + 1);
  lua_pop(state, 1);

  return 0;
}

int AddItem(lua_State* state)
{
  int index;
  int top;

  top = lua_gettop(state);

  if (lua_type(state, 1) != LUA_TTABLE)  return Fail(state, "menu:add() is called with a colon");
  if (lua_type(state, 2) != LUA_TSTRING) return Fail(state, "menu:add() takes a title");
  if (lua_type(state, 3) != LUA_TFUNCTION) return Fail(state, "menu:add() takes a function after the title");

  // The item keeps its arguments the way table.pack() does, nil in the middle included
  lua_createtable(state, top - 3, 3);
  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "title");
  lua_pushvalue(state, 3);
  lua_setfield(state, -2, "action");
  lua_pushinteger(state, top - 3);
  lua_setfield(state, -2, "n");

  for (index = 4; index <= top; index ++)
  {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, index - 3);
  }

  lua_rawseti(state, 1, lua_rawlen(state, 1) + 1);
  return 0;
}

int AddSubmenu(lua_State* state)
{
  if (lua_type(state, 1) != LUA_TTABLE)  return Fail(state, "menu:sub() is called with a colon");
  if (lua_type(state, 2) != LUA_TSTRING) return Fail(state, "menu:sub() takes a title");

  // The item opens a menu of its own, items are added to it as to the menu; one with none is not shown
  lua_createtable(state, 0, 2);
  lua_pushvalue(state, 2);
  lua_setfield(state, -2, "title");
  lua_newtable(state);
  luaL_setmetatable(state, MENU);
  lua_pushvalue(state, -1);
  lua_setfield(state, -3, "sub");
  lua_insert(state, -2);
  lua_rawseti(state, 1, lua_rawlen(state, 1) + 1);
  return 1;
}

// The items the handlers make for an entry of a window: on the stack from the base on Trace, the entry, the context, the
// items of all handlers, the handlers and their names; the errors of the handlers that fail go to the errors
static void CollectActions(JournalView* view, const std::vector<std::pair<std::string, std::string>>& fields, std::vector<std::string>& errors)
{
  int base;
  int count;
  int index;
  std::string title;

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);                   // base + 1
  PushEntry(state, fields);                          // base + 2
  PushWindowContext(state, view);                    // base + 3
  lua_newtable(state);                               // base + 4, items of every handler
  lua_getfield(state, LUA_REGISTRYINDEX, HANDLERS);  // base + 5
  lua_getfield(state, LUA_REGISTRYINDEX, NAMES);     // base + 6

  phase    = PHASE_MENU;
  origin   = view;
  deadline = GetQueryMilliseconds() + MENU_TIME;
  budget   = MENU_SCAN;
  count    = lua_rawlen(state, base + 5);

  for (index = 1; index <= count; index ++)
  {
    // handler(entry, menu, context), the items of a handler that fails go along with it
    lua_rawgeti(state, base + 5, index);
    lua_newtable(state);
    luaL_setmetatable(state, MENU);
    lua_pushvalue(state, -1);
    lua_insert(state, -3);
    lua_pushvalue(state, base + 2);
    lua_insert(state, -2);
    lua_pushvalue(state, base + 3);

    if (lua_pcall(state, 3, 0, base + 1) != LUA_OK)
    {
      title = GetError(state);
      lua_rawgeti(state, base + 6, index);
      errors.push_back(std::string(lua_tostring(state, -1)) + ": " + title);
      lua_settop(state, base + 6);
      continue;
    }

    for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, -1); number ++)
    {
      // Every item tells its plugin, two plugins may offer items of the same title
      lua_rawgeti(state, -1, number);
      lua_rawgeti(state, base + 6, index);
      lua_setfield(state, -2, "plugin");
      lua_rawseti(state, base + 4, lua_rawlen(state, base + 4) + 1);
    }

    lua_settop(state, base + 6);
  }

  phase  = PHASE_IDLE;
  origin = nullptr;
}

// The menu with one line that tells why there is nothing to choose, instead of a dialog with OK: Esc, Enter, a click or
// the key that has opened it closes it, as any menu. The line is an item as any other, readable on the bar of the
// cursor, its command does nothing
static void ShowActionNote(TPoint where, const std::string& text)
{
  popupMenu(where, *new TMenuItem(Fit(text, TProgram::deskTop->size.x - 8).c_str(), cmJournalAction, kbNoKey, hcNoContext), nullptr);
}

// An item of the Actions menu as it is shown: an action, with its index in the flat list of the actions from 1, or a
// submenu with its items
struct ActionNode
{
  std::string title;
  std::string plugin;
  int leaf;
  std::vector<ActionNode> children;
};

// The items of the list at the index of the stack: the actions go into the flat list at the index flat, each with its
// plugin, up to ACTION_LIMIT of them; a submenu without an action in it, or deeper than SUBMENU_DEPTH, is left out
static void WalkActions(int list, int flat, const std::string& owner, int depth, std::vector<ActionNode>& nodes)
{
  int top;
  ActionNode node;

  for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, list); number ++)
  {
    if (lua_rawlen(state, flat) >= ACTION_LIMIT)
    {
      break;
    }

    top = lua_gettop(state);
    lua_rawgeti(state, list, number);
    lua_getfield(state, top + 1, "title");
    lua_getfield(state, top + 1, "plugin");

    node          = ActionNode {};
    node.title    = lua_tostring(state, top + 2);
    node.plugin   = owner.empty() ? lua_tostring(state, top + 3) : owner;
    node.leaf     = 0;

    if (lua_getfield(state, top + 1, "sub") == LUA_TTABLE)
    {
      if (depth < SUBMENU_DEPTH)
      {
        WalkActions(top + 4, flat, node.plugin, depth + 1, node.children);
      }

      if (!node.children.empty())
      {
        nodes.push_back(node);
      }
    }
    else
    {
      // Run as an action of its plugin, the one of the item on top
      lua_pushstring(state, node.plugin.c_str());
      lua_setfield(state, top + 1, "plugin");
      lua_pushvalue(state, top + 1);
      lua_rawseti(state, flat, lua_rawlen(state, flat) + 1);
      node.leaf = lua_rawlen(state, flat);
      nodes.push_back(node);
    }

    lua_settop(state, top);
  }
}

// The items of the menu, the command of an action the first one and its index, a line between the items of two plugins
// on top, the plugin standing where a menu has the key of an item; in a submenu the plugin is the one of the item it
// opens from. A box of Turbo Vision shows ► in the place of the plugin of an item with a submenu, so on top the plugin
// goes into its title, its end where the plugins of the other items end, and theirs leave room for ► after them
static TMenuItem* MakeActionItems(const std::vector<ActionNode>& nodes, int width, bool top, ushort command)
{
  int room;
  TMenuItem* first;
  TMenuItem* last;
  TMenuItem* item;
  std::string title;
  std::string previous;
  std::vector<std::string> titles;

  first = nullptr;
  last  = nullptr;
  room  = 10;  // The width of a box, as Turbo Vision makes it: the title of an item after 3 columns, its key or ► and 3 more

  for (const ActionNode& node : nodes)
  {
    titles.push_back(Fit(node.title, width - (top ? strwidth(node.plugin) + 5 : (node.leaf > 0) ? 0 : 3)));

    if (top)
    {
      // An item: its title, 2 columns, its plugin and 3 more, and 6 of the box; one with a submenu: its title, at
      // least 2 columns, its plugin and 9 of the box
      room = std::max<int>(room, cstrlen(titles.back()) + strwidth(node.plugin) + 11);
    }
  }

  for (size_t index = 0; index < nodes.size(); index ++)
  {
    const ActionNode& node = nodes[index];

    if ((top) &&
        (last != nullptr) &&
        (node.plugin != previous))
    {
      last->next = &newLine();
      last       = last->next;
    }

    if (node.leaf > 0)
    {
      // The plugin 3 columns short of where a menu ends a key, where ► stands
      title = top ? node.plugin + "   " : std::string();
      item  = new TMenuItem(titles[index].c_str(), command + node.leaf - 1, kbNoKey, hcNoContext, top ? title.c_str() : nullptr);
    }
    else
    {
      title = titles[index];

      if (top)
      {
        // Padded to end its plugin 9 columns short of the end of the box
        title += std::string(std::max<int>(2, room - 9 - cstrlen(titles[index]) - strwidth(node.plugin)), ' ') + node.plugin;
      }

      item = new TMenuItem(title.c_str(), kbNoKey, new TMenu(*MakeActionItems(node.children, width, false, command)), hcNoContext);
    }

    first    = (first == nullptr) ? item : first;
    previous = node.plugin;

    if (last != nullptr)
    {
      last->next = item;
    }

    last = item;
  }

  return first;
}

void ShowActions(JournalView* view, const std::string& cursor, TPoint where, bool keyboard)
{
  const JournalFilter& filter = view->getFilter();
  int base;
  int count;
  int index;
  int arguments;
  ushort command;
  std::string name;
  std::vector<ActionNode> nodes;
  std::vector<std::string> errors;
  std::vector<std::pair<std::string, std::string>> fields;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice: an action runs, its dialog keeps the windows going
    return;
  }

  if ((cursor.empty()) ||
      ((fields = ReadJournalFields(filter, cursor)).empty()))
  {
    // Rotated away meanwhile, or the journal cannot be opened
    ShowActionNote(where, DescribeJournalLoss(filter));
    return;
  }

  base = lua_gettop(state);
  CollectActions(view, fields, errors);
  lua_newtable(state);  // base + 7, the actions in the order of the menu, the ones of submenus among them
  WalkActions(base + 4, base + 7, "", 0, nodes);
  count = lua_rawlen(state, base + 7);

  if (!errors.empty())
  {
    ShowErrors(errors);
  }

  if (count == 0)
  {
    if (errors.empty())
    {
      ShowActionNote(where, "No actions for this entry");
    }

    lua_settop(state, base);
    return;
  }

  command = ShowActionMenu(where, *MakeActionItems(nodes, TProgram::deskTop->size.x - 8, true, cmJournalAction), "", keyboard);

  if ((command >= cmJournalAction) &&
      (command < cmJournalAction + count))
  {
    // action(...) with the arguments given to menu:add()
    lua_rawgeti(state, base + 7, command - cmJournalAction + 1);
    lua_getfield(state, -1, "plugin");
    name = lua_tostring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "action");
    lua_getfield(state, -2, "n");
    arguments = lua_tointeger(state, -1);
    lua_pop(state, 1);

    for (index = 1; index <= arguments; index ++)
    {
      lua_rawgeti(state, -1 - index, index);
    }

    phase  = PHASE_ACTION;
    origin = view;
    plugin = name;  // Watches and timers the action sets are of its plugin

    if (lua_pcall(state, arguments, 0, base + 1) != LUA_OK)
    {
      phase  = PHASE_IDLE;
      origin = nullptr;
      ShowErrors({ name + ": " + GetError(state) });
    }

    phase  = PHASE_IDLE;
    origin = nullptr;
  }

  lua_settop(state, base);
}

TMenuItem* MakePluginMenu(int width)
{
  int top;
  size_t place;
  std::string title;
  std::string name;
  std::vector<ActionNode> nodes;
  std::vector<ActionNode>* level;

  if (state == nullptr)
  {
    return nullptr;
  }

  top = lua_gettop(state);
  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);

  for (lua_Integer index = 1; index <= (lua_Integer)lua_rawlen(state, top + 1); index ++)
  {
    lua_rawgeti(state, top + 1, index);
    lua_getfield(state, top + 2, "plugin");
    name  = lua_tostring(state, -1);
    level = &nodes;

    if (lua_getfield(state, top + 2, "path") == LUA_TTABLE)
    {
      for (lua_Integer number = 1; number <= (lua_Integer)lua_rawlen(state, top + 4); number ++)
      {
        // Into the submenu of that title of the plugin, made by its first item
        lua_rawgeti(state, top + 4, number);
        title = lua_tostring(state, -1);
        lua_pop(state, 1);

        for (place = 0; place < level->size(); place ++)
        {
          if (((*level)[place].leaf == 0) &&
              ((*level)[place].plugin == name) &&
              ((*level)[place].title == title))
          {
            break;
          }
        }

        if (place == level->size())
        {
          level->push_back(ActionNode { title, name, 0, { } });
        }

        level = &(*level)[place].children;
      }
    }

    lua_getfield(state, top + 2, "title");
    level->push_back(ActionNode { lua_tostring(state, -1), name, (int)index, { } });
    lua_settop(state, top + 1);
  }

  lua_settop(state, top);
  return nodes.empty() ? nullptr : MakeActionItems(nodes, width, true, cmJournalMenu);
}

void RunPluginMenu(int index, JournalView* view, const JournalFilter& filter)
{
  int base;
  int arguments;
  std::string name;

  if ((state == nullptr) ||
      (phase != PHASE_IDLE))
  {
    // Lua is not entered twice: an action runs, its dialog keeps the windows going
    return;
  }

  base = lua_gettop(state);

  lua_pushcfunction(state, Trace);                // base + 1
  lua_getfield(state, LUA_REGISTRYINDEX, MENUS);  // base + 2

  if (lua_rawgeti(state, base + 2, index + 1) != LUA_TTABLE)
  {
    lua_settop(state, base);
    return;
  }

  // func(context, ...) with the values given to on('menu') after it
  lua_getfield(state, base + 3, "plugin");
  name = lua_tostring(state, -1);
  lua_pop(state, 1);
  lua_getfield(state, base + 3, "action");

  if (view != nullptr) PushWindowContext(state, view);
  else                 PushContext(state, filter);

  lua_getfield(state, base + 3, "n");
  arguments = lua_tointeger(state, -1);
  lua_pop(state, 1);

  for (int number = 1; number <= arguments; number ++)
  {
    lua_rawgeti(state, base + 3, number);
  }

  phase  = PHASE_ACTION;
  origin = view;
  basis  = &filter;
  plugin = name;  // Watches and timers the action sets are of its plugin

  if (lua_pcall(state, arguments + 1, 0, base + 1) != LUA_OK)
  {
    phase  = PHASE_IDLE;
    origin = nullptr;
    ShowErrors({ name + ": " + GetError(state) });
  }

  phase  = PHASE_IDLE;
  origin = nullptr;
  basis  = nullptr;  // The filter is of the caller, gone once it returns
  lua_settop(state, base);
}

// The actions of the nodes and of their submenus, each with the titles of the submenus above it
static void FlattenActions(const std::vector<ActionNode>& nodes, std::vector<std::string>& path, std::vector<PluginAction>& actions)
{
  for (const ActionNode& node : nodes)
  {
    if (node.leaf > 0)
    {
      actions.push_back({ node.title, node.plugin, path });
      continue;
    }

    path.push_back(node.title);
    FlattenActions(node.children, path, actions);
    path.pop_back();
  }
}

bool ListPluginActions(JournalView* view, const std::string& cursor, std::vector<PluginAction>& actions)
{
  int base;
  std::vector<ActionNode> nodes;
  std::vector<std::string> path;
  std::vector<std::string> errors;
  std::vector<std::pair<std::string, std::string>> fields;

  actions.clear();

  if ((!IsPluginIdle()) ||
      (cursor.empty()) ||
      ((fields = ReadJournalFields(view->getFilter(), cursor)).empty()))
  {
    // Lua runs something else, or the entry is not in the journal anymore
    return false;
  }

  // As the menu is made, nothing is run: a handler only adds items, the errors of a failing one stay with the user
  base = lua_gettop(state);
  CollectActions(view, fields, errors);
  lua_newtable(state);
  WalkActions(base + 4, base + 7, "", 0, nodes);
  FlattenActions(nodes, path, actions);
  lua_settop(state, base);
  return true;
}
