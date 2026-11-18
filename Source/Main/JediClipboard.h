#ifndef JEDICLIPBOARD_H
#define JEDICLIPBOARD_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <sys/types.h>

#include <string>

// Copied lines go to a command of the user on its standard input, such as remote-copy.sh of RemoteClipboard,
// or to the clipboard of Turbo Vision without one; input lines of the dialogs keep the clipboard of Turbo Vision

// Run by /bin/sh -c, empty for the clipboard of Turbo Vision. With a user, as that user, its groups, home and name:
// the command of the user of sudo, from a file of that user, never runs as root; a user the system does not know
// leaves the clipboard of Turbo Vision
void SetClipboardCommand(const std::string& command, uid_t user = 0);

// copy = COMMAND of a file of settings, false without one: a regular file of the owner, not a link, so that root does
// not read the line of another file for it
bool ReadClipboardCommand(const std::string& path, uid_t owner, std::string& command);

// The clipboard of Turbo Vision takes the text when the command fails or does not finish in time
void CopyClipboardText(const std::string& text);

#endif
