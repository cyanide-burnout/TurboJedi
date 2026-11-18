#ifndef SCRIPTPRIVATE_H
#define SCRIPTPRIVATE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// What the parts of the interface of Lua share: the state, the phase of a call, what it is made of, and the
// functions one part has of another. JediScript.h is the interface for the rest of TurboJedi

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TMenuItem
#define Uses_TKeys
#define Uses_TEvent
#include <tvision/tv.h>

#include "Version.h"
#include "JediScript.h"
#include "JediProcess.h"
#include "JediFetch.h"
#include "JediDBus.h"
#include "ActionDialogs.h"
#include "JediClipboard.h"
#include "JediSettings.h"
#include "JournalExpression.h"
#include "JournalView.h"
#include "JournalQuery.h"
#include "JournalWatch.h"
#include "ScriptNotation.h"
#include "Commands.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#if LUA_VERSION_NUM != 504
#error "Plugins are written for Lua 5.4"
#endif

#include <grp.h>
#include <ctype.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <map>
#include <set>
#include <memory>
#include <algorithm>

#define PHASE_ANY     -1

#define PHASE_LOAD    1  // The files run, handlers are registered
#define PHASE_MENU    2  // Handlers add items to the menu
#define PHASE_ACTION  4  // The chosen item runs
#define PHASE_EVENT   8   // A callback of poll(), timeout() or fetch() runs
#define PHASE_ENTRY   16  // A callback of watch() runs, with an entry or a gap
#define PHASE_RESTORE 32  // A function of on('restore') runs for a watch of a layout
#define PHASE_DO      (PHASE_ACTION | PHASE_EVENT | PHASE_ENTRY)                                // Dialogs and commands
#define PHASE_WATCH   (PHASE_LOAD | PHASE_ACTION | PHASE_EVENT | PHASE_ENTRY | PHASE_RESTORE)  // Watches and timers are set
#define PHASE_READ    (PHASE_MENU | PHASE_ACTION)                                               // The journal is read for the plugin
#define PHASE_MARK    (PHASE_ACTION | PHASE_ENTRY)                                              // Lines are marked in a window

#define FETCH_STEP         20      // Milliseconds it is waited for between the events of its dialog

#define DBUS      "TurboJedi.dbus"

// A descriptor a plugin watches, a timer it has set: the callback in the registry of Lua, the plugin for its errors; pending
// when it is due and waits for no dialog being open
struct Watch
{
  int handle;
  short events;
  short revents;
  int callback;
  std::string plugin;
  bool pending;
};

struct Timer
{
  int64_t due;
  int64_t period;  // Milliseconds of a periodic one, zero for one shot
  int callback;
  std::string plugin;
  bool pending;
};

// A request of fetch() with a callback, its transfer goes by the same number
struct Request
{
  int callback;
  std::string plugin;
};

// Errors of Lua leave a function by longjmp(), the system Lua is built as C: a function of the interface is a body that
// gives -1 with the message on the stack instead of raising it, Call raises it once the body has returned and no object
// of C++ is left there to destroy

typedef int (*Body)(lua_State* state);

extern lua_State* state;
extern int phase;
extern std::string plugin;  // Name of the plugin being loaded
extern int64_t deadline;    // Of the queries of the handlers making the menu, shared by all of them
extern std::map<int, Request> invocations;  // Calls of dbus() with a callback, their calls go by the same number
extern int serial;

int Fail(lua_State* state, const char* format, ...);
bool GetOption(lua_State* state, int index, const char* name, std::string& value);
int KeepCallback(lua_State* state, int function, int first);
void PushHandle(lua_State* state, int number, const char* kind);
int GetHandle(lua_State* state);
bool AppendValue(lua_State* state, int index, const std::string& type, sd_bus_message* message, std::string& error);
int PushDBusValue(lua_State* state, sd_bus_message* message);
int PushDBusAnswer(lua_State* state, sd_bus_message* reply, const JediDBusError& error);
int CallDBus(lua_State* state);
int CancelInvocation(lua_State* state);
int ListServices(lua_State* state);
std::string Fit(std::string title, int width);

template <Body body, int needed> static int Call(lua_State* state)
{
  int result;

  if ((needed != PHASE_ANY) &&
      ((phase & needed) == 0))
  {
    switch (needed)
    {
      case PHASE_LOAD:   return luaL_error(state, "handlers are registered only while the plugin is loaded");
      case PHASE_MENU:   return luaL_error(state, "items are added only by a handler while it makes the menu");
      case PHASE_ACTION: return luaL_error(state, "the window of the menu is there only for an action chosen from it");
      case PHASE_WATCH:  return luaL_error(state, "watches and timers are set by a plugin loaded, an action or a callback");
      case PHASE_READ:   return luaL_error(state, "the journal is read for a handler or an action");
      case PHASE_MARK:   return luaL_error(state, "lines are marked by an action or a callback of a watch");
      default:           return luaL_error(state, "dialogs and commands are used only by an action or a callback");
    }
  }

  result = body(state);
  return (result < 0) ? lua_error(state) : result;
}

#endif
