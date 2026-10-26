#ifndef JEDICLIPBOARD_H
#define JEDICLIPBOARD_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <string>

// Copied lines go to a command of the user on its standard input, such as remote-copy.sh of RemoteClipboard,
// or to the clipboard of Turbo Vision without one; input lines of the dialogs keep the clipboard of Turbo Vision

// Run by /bin/sh -c, empty for the clipboard of Turbo Vision
void SetClipboardCommand(const std::string& command);

// The clipboard of Turbo Vision takes the text when the command fails or does not finish in time
void CopyClipboardText(const std::string& text);

#endif
