#ifndef JEDISETTINGS_H
#define JEDISETTINGS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <sys/types.h>
#include <stdio.h>

#include <string>

// Files TurboJedi writes by itself, update.conf and screen.conf and the ones of settings() of the plugins: run by sudo,
// they are the ones of the user of sudo, read and written as that user, so that root and the user see one choice and the
// files stay theirs

// The user of sudo the files are of, 0 for the process itself; a user the system does not know leaves them unwritten
void SetSettingsUser(uid_t user);

// The directories of the files, ~/.config/turbojedi/ of the user for settings and ~/.local/share/turbojedi/ for data
// of the user, a base of bookmarks for one, and a file of a name there; empty without a directory
void SetSettingsDirectory(const std::string& directory, const std::string& data);
std::string GetSettingsPath(const std::string& name);

// A file of data kept among the settings before, by TurboJedi 0.18, is moved to the directory of data as its user, not
// while a journal of SQLite is beside it; where it is when it cannot be moved
std::string GetDataPath(const std::string& name);

// The file to read, nullptr without one: a regular file of its user, not a link
FILE* OpenSettings(const std::string& path);

// The text as the whole file, its directories made when they are not there, false when it cannot be written; a file
// written anew takes the mode, 0600 for one of the user alone
bool SaveSettings(const std::string& path, const std::string& text, mode_t mode = 0644);

#endif
