#ifndef ACTIONDIALOGS_H
#define ACTIONDIALOGS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

class TMenuItem;
class TView;
struct TPoint;
struct TEvent;

// Dialogs an action of a plugin asks the user with, modal: the windows go on behind them. Sized by their text up to the
// desktop, the text is shown as it is, ~ included

// Text with OK
void ShowActionText(const char* title, const std::string& text);

// Text with a line to type a question into under it, Ask and Close: false when closed, the text wrapped at the width
// of the dialog and scrolled when it is longer than the desktop
bool AskActionText(const char* title, const std::string& text, const std::string& prompt, std::string& value);

// Yes or No, No is the default one, Esc is No as well
bool AskActionConfirm(const std::string& text);

// A line to edit, false when cancelled
bool AskActionInput(const std::string& prompt, std::string& value);

// One of the items, the initial one under the cursor; -1 when cancelled
int AskActionChoice(const std::string& title, const std::vector<std::string>& items, int initial);

// A popup menu of the items, which it takes, as the Actions menu opens: under where, or in the middle of the desktop
// when where.x is negative; a title on its upper frame when there is one; Right opens a submenu as Enter does, Left and
// Esc close it. With keyboard the first item is under the cursor. The command chosen, 0 when none
unsigned short ShowActionMenu(TPoint where, TMenuItem& items, const std::string& title, bool keyboard);

// Right on an item with a submenu in a menu of ShowActionMenu() made Enter, which opens it; for getEvent() of the
// application, with TopView()
void TranslateActionMenuKey(TEvent& event, TView* top);

// One of the items in a popup menu in the middle of the desktop, the title on its frame, both fit to it by the caller; up
// to 100 items; -1 when closed
int AskActionPopup(const std::string& title, const std::vector<std::string>& items);

#endif
