// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "ScriptArchive.h"

#include <string.h>

#define CHUNK_LENGTH  4096  // Bytes of a file Lua is given at once

// A file of the archive read by Lua as it compiles it, a piece at a time
struct ArchiveChunk
{
  int status;                 // Of the last read, negative when it has failed
  zip_file_t* file;           // Opened to be compiled, closed once Lua has compiled it
  char buffer[CHUNK_LENGTH];  // The piece Lua is given, on the stack of the one that compiles it
};

static const char* ReadArchiveChunk(lua_State* state, void* data, size_t* size)
{
  ArchiveChunk* chunk;
  zip_int64_t length;

  // The state is not needed to read
  (void)state;

  chunk  = static_cast<ArchiveChunk*>(data);
  length = zip_fread(chunk->file, chunk->buffer, CHUNK_LENGTH);

  if (length <= 0)
  {
    // The end of the file, or a failure to read it told after the chunk
    chunk->status = -(length < 0);
    *size         = 0;
    return nullptr;
  }

  *size = static_cast<size_t>(length);
  return chunk->buffer;
}

int LoadArchiveFile(lua_State* state, zip_t* archive, zip_int64_t index, const std::string& place)
{
  int result;
  const char* name;
  ArchiveChunk chunk;
  std::string source;

  name = zip_get_name(archive, index, 0);

  if ((chunk.file = zip_fopen_index(archive, index, 0)) == nullptr)
  {
    // A file the archive lists and cannot open, damaged or of a password: libzip tells it by the archive
    lua_pushfstring(state, "%s:%s: %s", place.c_str(), (name != nullptr) ? name : "?", zip_strerror(archive));
    return LUA_ERRFILE;
  }

  // Errors of Lua tell the archive and the file in it as the place, place:file:line
  chunk.status = 0;
  source       = "@" + place + ":" + ((name != nullptr) ? name : "?");
  result       = lua_load(state, ReadArchiveChunk, &chunk, source.c_str(), nullptr);

  if (chunk.status < 0)
  {
    // libzip tells a failure to read by the file, which goes with it: what Lua has made of a part of the file is not taken
    lua_pop(state, 1);
    lua_pushfstring(state, "%s: %s", source.c_str() + 1, zip_file_strerror(chunk.file));
    result = LUA_ERRFILE;
  }

  zip_fclose(chunk.file);
  return result;
}

// The loader of a module that has failed to compile: require() tells why, as for a module of the disk
static int RaiseArchiveError(lua_State* state)
{
  return luaL_error(state, "%s", lua_tostring(state, lua_upvalueindex(1)));
}

// The loader of a file of the archive on the stack: the file compiled, or a function that tells why it is not
static void LoadArchiveModule(lua_State* state, zip_t* archive, zip_int64_t index, const std::string& place, const std::string& module)
{
  if (LoadArchiveFile(state, archive, index, place) != LUA_OK)
  {
    lua_pushfstring(state, "error loading module '%s' from %s:%s:\n\t%s", module.c_str(), place.c_str(), zip_get_name(archive, index, 0), lua_tostring(state, -1));
    lua_remove(state, -2);
    lua_pushcclosure(state, RaiseArchiveError, 1);
  }
}

// The length of the name of the module of a file by a pattern, net/address of net/address.lua by ?.lua; 0 when the file
// is not of the pattern
static size_t MatchArchivePattern(const char* file, const char* start, const char* mark, const char* end)
{
  size_t length;
  size_t prefix;
  size_t suffix;

  length = strlen(file);
  prefix = mark - start;
  suffix = end - mark - 1;

  if ((length <= prefix + suffix) ||
      (strncmp(file, start, prefix) != 0) ||
      (strncmp(file + length - suffix, mark + 1, suffix) != 0))
  {
    return 0;
  }

  length -= prefix + suffix;

  if (memchr(file + prefix, '.', length) != nullptr)
  {
    // A dot of a name stands for a slash of the path, a dot of the path cannot be required
    return 0;
  }

  return length;
}

// The length of the shortest name of a file by the patterns of the template: a file is one module, net/init.lua is net
// by ?/init.lua and not net.init by ?.lua as well
static size_t MatchArchivePath(const char* file, const char* path)
{
  const char* end;
  const char* mark;
  const char* start;
  size_t length;
  size_t shortest;

  shortest = 0;

  for (start = path; *start != '\0'; start = end + (*end == ';'))
  {
    end  = strchrnul(start, ';');
    mark = static_cast<const char*>(memchr(start, '?', end - start));

    if ((mark != nullptr) &&
        ((length = MatchArchivePattern(file, start, mark, end)) != 0) &&
        ((shortest == 0) || (length < shortest)))
    {
      shortest = length;
    }
  }

  return shortest;
}

void LoadArchiveModules(lua_State* state, zip_t* archive, const char* path, const std::string& prefix, const std::string& place)
{
  const char* end;
  const char* mark;
  const char* file;
  const char* start;
  size_t length;
  zip_int64_t index;
  zip_int64_t count;
  std::string module;

  // package.preload, and the modules the template has found so far: a module of an earlier pattern is not taken again
  lua_getglobal(state, "package");
  lua_getfield(state, -1, "preload");
  lua_newtable(state);

  count = zip_get_num_entries(archive, 0);

  for (start = path; *start != '\0'; start = end + (*end == ';'))
  {
    end  = strchrnul(start, ';');
    mark = static_cast<const char*>(memchr(start, '?', end - start));

    // An empty pattern of ;; or one without a place for the name has no files
    for (index = 0; (mark != nullptr) && (index < count); index ++)
    {
      // A file of the pattern, unless another gives it a shorter name; init.lua at the root is the plugin
      if (((file   = zip_get_name(archive, index, 0)) != nullptr) &&
          (strcmp(file, "init.lua") != 0) &&
          ((length = MatchArchivePattern(file, start, mark, end)) != 0) &&
          (length == MatchArchivePath(file, path)))
      {
        module.assign(file + (mark - start), length);

        for (char& letter : module)
        {
          // net/address is net.address
          letter = (letter == '/') ? '.' : letter;
        }

        module = prefix + module;
        lua_getfield(state, -1, module.c_str());

        if (lua_isnil(state, -1))
        {
          // The file compiled now: the archive is closed once the plugin has been loaded
          LoadArchiveModule(state, archive, index, place, module);
          lua_setfield(state, -4, module.c_str());
          lua_pushboolean(state, 1);
          lua_setfield(state, -3, module.c_str());
        }

        lua_pop(state, 1);
      }
    }
  }

  lua_pop(state, 3);
}
