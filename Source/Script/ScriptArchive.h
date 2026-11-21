#ifndef SCRIPTARCHIVE_H
#define SCRIPTARCHIVE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <zip.h>

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

#include <string>

// Plugins of an archive of ZIP, the way LuaArchive of TetraPack loads its modules: init.lua at the root is the plugin,
// the other files of the archive are its modules, compiled at once into package.preload, so that the archive is closed
// once the plugin has been loaded

// A file of the archive compiled onto the stack: LUA_OK and the function, or an error and its message, as luaL_loadfile
// gives them; place names the archive in the messages, place:file:line. left, when given, is the bytes the files of the
// archive may yet unpack to, counted as they are read whatever the archive says of their sizes: past it the file fails
// and left is negative
int LoadArchiveFile(lua_State* state, zip_t* archive, zip_int64_t index, const std::string& place, zip_int64_t* left = nullptr);

// The modules of the archive into package.preload: path is a template as package.path takes it, "?.lua;?/init.lua", the
// ? the name of the module with its dots as slashes, the name prefix and the one of the file, bookmarks.base of base.lua
// by bookmarks. and ?.lua. The template is laid on the files the other way package.path is laid on a name: what a file
// has in place of the ? is the name of its module, its slashes as dots:
//
//   base.lua           bookmarks.base           by ?.lua
//   net/address.lua    bookmarks.net.address    by ?.lua
//   net/init.lua       bookmarks.net            by ?/init.lua
//
// A file is one module, of the pattern that leaves it the shortest name, so net/init.lua is not bookmarks.net.init as
// well; with both net.lua and net/init.lua there, the pattern first in the template gives the module, as package.path
// does. A file with a dot of its own in place of the ?, a.b.lua, is left out, and so is init.lua at the root, the plugin
// itself. A file that does not compile or cannot be read leaves a loader that tells why when the module is required
void LoadArchiveModules(lua_State* state, zip_t* archive, const char* path, const std::string& prefix, const std::string& place, zip_int64_t* left = nullptr);

#endif
