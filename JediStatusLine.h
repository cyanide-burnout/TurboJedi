#ifndef JEDISTATUSLINE_H
#define JEDISTATUSLINE_H

// TurboJedi, Turbo Journald Interface
// https://github.com/cyanide-burnout/TurboJedi
// Artem Prilutskiy, 2026

#define Uses_TStatusLine
#define Uses_TStatusDef
#include <tvision/tv.h>

#include <string>

class JediStatusLine : public TStatusLine
{
  public:

    JediStatusLine(const TRect& bounds, TStatusDef& definitions);

    const char* hint(ushort context) override;

  private:

    std::string status;
};

#endif
