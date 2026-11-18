// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TScrollBar
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TStringCollection
#define Uses_TEvent
#define Uses_MsgBox
#include "DirectoryDialog.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <algorithm>
#include <sys/stat.h>

#define PATH_LENGTH  256

// DirectoryDialog

DirectoryDialog::DirectoryDialog(const std::string& path) :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 60, 22), "Directory")
{
  TScrollBar* bar;

  options |= ofCentered;
  line     = new TInputLine(TRect(3, 3, 57, 4), PATH_LENGTH);
  bar      = new TScrollBar(TRect(56, 6, 57, 17));
  box      = new TListBox(TRect(3, 6, 56, 17), 1, bar);

  insert(line);
  insert(new TLabel(TRect(2, 2, 20, 3), "~P~ath", line));
  insert(bar);
  insert(box);
  insert(new TLabel(TRect(2, 5, 30, 6), "~S~ubdirectories", box));
  insert(new TStaticText(TRect(3, 17, 57, 18), "Space or a double click opens a directory"));
  insert(new TButton(TRect(16, 19, 28, 21), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(31, 19, 43, 21), "Cancel", cmCancel, bfNormal));

  if ((!load(path)) &&
      (!load(failed.substr(0, std::max<size_t>(failed.find_last_of('/'), 1)))))
  {
    // Told why already: the one above the directory that could not be read, then the root
    load("/");
  }

  box->select();
}

void DirectoryDialog::handleEvent(TEvent& event)
{
  const char* name;

  TDialog::handleEvent(event);

  if ((event.what == evBroadcast) &&
      (event.message.command == cmListItemSelected) &&
      (box->range > 0))
  {
    // Down into the chosen one, or up by ..
    name = static_cast<const char*>(box->list()->at(box->focused));
    load(std::string(line->data) + "/" + name);
    clearEvent(event);
  }
}

void DirectoryDialog::shutDown()
{
  // A list box destroys the collection it is given only when it gets another one
  box->newList(nullptr);
  TDialog::shutDown();
}

std::string DirectoryDialog::selection()
{
  return line->data;
}

bool DirectoryDialog::load(const std::string& path)
{
  DIR* handle;
  size_t position;
  struct stat status;
  struct dirent* entry;
  char resolved[PATH_MAX];
  std::string probe;
  TStringCollection* collection;

  probe = path.empty() ? "/" : path;

  while ((realpath(probe.c_str(), resolved) == nullptr) ||
         (stat(resolved, &status) != 0) ||
         (!S_ISDIR(status.st_mode)))
  {
    if (probe == "/")
    {
      // Nothing left above, opendir() tells what is wrong with it
      strcpy(resolved, "/");
      break;
    }

    // A path with a typo or a file in it opens the nearest directory above, the way file managers do,
    // a relative one goes through the current directory
    position = probe.find_last_of('/');
    probe    = (position == std::string::npos) ? ((probe == ".") ? "/" : ".") : ((position == 0) ? "/" : probe.substr(0, position));
  }

  if ((handle = opendir(resolved)) == nullptr)
  {
    // No right to read it, the dialog stays where it is
    messageBox(mfError | mfOKButton, "Cannot open %s: %s", resolved, strerror(errno));
    failed = resolved;
    return false;
  }

  collection = new TStringCollection(16, 16);

  if (strcmp(resolved, "/") != 0)
  {
    // Up first, a dot sorts before the names
    collection->insert(newStr(".."));
  }

  while ((entry = readdir(handle)) != nullptr)
  {
    if ((entry->d_name[0] != '.') &&
        (fstatat(dirfd(handle), entry->d_name, &status, 0) == 0) &&
        S_ISDIR(status.st_mode))
    {
      // Hidden ones stay out, symbolic links to directories are followed
      collection->insert(newStr(entry->d_name));
    }
  }

  closedir(handle);

  // The list destroys the previous collection
  box->newList(collection);
  strncpy(line->data, resolved, PATH_LENGTH - 1);
  line->data[PATH_LENGTH - 1] = '\0';
  line->selectAll(True);
  line->drawView();

  return true;
}
