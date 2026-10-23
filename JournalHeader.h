#ifndef JOURNALHEADER_H
#define JOURNALHEADER_H

#define Uses_TView
#include <tvision/tv.h>

class JournalView;

// Column titles above the lines of a journal window, they stay in place while the message scrolls

class JournalHeader : public TView
{
  public:

    JournalHeader(const TRect& bounds, JournalView* view);

    void draw() override;

  private:

    JournalView* view;
};

#endif
