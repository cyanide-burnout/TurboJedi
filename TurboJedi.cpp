// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include "Version.h"
#include "JediApplication.h"
#include "JournalLayout.h"
#include "JournalTools.h"
#include "JournalHighlighter.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <syslog.h>
#include <strings.h>
#include <sys/stat.h>

#include <systemd/sd-id128.h>

#define DEFAULT_HISTORY  1000
#define LAST_LAYOUT      "last.jedi"

static int GetPriorityLevel(const char* value)
{
  static const char* const names[] = { "emerg", "alert", "crit", "err", "warning", "notice", "info", "debug" };

  int level;

  if ((value[0] >= '0') &&
      (value[0] <= '7') &&
      (value[1] == '\0'))
  {
    // Numeric form
    return value[0] - '0';
  }

  for (level = 0; level < 8; level ++)
  {
    if (strcasecmp(value, names[level]) == 0)
    {
      // Symbolic form
      return level;
    }
  }

  return -1;
}

static void SetFilterValue(JournalFilter& filter, int field, const char* name, const std::string& value)
{
  if (filter.values[field].empty())
  {
    // The first one is shown and changed in the Source dialog
    filter.values[field] = value;
    return;
  }

  // Further ones of the same field are joined by OR as raw matches
  filter.matches.push_back(std::string(name) + "=" + value);
}

static std::string GetLastLayout()
{
  size_t position;
  const char* home;
  std::string path;

  if ((home = getenv("XDG_CONFIG_HOME")) != nullptr) path = std::string(home) + "/turbojedi/";
  else if ((home = getenv("HOME")) != nullptr)       path = std::string(home) + "/.config/turbojedi/";
  else                                                 return std::string();

  for (position = path.find('/', 1); position != std::string::npos; position = path.find('/', position + 1))
  {
    // Like mkdir -p, existing ones fail harmlessly
    mkdir(path.substr(0, position).c_str(), 0755);
  }

  return path + LAST_LAYOUT;
}

static void PrintUsage(const char* name)
{
  printf("TurboJedi %s, Turbo Journald Interface\n", VERSION);
  printf("Artem Prilutskiy, 2026, https://github.com/cyanide-burnout/TurboJedi\n");
  printf("\n");
  printf("Usage: %s [options] [FIELD=VALUE ...]\n", name);
  printf("Shows the system journal and follows it, every window asks the journal with a filter of its own\n");
  printf("\n");
  printf("  -t, --identifier ID   entries of SYSLOG_IDENTIFIER (repeatable)\n");
  printf("  -u, --unit UNIT       entries of systemd unit, .service is assumed without a suffix (repeatable)\n");
  printf("  -U, --user USER       entries of user, by name or number (repeatable)\n");
  printf("  -p, --priority LEVEL  entries up to LEVEL: emerg, alert, crit, err, warning, notice, info, debug or 0..7\n");
  printf("  -k, --kernel          kernel messages only\n");
  printf("  -S, --since TIME      entries from TIME on instead of the last ones\n");
  printf("      --until TIME      entries up to TIME, the window stops following then\n");
  printf("  -b, --boot            entries of the current boot only\n");
  printf("  -D, --directory DIR   journal files of DIR instead of the local journal\n");
  printf("  -n, --lines N         history to start with (default %d)\n", DEFAULT_HISTORY);
  printf("  -H, --highlight NAME  rules that paint messages: builtin (default), off, a rule file of grc\n");
  printf("                        such as conf.log, or a path to one\n");
  printf("  -c, --layout FILE     windows of a layout saved by File, Save layout, options are left for new windows then\n");
  printf("\n");
  printf("The layout of all windows is kept on exit in $XDG_CONFIG_HOME/turbojedi/%s and comes back on the next start\n", LAST_LAYOUT);
  printf("without a layout and filter options\n");
  printf("\n");
  printf("FIELD=VALUE matches any journal field. Matches of the same field are joined by OR, of different fields by AND\n");
  printf("TIME is 2026-10-23 19:00[:00], 2026-10-23, 19:00 (today), now, today, yesterday, tomorrow or -1h, +30m (s, m, h, d)\n");
  printf("Options set the filter of the first window, F7 changes the source of a window, F3 its levels, F4 its text,\n");
  printf("Ctrl-T its time range, F1 shows all the keys\n");
}

int main(int count, char* const* arguments)
{
  int index;
  int level;
  int result;
  int history;
  bool custom;
  uint64_t stamp;
  std::string value;
  std::string directory;
  std::string layout;
  std::string last;
  std::string error;
  std::vector<JournalLayout> windows;
  sd_id128_t boot;
  JournalFilter filter;
  JediApplication* application;
  char identifier[SD_ID128_STRING_MAX];

  struct option options[] =
  {
    { "identifier", required_argument, nullptr, 't' },
    { "unit",       required_argument, nullptr, 'u' },
    { "user",       required_argument, nullptr, 'U' },
    { "priority",   required_argument, nullptr, 'p' },
    { "kernel",     no_argument,       nullptr, 'k' },
    { "since",      required_argument, nullptr, 'S' },
    { "until",      required_argument, nullptr, 'T' },
    { "boot",       no_argument,       nullptr, 'b' },
    { "directory",  required_argument, nullptr, 'D' },
    { "layout",     required_argument, nullptr, 'c' },
    { "lines",      required_argument, nullptr, 'n' },
    { "highlight",  required_argument, nullptr, 'H' },
    { "help",       no_argument,       nullptr, 'h' },
    { nullptr,      0,                 nullptr, 0   }
  };

  history          = DEFAULT_HISTORY;
  custom           = false;
  filter.pid       = 0;
  filter.mode      = 0;
  filter.levels    = 0xff;
  filter.columns   = JOURNAL_COLUMN_DEFAULT;
  filter.highlight = JOURNAL_HIGHLIGHT_BUILTIN;

  while ((result = getopt_long(count, arguments, "t:u:U:p:kS:bD:c:n:H:h", options, nullptr)) > 0)
  {
    // Only the history and the directory keep the last layout
    custom |= (result != 'n') && (result != 'D');

    switch (result)
    {
      case 't':
        SetFilterValue(filter, JOURNAL_FIELD_IDENTIFIER, "SYSLOG_IDENTIFIER", optarg);
        break;

      case 'u':
        value = optarg;
        value = (value.find('.') == std::string::npos) ? (value + ".service") : value;
        SetFilterValue(filter, JOURNAL_FIELD_UNIT, "_SYSTEMD_UNIT", value);
        break;

      case 'U':
        // The first one stays a name for the Source dialog, further ones become raw matches that need the number
        value = filter.values[JOURNAL_FIELD_USER].empty() ? std::string(optarg) : GetJournalUserIdentifier(optarg);
        SetFilterValue(filter, JOURNAL_FIELD_USER, "_UID", value.empty() ? std::string("-") : value);
        break;

      case 'S':
      case 'T':
        if (!ParseJournalTime(optarg, &stamp))
        {
          fprintf(stderr, "Unknown time %s\n", optarg);
          return EXIT_FAILURE;
        }
        ((result == 'S') ? filter.since : filter.until) = optarg;
        break;

      case 'p':
        if ((level = GetPriorityLevel(optarg)) < 0)
        {
          fprintf(stderr, "Unknown priority %s\n", optarg);
          return EXIT_FAILURE;
        }
        filter.levels = (2 << level) - 1;
        break;

      case 'k':
        SetFilterValue(filter, JOURNAL_FIELD_TRANSPORT, "_TRANSPORT", "kernel");
        break;

      case 'b':
        if (sd_id128_get_boot(&boot) >= 0)
        {
          sd_id128_to_string(boot, identifier);
          SetFilterValue(filter, JOURNAL_FIELD_BOOT, "_BOOT_ID", identifier);
        }
        break;

      case 'D':
        directory = optarg;
        break;

      case 'c':
        layout = optarg;
        break;

      case 'n':
        history = atoi(optarg);
        break;

      case 'H':
        if ((strcmp(optarg, JOURNAL_HIGHLIGHT_OFF) != 0) &&
            (JournalHighlighter::get(optarg, &error) == nullptr))
        {
          fprintf(stderr, "Cannot read the rules, %s\n", error.c_str());
          return EXIT_FAILURE;
        }
        filter.highlight = optarg;
        break;

      default:
        PrintUsage(arguments[0]);
        return (result == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  }

  for (index = optind; index < count; index ++)
  {
    if (!CheckJournalMatch(arguments[index]))
    {
      fprintf(stderr, "%s is not a FIELD=VALUE match\n", arguments[index]);
      return EXIT_FAILURE;
    }

    filter.matches.push_back(arguments[index]);
    custom = true;
  }

  if ((!layout.empty()) &&
      (!LoadJournalLayout(layout, windows, error)))
  {
    // The last layout falls back to a window of the options, a given one has to be right
    fprintf(stderr, "Cannot read a layout from %s, %s\n", layout.c_str(), error.c_str());
    return EXIT_FAILURE;
  }

  if ((isatty(STDIN_FILENO)  == 0) ||
      (isatty(STDOUT_FILENO) == 0))
  {
    fprintf(stderr, "TurboJedi needs a terminal\n");
    return EXIT_FAILURE;
  }

  last   = GetLastLayout();
  layout = (layout.empty() && !custom) ? last : layout;

  application = new JediApplication(directory, filter, history, layout);
  application->run();

  if (!last.empty())
  {
    // Next start goes on from here
    application->save(last);
  }

  TObject::destroy(application);

  return EXIT_SUCCESS;
}
