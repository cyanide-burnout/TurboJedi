#ifndef ACTIONDIALOGS_H
#define ACTIONDIALOGS_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#include <vector>
#include <string>

// Dialogs an action of a plugin asks the user with, modal: the windows go on behind them. Sized by their text up to the
// desktop, the text is shown as it is, ~ included

// Text with OK
void ShowActionText(const char* title, const std::string& text);

// Yes or No, No is the default one, Esc is No as well
bool AskActionConfirm(const std::string& text);

// A line to edit, false when cancelled
bool AskActionInput(const std::string& prompt, std::string& value);

// One of the items, the initial one under the cursor; -1 when cancelled
int AskActionChoice(const std::string& title, const std::vector<std::string>& items, int initial);

#endif
