#ifndef JOURNALVIEWPRIVATE_H
#define JOURNALVIEWPRIVATE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// What the parts of JournalView share: the headers, the limits of a window and the helpers one part has of another

#define Uses_TFrame
#define Uses_TWindow
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TStatusLine
#define Uses_TFileDialog
#define Uses_MsgBox
#include "JournalView.h"
#include "TimeDialog.h"
#include "PageDialog.h"
#include "PickDialog.h"
#include "TextDialog.h"
#include "FilterDialog.h"
#include "ColumnsDialog.h"
#include "JournalsDialog.h"
#include "JediClipboard.h"
#include "JournalQuery.h"
#include "JediScript.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <fstream>
#include <algorithm>

#define NO_SELECTION  UINT64_MAX
#define FIRST_LINE    (1ULL << 62)  // Number of the first line, history goes in front of it
#define READ_STEP     5000          // Entries read at once, the rest comes with the next polls
#define BYTE_CAPACITY (64 << 20)    // Text a window keeps whatever the count of its lines, one huge message stays
#define HISTORY_STEP  1000          // Entries brought in front when scrolling above the first line

#define HOST_WIDTH        24  // Limits of the columns as wide as their longest values
#define UNIT_WIDTH        32
#define IDENTIFIER_WIDTH  24
#define PID_WIDTH         8

int GetSourceWidth(const int* widths, uint16_t columns);
std::string GetSign(bool follow, uint64_t unseen);

#endif
