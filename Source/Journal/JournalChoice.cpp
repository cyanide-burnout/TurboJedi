// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

// JournalView asked: the dialogs of the filter, columns, find, a moment, highlights, the source of the journals and time

#include "JournalViewPrivate.h"

void JournalView::chooseColumns()
{
  ushort value;

  value = filter.columns;

  if (TProgram::application->executeDialog(new ColumnsDialog(), &value) == cmOK)
  {
    // Lines keep every field, nothing to read again; the order is changed by turning the lines round
    filter.columns = (value & ~JOURNAL_COLUMN_REVERSE) | (filter.columns & JOURNAL_COLUMN_REVERSE);
    layout         = 0;
    arrange();

    if ((value ^ filter.columns) & JOURNAL_COLUMN_REVERSE)
    {
      reverse();
    }

    refresh();
  }
}

void JournalView::arrange()
{
  TRect bounds;

  if (header == nullptr)
  {
    // Not in a window yet
    return;
  }

  bounds     = getBounds();
  bounds.a.y = header->origin.y + ((filter.columns & JOURNAL_COLUMN_HEADLESS) ? 0 : 1);

  if (filter.columns & JOURNAL_COLUMN_HEADLESS)
  {
    header->hide();
  }
  else
  {
    header->show();
  }

  locate(bounds);
}

void JournalView::chooseFind()
{
  TextData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.text, finder.getText().c_str(), sizeof(data.text) - 1);
  data.options = finder.getMode();

  if (TProgram::application->executeDialog(new TextDialog("Find", "~F~ind"), &data) == cmOK)
  {
    // Checked by the dialog already
    finder.set(data.text, data.options);
    drawView();

    if (!finder.empty())
    {
      // The newest one from the selection or the newest end of the screen, then on into the history
      find(reversed() ? 1 : -1);
    }
  }
}

void JournalView::chooseMoment()
{
  size_t index;
  uint64_t stamp;
  char text[64];

  text[0] = '\0';

  if ((index = focus()) < lines.size())
  {
    // The time of the line at hand to start from, in the zone the window shows it in
    strncpy(text, FormatJournalTime(lines[index].realtime / 1000000 * 1000000, (filter.columns & JOURNAL_COLUMN_UTC) != 0).c_str(), sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
  }

  while (inputBox("Go to time", "~T~ime", text, sizeof(text) - 1) == cmOK)
  {
    if (ParseJournalTime(text, &stamp))
    {
      seek(stamp, false);
      return;
    }

    // Back to the box with what has been typed
    messageBox("Unknown time format", mfError | mfOKButton);
  }
}

void JournalView::chooseHighlight()
{
  std::string error;
  const char* label;
  PickDialog* dialog;
  std::shared_ptr<JournalHighlighter> chosen;
  std::map<std::string, std::string> values;  // Label to name
  std::map<std::string, std::string>::iterator item;

  JournalHighlighter::list(values);
  dialog = new PickDialog("Highlight", values);

  if ((TProgram::deskTop->execView(dialog) == cmOK) &&
      ((label = dialog->selection()) != nullptr) &&
      ((item = values.find(label)) != values.end()))
  {
    chosen = JournalHighlighter::get(item->second, &error);

    if (!error.empty())
    {
      messageBox(mfError | mfOKButton, "Cannot read the rules, %s", error.c_str());
    }
    else
    {
      // Lines keep their text, nothing to read again
      filter.highlight = item->second;
      highlighter      = chosen;
      drawView();
    }
  }

  TObject::destroy(dialog);
}

void JournalView::chooseFilter(int start)
{
  ushort result;
  JournalFilter draft;
  SimpleDialog* plain;
  FilterDialog* rules;

  // The simple filter when it shows all of it, the window of the rules otherwise; F3 goes to the levels of either
  draft  = filter;
  result = FitsSimpleFilter(draft) ? cmFilterSimple : cmFilterRules;

  // From one to the other with what has been chosen there, OK applies it, Cancel leaves the filter as it has been
  while ((result == cmFilterSimple) || (result == cmFilterRules))
  {
    if (result == cmFilterSimple)
    {
      plain  = new SimpleDialog(draft, start);
      result = TProgram::deskTop->execView(plain);

      if (result != cmCancel)
      {
        plain->apply(draft);
      }

      TObject::destroy(plain);
      continue;
    }

    rules  = new FilterDialog(draft, start);
    result = TProgram::deskTop->execView(rules);

    if (result != cmCancel)
    {
      rules->apply(draft);
    }

    TObject::destroy(rules);

    if ((result == cmFilterSimple) &&
        (!FitsSimpleFilter(draft)))
    {
      // The simple one would lose the rest
      messageBox("The simple filter shows a value of each field and a text: take the rules of more out first", mfInformation | mfOKButton);
      result = cmFilterRules;
    }
  }

  if (result == cmOK)
  {
    filter = draft;
    open();
  }
}

void JournalView::chooseSource()
{
  JournalsData data;

  memset(&data, 0, sizeof(data));
  strncpy(data.directory, filter.directory.c_str(), sizeof(data.directory) - 1);
  data.scope = !filter.directory.empty() ? JOURNALS_DIRECTORY : (filter.merge ? JOURNALS_ALL : JOURNALS_THIS);

  if (TProgram::application->executeDialog(new JournalsDialog(), &data) == cmOK)
  {
    // A directory is checked by the dialog already, the query starts over
    filter.merge     = data.scope == JOURNALS_ALL;
    filter.directory = (data.scope == JOURNALS_DIRECTORY) ? data.directory : "";
    open();
  }
}

void JournalView::chooseTime()
{
  bool utc;
  bool valid;
  ushort result;
  uint64_t stamp;
  std::string since;
  std::string until;
  TimeData data;

  // In the zone the range is written in, so OK as it is changes nothing; in the one the window shows its times in when
  // nothing in the range is of a zone
  memset(&data, 0, sizeof(data));
  data.utc = GetJournalTimeZone(filter.since, &utc) ? utc : (GetJournalTimeZone(filter.until, &utc) ? utc : ((filter.columns & JOURNAL_COLUMN_UTC) != 0));
  ConvertJournalTime(filter.since, false, data.utc, since);
  ConvertJournalTime(filter.until, false, data.utc, until);
  strncpy(data.since, since.c_str(), sizeof(data.since) - 1);
  strncpy(data.until, until.c_str(), sizeof(data.until) - 1);

  do
  {
    if ((result = TProgram::application->executeDialog(new TimeDialog(), &data)) != cmOK)
    {
      // Nothing changes
      return;
    }

    valid =
      ((data.since[0] == '\0') || ParseJournalTime(data.since, &stamp)) &&
      ((data.until[0] == '\0') || ParseJournalTime(data.until, &stamp));

    if (!valid)
    {
      // Back to the dialog with what has been typed
      messageBox("Unknown time format", mfError | mfOKButton);
    }
  }
  while (!valid);

  filter.since = data.since;
  filter.until = data.until;
  open();
}
