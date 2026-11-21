// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// Values of the bus: the entries and filters a call is answered with, the options and the values of a method read and
// checked

#include "ServicePrivate.h"

static const char* const FieldNames[JOURNAL_FIELD_COUNT] = { "unit", "identifier", "command", "transport", "boot", "user", "machine" };
static const char* const LevelNames[] = { "emerg", "alert", "crit", "err", "warning", "notice", "info", "debug" };
static const char LevelLetters[] = "EACRWNID";

// A value D-Bus takes as a string: UTF-8 without NUL, each character in its shortest form, no surrogates, nothing past
// U+10FFFF; sd-bus refuses the message otherwise
bool IsText(const std::string& value)
{
  size_t index;
  size_t count;
  uint32_t code;
  unsigned char byte;

  for (index = 0; index < value.size(); index += count + 1)
  {
    byte = value[index];

    if      (byte == 0x00)          return false;
    else if (byte <  0x80)          count = 0, code = byte;
    else if ((byte & 0xe0) == 0xc0) count = 1, code = byte & 0x1f;
    else if ((byte & 0xf0) == 0xe0) count = 2, code = byte & 0x0f;
    else if ((byte & 0xf8) == 0xf0) count = 3, code = byte & 0x07;
    else                            return false;

    if (count >= value.size() - index)
    {
      // Cut short
      return false;
    }

    for (size_t next = 1; next <= count; next ++)
    {
      byte = value[index + next];

      if ((byte & 0xc0) != 0x80)
      {
        return false;
      }

      code = (code << 6) | (byte & 0x3f);
    }

    if (((count == 1) && (code < 0x80)) ||
        ((count == 2) && (code < 0x800)) ||
        ((count == 3) && (code < 0x10000)) ||
        ((code >= 0xd800) && (code <= 0xdfff)) ||
        (code > 0x10ffff))
    {
      return false;
    }
  }

  return true;
}

// Appending to a message: every call adds to the result, an error of sd-bus stays negative

int AppendString(sd_bus_message* message, const char* key, const std::string& value)
{
  return sd_bus_message_append(message, "{sv}", key, "s", value.c_str());
}

int AppendStrings(sd_bus_message* message, const char* key, const std::vector<std::string>& values)
{
  int result;

  result = std::min(0, sd_bus_message_open_container(message, 'e', "sv"));
  result = std::min(result, sd_bus_message_append(message, "s", key));
  result = std::min(result, sd_bus_message_open_container(message, 'v', "as"));
  result = std::min(result, sd_bus_message_open_container(message, 'a', "s"));

  for (const std::string& value : values)
  {
    result = std::min(result, sd_bus_message_append(message, "s", value.c_str()));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  result = std::min(result, sd_bus_message_close_container(message));
  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

// The filter of a window or a timeline, the keys of ListWindows()
int AppendFilter(sd_bus_message* message, const JournalFilter& filter)
{
  int result;
  std::string levels;

  for (int index = 0; index < 8; index ++)
  {
    levels += (filter.levels & (1 << index)) ? LevelLetters[index] : '-';
  }

  result = std::min(0, AppendString(message, "directory", filter.directory));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "merge", "b", (int)filter.merge));
  result = std::min(result, AppendString(message, "text", filter.text));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "regex", "b", (int)((filter.mode & JOURNAL_TEXT_EXPRESSION) != 0)));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "exclude", "b", (int)((filter.mode & JOURNAL_TEXT_EXCLUDE) != 0)));
  result = std::min(result, sd_bus_message_append(message, "{sv}", "case", "b", (int)((filter.mode & JOURNAL_TEXT_SENSITIVE) != 0)));
  result = std::min(result, AppendString(message, "levels", levels));
  result = std::min(result, AppendStrings(message, "matches", filter.matches));

  for (int field = 0; field < JOURNAL_FIELD_COUNT; field ++)
  {
    if (!filter.values[field].empty())
    {
      // Only the ones the filter has
      result = std::min(result, AppendString(message, FieldNames[field], filter.values[field]));
    }
  }

  if (filter.pid != 0)          result = std::min(result, sd_bus_message_append(message, "{sv}", "pid", "u", (uint32_t)filter.pid));
  if (!filter.since.empty())    result = std::min(result, AppendString(message, "since", filter.since));
  if (!filter.until.empty())    result = std::min(result, AppendString(message, "until", filter.until));

  return result;
}

// An entry as a{sv}: a field once is s, several times as, binary ay or aay, the timestamps t
int AppendEntry(sd_bus_message* message, const JournalRecord& fields)
{
  int result;
  bool text;
  std::vector<std::string> names;
  std::map<std::string, std::vector<std::string>> values;

  for (const std::pair<std::string, std::string>& field : fields)
  {
    if (values.find(field.first) == values.end())
    {
      // In the order they come
      names.push_back(field.first);
    }

    values[field.first].push_back(field.second);
  }

  result = std::min(0, sd_bus_message_open_container(message, 'a', "{sv}"));

  for (const std::string& name : names)
  {
    const std::vector<std::string>& list = values[name];

    if ((name == "__REALTIME_TIMESTAMP") ||
        (name == "__MONOTONIC_TIMESTAMP"))
    {
      result = std::min(result, sd_bus_message_append(message, "{sv}", name.c_str(), "t", (uint64_t)strtoull(list[0].c_str(), nullptr, 10)));
      continue;
    }

    text   = std::all_of(list.begin(), list.end(), IsText);
    result = std::min(result, sd_bus_message_open_container(message, 'e', "sv"));
    result = std::min(result, sd_bus_message_append(message, "s", name.c_str()));

    if (list.size() == 1)
    {
      result = std::min(result, sd_bus_message_open_container(message, 'v', text ? "s" : "ay"));
      result = std::min(result, text ? sd_bus_message_append(message, "s", list[0].c_str()) : sd_bus_message_append_array(message, 'y', list[0].data(), list[0].size()));
      result = std::min(result, sd_bus_message_close_container(message));
    }
    else
    {
      result = std::min(result, sd_bus_message_open_container(message, 'v', text ? "as" : "aay"));
      result = std::min(result, sd_bus_message_open_container(message, 'a', text ? "s" : "ay"));

      for (const std::string& value : list)
      {
        result = std::min(result, text ? sd_bus_message_append(message, "s", value.c_str()) : sd_bus_message_append_array(message, 'y', value.data(), value.size()));
      }

      result = std::min(result, sd_bus_message_close_container(message));
      result = std::min(result, sd_bus_message_close_container(message));
    }

    result = std::min(result, sd_bus_message_close_container(message));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

int AppendEntries(sd_bus_message* message, const std::vector<JournalRecord>& records)
{
  int result;

  result = std::min(0, sd_bus_message_open_container(message, 'a', "a{sv}"));

  for (const JournalRecord& record : records)
  {
    result = std::min(result, AppendEntry(message, record));
  }

  result = std::min(result, sd_bus_message_close_container(message));
  return result;
}

int ReadOptions(sd_bus_message* message, Options& options)
{
  int result;
  char type;
  char** list;
  int flag;
  uint32_t small;
  uint64_t large;
  int32_t signed32;
  int64_t signed64;
  const char* key;
  const char* contents;
  const char* text;

  if ((result = sd_bus_message_enter_container(message, 'a', "{sv}")) < 0)
  {
    return result;
  }

  while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0)
  {
    if (((result = sd_bus_message_read(message, "s", &key)) < 0) ||
        ((result = sd_bus_message_peek_type(message, &type, &contents)) < 0) ||
        ((result = sd_bus_message_enter_container(message, 'v', contents)) < 0))
    {
      return result;
    }

    if (strcmp(contents, "s") == 0)
    {
      result = sd_bus_message_read(message, "s", &text);
      options.texts[key] = text;
    }
    else if (strcmp(contents, "b") == 0)
    {
      result = sd_bus_message_read(message, "b", &flag);
      options.flags[key] = flag;
    }
    else if (strcmp(contents, "u") == 0)
    {
      result = sd_bus_message_read(message, "u", &small);
      options.numbers[key] = small;
    }
    else if (strcmp(contents, "t") == 0)
    {
      result = sd_bus_message_read(message, "t", &large);
      options.numbers[key] = large;
    }
    else if (strcmp(contents, "i") == 0)
    {
      // Python and others send a number as i by default, a negative one is taken as zero
      result = sd_bus_message_read(message, "i", &signed32);
      options.numbers[key] = std::max(signed32, 0);
    }
    else if (strcmp(contents, "x") == 0)
    {
      result = sd_bus_message_read(message, "x", &signed64);
      options.numbers[key] = std::max<int64_t>(signed64, 0);
    }
    else if ((strcmp(contents, "as") == 0) &&
             ((result = sd_bus_message_read_strv(message, &list)) >= 0))
    {
      for (char** item = list; (item != nullptr) && (*item != nullptr); item ++)
      {
        options.matches.push_back(*item);
        free(*item);
      }

      free(list);
    }
    else
    {
      result = sd_bus_message_skip(message, contents);
    }

    if ((result < 0) ||
        ((result = sd_bus_message_exit_container(message)) < 0) ||
        ((result = sd_bus_message_exit_container(message)) < 0))
    {
      return result;
    }
  }

  return (result < 0) ? result : sd_bus_message_exit_container(message);
}

// The filter of the options as query() and open() of a plugin make it: the journal of the window of `id`, or of this
// machine; false and the reason when an option is wrong
bool MakeFilter(const Options& options, JournalFilter& filter, std::string& error)
{
  int level;
  uint64_t stamp;
  JournalWindow* window;
  std::map<std::string, std::string>::const_iterator text;
  std::map<std::string, uint64_t>::const_iterator number;

  filter           = JournalFilter { };
  filter.levels    = 0xff;
  filter.columns   = JOURNAL_COLUMN_DEFAULT;
  filter.highlight = JOURNAL_HIGHLIGHT_BUILTIN;

  if ((number = options.numbers.find("id")) != options.numbers.end())
  {
    if ((window = FindWindow(number->second)) == nullptr)
    {
      error = "there is no window of id " + std::to_string(number->second);
      return false;
    }

    // Its journal and the way it shows them
    filter.directory = window->view->getFilter().directory;
    filter.merge     = window->view->getFilter().merge;
    filter.columns   = window->view->getFilter().columns;
    filter.highlight = window->view->getFilter().highlight;
  }

  for (const std::string& match : options.matches)
  {
    if (!CheckJournalMatch(match))
    {
      error = match + " is not a FIELD=VALUE match";
      return false;
    }

    filter.matches.push_back(match);
  }

  if ((text = options.texts.find("text")) != options.texts.end())
  {
    filter.text = text->second;
    filter.mode = (options.flags.count("regex")   && options.flags.at("regex")   ? JOURNAL_TEXT_EXPRESSION : 0) |
                  (options.flags.count("exclude") && options.flags.at("exclude") ? JOURNAL_TEXT_EXCLUDE    : 0) |
                  (options.flags.count("case")    && options.flags.at("case")    ? JOURNAL_TEXT_SENSITIVE  : 0);

    if ((filter.mode & JOURNAL_TEXT_EXPRESSION) &&
        (!JournalExpression().compile(filter.text, !(filter.mode & JOURNAL_TEXT_SENSITIVE), &error)))
    {
      return false;
    }
  }

  if (((text = options.texts.find("since")) != options.texts.end()) && (!ParseJournalTime(filter.since = text->second, &stamp)))
  {
    error = "not a time TurboJedi knows, or one before 1970: " + text->second;
    return false;
  }

  if (((text = options.texts.find("until")) != options.texts.end()) && (!ParseJournalTime(filter.until = text->second, &stamp)))
  {
    error = "not a time TurboJedi knows, or one before 1970: " + text->second;
    return false;
  }

  if ((text = options.texts.find("priority")) != options.texts.end())
  {
    // Up to the level, like -p
    for (level = 0; (level < 8) && (text->second != LevelNames[level]) && (text->second != std::to_string(level)); level ++);

    if (level == 8)
    {
      error = "unknown priority " + text->second;
      return false;
    }

    filter.levels = (2 << level) - 1;
  }

  return true;
}

// An integer type of the bus: y n q i u x t
static bool IsInteger(const char* type)
{
  return (type[0] != '\0') && (type[1] == '\0') && (strchr("ynqiuxt", type[0]) != nullptr);
}

// The integer of the variant the message is at, of a type IsInteger() has taken, as a sign and a magnitude: the variant is
// read through
static bool ReadInteger(sd_bus_message* message, const char* type, bool& negative, uint64_t& magnitude)
{
  int64_t value;
  union { uint8_t y; int16_t n; uint16_t q; int32_t i; uint32_t u; int64_t x; uint64_t t; } number;

  if ((sd_bus_message_enter_container(message, 'v', type) < 0) ||
      (sd_bus_message_read_basic(message, type[0], &number) < 0) ||
      (sd_bus_message_exit_container(message) < 0))
  {
    return false;
  }

  switch (type[0])
  {
    case 'y': value = number.y; break;
    case 'n': value = number.n; break;
    case 'q': value = number.q; break;
    case 'i': value = number.i; break;
    case 'u': value = number.u; break;
    case 'x': value = number.x; break;
    default:  negative = false; magnitude = number.t; return true;
  }

  negative  = (value < 0);
  magnitude = negative ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
  return true;
}

// A value of the bus fits a type of an argument: an integer of another type when its value fits, any number for d; a
// dictionary a{sv} whatever its values are, the method gets them as tables. An integer is read through, read tells it;
// the variant of another value is left for the caller to skip
static bool FitsArgument(sd_bus_message* message, const char* contents, const std::string& type, bool& read)
{
  bool negative;
  uint64_t magnitude;

  read = false;

  if ((type == "s") || (type == "b") || (type == "as") || (type == "a{sv}") ||
      (strcmp(contents, "d") == 0))
  {
    return (type == contents) || ((type == "d") && (strcmp(contents, "d") == 0));
  }

  if ((!IsInteger(contents)) ||
      (!(read = ReadInteger(message, contents, negative, magnitude))))
  {
    return false;
  }

  return (type == "d") ||
         ((type == "i") && (magnitude <= (negative ? 0x80000000ULL : 0x7fffffffULL))) ||
         ((type == "u") && (!negative) && (magnitude <= 0xffffffffULL)) ||
         ((type == "x") && (magnitude <= (negative ? 0x8000000000000000ULL : 0x7fffffffffffffffULL))) ||
         ((type == "t") && (!negative));
}

// The values of Invoke() at the place of the message: the keys of TurboJedi taken, _window of the window the method runs
// in, the ones of the method checked by its arguments when it has told them. False and why for a value it does not take,
// a key of TurboJedi there is not, or an argument it needs and has not got
bool CheckValues(sd_bus_message* message, const PluginMethod* method, bool& windowed, uint32_t& window, std::string& error)
{
  int result;
  char type;
  bool read;
  bool negative;
  uint64_t magnitude;
  const char* key;
  const char* contents;
  std::set<std::string> given;
  std::vector<PluginArgument>::const_iterator argument;

  windowed = false;
  error    = "the values cannot be read";

  if (sd_bus_message_enter_container(message, 'a', "{sv}") < 0)
  {
    return false;
  }

  while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0)
  {
    read = false;

    if ((sd_bus_message_read(message, "s", &key) < 0) ||
        (sd_bus_message_peek_type(message, &type, &contents) < 0))
    {
      return false;
    }

    if (key[0] == '_')
    {
      // Of TurboJedi, not of the method
      if (strcmp(key, "_window") != 0)
      {
        error = std::string("TurboJedi has no key ") + key + ", the keys beginning with _ are of TurboJedi";
        return false;
      }

      if ((!IsInteger(contents)) ||
          (!(read = ReadInteger(message, contents, negative, magnitude))) ||
          (negative) ||
          (magnitude > 0xffffffffULL))
      {
        error = "_window is the id of a window, u";
        return false;
      }

      windowed = true;
      window   = (uint32_t)magnitude;
    }
    else if (method->declared)
    {
      argument = std::find_if(method->arguments.begin(), method->arguments.end(), [key] (const PluginArgument& item)
      {
        return item.name == key;
      });

      if (argument == method->arguments.end())
      {
        error = "method " + method->method + " of plugin " + method->plugin + " takes no argument " + key;
        return false;
      }

      if (!FitsArgument(message, contents, argument->type, read))
      {
        error = "argument " + argument->name + " is " + argument->type + ", the value given is not";
        return false;
      }

      given.insert(key);
    }

    if (((!read) && (sd_bus_message_skip(message, "v") < 0)) ||
        (sd_bus_message_exit_container(message) < 0))
    {
      return false;
    }
  }

  if ((result < 0) ||
      (sd_bus_message_exit_container(message) < 0))
  {
    return false;
  }

  for (const PluginArgument& item : method->arguments)
  {
    if (item.required && (given.count(item.name) == 0))
    {
      error = "method " + method->method + " of plugin " + method->plugin + " needs argument " + item.name;
      return false;
    }
  }

  return true;
}
