#ifndef JOURNALLAYOUT_H
#define JOURNALLAYOUT_H

#include <vector>
#include <string>

#include "JournalTools.h"

// One window of a layout file: an INI section [window] with the bounds on the desktop, the filter and the columns

struct JournalLayout
{
  int bounds[4];  // Left, top, right, bottom
  JournalFilter filter;
};

// Takes a whole file or nothing, the error tells the line and what is wrong there
bool LoadJournalLayout(const std::string& path, std::vector<JournalLayout>& windows, std::string& error);
bool SaveJournalLayout(const std::string& path, const std::vector<JournalLayout>& windows);

#endif
