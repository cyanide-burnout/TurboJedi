// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TLabel
#define Uses_TButton
#include "LevelsDialog.h"

// LevelsDialog

LevelsDialog::LevelsDialog() :
  TWindowInit(&TDialog::initFrame),
  TDialog(TRect(0, 0, 32, 16), "Levels")
{
  TCheckBoxes* boxes;

  options |= ofCentered;
  boxes    = new TCheckBoxes(TRect(3, 3, 29, 11),
    new TSItem("~E~mergency",
    new TSItem("~A~lert",
    new TSItem("~C~ritical",
    new TSItem("E~r~ror",
    new TSItem("~W~arning",
    new TSItem("~N~otice",
    new TSItem("~I~nformational",
    new TSItem("~D~ebug", nullptr)))))))));

  insert(boxes);
  insert(new TLabel(TRect(2, 2, 20, 3), "~S~how levels", boxes));
  insert(new TButton(TRect(4, 12, 14, 14), "~O~K", cmOK, bfDefault));
  insert(new TButton(TRect(17, 12, 28, 14), "Cancel", cmCancel, bfNormal));
  selectNext(False);
}
