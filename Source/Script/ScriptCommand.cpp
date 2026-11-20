// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Interface of Lua: execute(), log(), append(), regex(), matches(), which(), quote(), settings(), sudo() and date()

#include "ScriptPrivate.h"

#define CAPTURE_TIMEOUT    30   // Seconds execute() of mode capture waits by default
#define EXPRESSION_LIMIT   64   // Compiled patterns kept for the next calls

std::map<std::string, JournalExpression> expressions;

static JournalExpression* GetExpression(lua_State* state, int index, int options, std::string& error)
{
  bool caseless;
  std::string key;
  std::string pattern;
  std::string flags;
  JournalExpression expression;
  std::map<std::string, JournalExpression>::iterator place;

  if (!GetString(state, index, pattern))
  {
    error = "the pattern is not a string";
    return nullptr;
  }

  GetString(state, options, flags);

  caseless = (flags.find('i') != std::string::npos);
  key      = (caseless ? "i:" : "-:") + pattern;
  place    = expressions.find(key);

  if (place != expressions.end())
  {
    return &place->second;
  }

  if (!expression.compile(pattern, caseless, &error))
  {
    return nullptr;
  }

  if (expressions.size() >= EXPRESSION_LIMIT)
  {
    // Patterns are few, made anew they cost a compilation
    expressions.clear();
  }

  return &expressions.emplace(key, std::move(expression)).first->second;
}

// Groups of a match: [0] the match, [1] and on the groups, named ones under their names as well
static void PushMatch(lua_State* state, const std::string& text, const std::vector<std::pair<size_t, size_t>>& groups, const std::vector<std::string>& names)
{
  size_t index;

  lua_createtable(state, groups.size(), names.size());

  for (index = 0; index < groups.size(); index ++)
  {
    if (groups[index].first == std::string::npos)
    {
      // Has not taken part
      continue;
    }

    lua_pushlstring(state, text.data() + groups[index].first, groups[index].second - groups[index].first);

    if ((index < names.size()) &&
        (!names[index].empty()))
    {
      lua_pushvalue(state, -1);
      lua_setfield(state, -3, names[index].c_str());
    }

    lua_rawseti(state, -2, index);
  }
}

// log(text, options): a line of the console with the plugin; { problem = true } leaves it unread
int LogText(lua_State* state)
{
  std::string text;

  if (!GetString(state, 1, text))
  {
    return Fail(state, "log() takes a text");
  }

  LogConsole(plugin.empty() ? std::string("lua") : plugin, text, GetFlag(state, 2, "problem"));
  return 0;
}

// append(path, text): the text at the end of a file of the process alone, made with 0600; nil and why when it is not
// one: a link, not a file, of another owner, or readable or writable by others. What a plugin keeps of the journal stays
// with its user, root included
int AppendText(lua_State* state)
{
  int handle;
  size_t length;
  ssize_t written;
  const char* text;
  std::string path;
  std::string reason;
  struct stat status;

  if ((!GetString(state, 1, path)) ||
      (lua_type(state, 2) != LUA_TSTRING))
  {
    return Fail(state, "append() takes a path and a text");
  }

  text = lua_tolstring(state, 2, &length);

  if ((handle = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)) < 0)
  {
    reason = strerror(errno);
  }
  else if ((fstat(handle, &status) < 0) ||
           (!S_ISREG(status.st_mode)))
  {
    reason = "not a regular file";
  }
  else if (status.st_uid != geteuid())
  {
    reason = "of another owner";
  }
  else if ((status.st_mode & 077) != 0)
  {
    reason = "open to others, chmod 600 it";
  }
  else
  {
    for (size_t done = 0; done < length; )
    {
      if (((written = write(handle, text + done, length - done)) < 0) &&
          (errno == EINTR))
      {
        // A signal has come before anything has been written
        continue;
      }

      if (written <= 0)
      {
        // Nothing written would be written forever
        reason = (written < 0) ? strerror(errno) : "nothing could be written";
        break;
      }

      done += written;
    }
  }

  if (handle >= 0)
  {
    close(handle);
  }

  if (!reason.empty())
  {
    lua_pushnil(state);
    lua_pushstring(state, reason.c_str());
    return 2;
  }

  lua_pushboolean(state, 1);
  return 1;
}

static void PushOutcome(lua_State* state, const JediOutcome& outcome)
{
  lua_pushstring(state, outcome.how.c_str());

  if (outcome.how == "error") lua_pushlstring(state, outcome.text.data(), outcome.text.size());
  else if (outcome.how == "cancel") lua_pushnil(state);
  else lua_pushinteger(state, outcome.code);
}

// execute(argv, options): mode tells where the output goes and whether the plugin waits, how and code come first in
// every one, 'error' and the reason when the command has not started
int Execute(lua_State* state)
{
  int timeout;
  std::string mode;
  std::string output;
  std::string errors;
  std::string failure;
  JediOutcome outcome;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "execute() takes a list of the command and its arguments");
  }

  if ((lua_gettop(state) >= 2) &&
      (!lua_isnil(state, 2)) &&
      (lua_type(state, 2) != LUA_TTABLE))
  {
    return Fail(state, "execute() takes a table of options after the command");
  }

  if (!GetOption(state, 2, "mode", mode))
  {
    // Nothing shown, nothing waited for
    mode = "background";
  }

  if ((mode == "background") ||
      (mode == "window"))
  {
    // Started and left: background tells only an end other than exit 0, window shows the output as it comes
    if (!((mode == "background") ? RunBackground(arguments, failure) : OpenOutput(arguments, failure)))
    {
      lua_pushliteral(state, "error");
      lua_pushlstring(state, failure.data(), failure.size());
      return 2;
    }

    lua_pushliteral(state, "started");
    return 1;
  }

  if (mode == "terminal")
  {
    PushOutcome(state, RunTerminal(arguments));
    return 2;
  }

  if (mode != "capture")
  {
    return Fail(state, "mode of execute() is background, window, terminal or capture");
  }

  timeout = CAPTURE_TIMEOUT;

  if (lua_type(state, 2) == LUA_TTABLE)
  {
    lua_pushliteral(state, "timeout");

    if (lua_rawget(state, 2) == LUA_TNUMBER)
    {
      // Seconds, zero waits for as long as it takes
      timeout = std::max<int>(0, lua_tointeger(state, -1));
    }

    lua_pop(state, 1);
  }

  outcome = CaptureCommand(arguments, timeout, output, errors);

  PushOutcome(state, outcome);

  if (outcome.how == "error") lua_pushnil(state);
  else                        lua_pushlstring(state, output.data(), output.size());

  if (errors.empty()) lua_pushnil(state);
  else                lua_pushlstring(state, errors.data(), errors.size());

  return 4;
}

int Quote(lua_State* state)
{
  std::string line;
  std::vector<std::string> arguments;

  if (!GetArguments(state, 1, arguments))
  {
    return Fail(state, "quote() takes a list of the command and its arguments");
  }

  line = QuoteArguments(arguments);
  lua_pushlstring(state, line.data(), line.size());
  return 1;
}

int Match(lua_State* state)
{
  std::string text;
  std::string error;
  JournalExpression* expression;
  std::vector<std::pair<size_t, size_t>> groups;

  if (!GetString(state, 1, text))
  {
    // A field the entry does not have
    lua_pushnil(state);
    return 1;
  }

  if ((expression = GetExpression(state, 2, 3, error)) == nullptr)
  {
    return Fail(state, "regex(): %s", error.c_str());
  }

  if (!expression->search(text, 0, groups))
  {
    lua_pushnil(state);
    return 1;
  }

  PushMatch(state, text, groups, expression->names());
  return 1;
}

static int NextMatch(lua_State* state)
{
  size_t offset;
  std::string text;
  std::string error;
  JournalExpression* expression;
  std::vector<std::pair<size_t, size_t>> groups;

  // Up values: the text, the pattern, the flags and where the search goes on
  if ((!GetString(state, lua_upvalueindex(1), text)) ||
      (lua_type(state, lua_upvalueindex(4)) != LUA_TNUMBER))
  {
    lua_pushnil(state);
    return 1;
  }

  offset = lua_tointeger(state, lua_upvalueindex(4));

  if ((expression = GetExpression(state, lua_upvalueindex(2), lua_upvalueindex(3), error)) == nullptr)
  {
    return Fail(state, "matches(): %s", error.c_str());
  }

  if ((offset > text.size()) ||
      (!expression->search(text, offset, groups)))
  {
    // Over, the next call says so again
    lua_pushnil(state);
    lua_replace(state, lua_upvalueindex(4));
    lua_pushnil(state);
    return 1;
  }

  lua_pushinteger(state, JournalExpression::advance(text, groups[0].first, groups[0].second));
  lua_replace(state, lua_upvalueindex(4));
  PushMatch(state, text, groups, expression->names());
  return 1;
}

int Matches(lua_State* state)
{
  std::string error;

  if (GetExpression(state, 2, 3, error) == nullptr)
  {
    return Fail(state, "matches(): %s", error.c_str());
  }

  // Without a text the loop goes round no time
  lua_pushvalue(state, 1);
  lua_pushvalue(state, 2);
  lua_pushvalue(state, 3);
  lua_pushinteger(state, 0);
  lua_pushcclosure(state, Call<NextMatch, PHASE_ANY>, 4);
  return 1;
}

int Which(lua_State* state)
{
  size_t start;
  size_t end;
  std::string name;
  std::string path;
  std::string list;
  struct stat status;

  if ((!GetString(state, 1, name)) ||
      (name.empty()))
  {
    return Fail(state, "which() takes the name of a program");
  }

  list = (getenv("PATH") != nullptr) ? getenv("PATH") : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

  for (start = 0; start <= list.size(); start = end + 1)
  {
    end  = std::min(list.find(':', start), list.size());
    path = (name.find('/') != std::string::npos) ? name : (((end > start) ? list.substr(start, end - start) : std::string(".")) + "/" + name);

    if ((stat(path.c_str(), &status) == 0) &&
        (S_ISREG(status.st_mode)) &&
        (access(path.c_str(), X_OK) == 0))
    {
      lua_pushlstring(state, path.data(), path.size());
      return 1;
    }

    if (name.find('/') != std::string::npos)
    {
      // A path is not looked for
      break;
    }
  }

  lua_pushnil(state);
  return 1;
}

// Time

// utc(): whether the user wants the times of the watches in UTC
int GetUniversal(lua_State* state)
{
  lua_pushboolean(state, universal);
  return 1;
}

// settings(name, text, options): a file of the plugin among the ones TurboJedi writes by itself, ~/.config/turbojedi/name,
// with { data = true } ~/.local/share/turbojedi/name, under sudo the one of the user of sudo, read and written as that
// user. Without the text, its text or nil; with it, the file replaced whole, true or nil and why
int Settings(lua_State* state)
{
  FILE* file;
  size_t length;
  std::string name;
  std::string text;
  std::string path;
  char buffer[4096];

  if ((!GetString(state, 1, name)) ||
      (name.empty()) ||
      (name[0] == '.') ||
      (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos))
  {
    return Fail(state, "settings() takes the name of a file, letters, digits, dots, dashes and underscores");
  }

  if ((!lua_isnoneornil(state, 2)) &&
      (!GetString(state, 2, text)))
  {
    return Fail(state, "settings() takes a text after the name");
  }

  if ((!lua_isnoneornil(state, 3)) &&
      (lua_type(state, 3) != LUA_TTABLE))
  {
    return Fail(state, "settings() takes a table of options after the text");
  }

  path = GetFlag(state, 3, "data") ? GetDataPath(name) : GetSettingsPath(name);

  if (lua_isnoneornil(state, 2))
  {
    if ((path.empty()) ||
        ((file = OpenSettings(path)) == nullptr))
    {
      lua_pushnil(state);
      return 1;
    }

    while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      text.append(buffer, length);
    }

    fclose(file);
    lua_pushlstring(state, text.data(), text.size());
    return 1;
  }

  // { private = true }: the file of the user alone, 0600, a base of the bookmarks for one
  if (!SaveSettings(path, text, GetFlag(state, 3, "private") ? 0600 : 0644))
  {
    lua_pushnil(state);
    lua_pushstring(state, path.empty() ? "there is no directory of settings, HOME is not set" : (path + " cannot be written").c_str());
    return 2;
  }

  lua_pushboolean(state, true);
  return 1;
}

// where(name, options): the path of the file settings() reads and writes by that name and options, under sudo the one of
// the user of sudo, nil without a directory; for a plugin that keeps a file of its own another way, a base of SQLite
int Where(lua_State* state)
{
  std::string name;
  std::string path;

  if ((!GetString(state, 1, name)) ||
      (name.empty()) ||
      (name[0] == '.') ||
      (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos))
  {
    return Fail(state, "where() takes the name of a file, letters, digits, dots, dashes and underscores");
  }

  if ((!lua_isnoneornil(state, 2)) &&
      (lua_type(state, 2) != LUA_TTABLE))
  {
    return Fail(state, "where() takes a table of options after the name");
  }

  path = GetFlag(state, 2, "data") ? GetDataPath(name) : GetSettingsPath(name);

  if (path.empty()) lua_pushnil(state);
  else              lua_pushlstring(state, path.data(), path.size());

  return 1;
}

// sudo(): the user who has run TurboJedi as root by sudo, { name, uid, home }, nil otherwise; the system tells the home,
// the passwd file or a directory service, for a plugin to read a file of settings of that user
int GetSudoUser(lua_State* state)
{
  char* end;
  char buffer[4096];
  const char* value;
  unsigned long uid;
  struct passwd entry;
  struct passwd* user;

  value = getenv("SUDO_UID");

  if ((getuid() != 0) ||
      (value == nullptr) ||
      (*value == '\0') ||
      (strspn(value, "0123456789") != strlen(value)))
  {
    lua_pushnil(state);
    return 1;
  }

  errno = 0;
  uid   = strtoul(value, &end, 10);

  if ((errno != 0) ||
      (uid == 0) ||
      (uid >= (unsigned long)(uid_t)-1) ||
      (getpwuid_r((uid_t)uid, &entry, buffer, sizeof(buffer), &user) != 0) ||
      (user == nullptr))
  {
    lua_pushnil(state);
    return 1;
  }

  lua_createtable(state, 0, 3);
  lua_pushstring(state, user->pw_name);
  lua_setfield(state, -2, "name");
  lua_pushinteger(state, user->pw_uid);
  lua_setfield(state, -2, "uid");
  lua_pushstring(state, user->pw_dir);
  lua_setfield(state, -2, "home");
  return 1;
}

// date(format, time): os.date() in the zone the user has chosen for the watches, ! in front of the format forces UTC
int FormatDate(lua_State* state)
{
  int whole;
  bool utc;
  time_t moment;
  struct tm parts;
  std::string format;
  char buffer[512];

  format = "%c";

  if ((!lua_isnoneornil(state, 1)) &&
      (!GetString(state, 1, format)))
  {
    return Fail(state, "date() takes a format of strftime");
  }

  utc    = universal || ((!format.empty()) && (format[0] == '!'));
  format = ((!format.empty()) && (format[0] == '!')) ? format.substr(1) : format;
  moment = time(nullptr);

  if (!format.empty() && (format[0] == '*'))
  {
    return Fail(state, "date() takes a format of strftime, os.date() makes the table of *t");
  }

  if (!lua_isnoneornil(state, 2))
  {
    moment = lua_tointegerx(state, 2, &whole);

    if (!whole)
    {
      return Fail(state, "date() takes whole seconds after the format");
    }
  }

  if (((utc ? gmtime_r(&moment, &parts) : localtime_r(&moment, &parts)) == nullptr) ||
      ((strftime(buffer, sizeof(buffer), format.c_str(), &parts) == 0) && (!format.empty())))
  {
    return Fail(state, "date() cannot make the time of %s", format.c_str());
  }

  lua_pushstring(state, format.empty() ? "" : buffer);
  return 1;
}
