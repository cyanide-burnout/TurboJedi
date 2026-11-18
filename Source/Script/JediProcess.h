#ifndef JEDIPROCESS_H
#define JEDIPROCESS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

// Commands of the actions of plugins, always as a list of arguments for execvp: whatever an entry of the journal holds stays
// one argument and never reaches a shell. Each runs in a process group of its own, so that a timeout or a closed window stops
// whatever it has started

// How a command has ended, the way os.execute of Lua 5.4 tells it: exit with its code, signal with its number, timeout with
// the seconds it has had, cancel by the user, error when it has not started, with the reason in text
struct JediOutcome
{
  std::string how;
  int code;
  std::string text;
};

// Arguments as one line of sh, every one quoted only when it has to be
std::string QuoteArguments(const std::vector<std::string>& arguments);

// Started without a window: false and the reason when it has not started. Its end is told by PollBackground()
bool RunBackground(const std::vector<std::string>& arguments, std::string& failure);

// Commands in the background that have ended other than with exit 0, each with its standard error, are added to reports;
// true while any of them runs
bool PollBackground(std::vector<std::string>& reports);

// Runs the command and waits for it: a short one ends before anything is shown, a longer one gets a dialog with Cancel
// while the windows go on. Standard output and error are kept apart
JediOutcome CaptureCommand(const std::vector<std::string>& arguments, int timeout, std::string& output, std::string& errors);

// A window of its own shows what the command prints, its outcome at the end; closing the window stops it
bool OpenOutput(const std::vector<std::string>& arguments, std::string& failure);

// The screen is given back to the terminal while the command runs on it, Enter brings TurboJedi back
JediOutcome RunTerminal(const std::vector<std::string>& arguments);

#endif
