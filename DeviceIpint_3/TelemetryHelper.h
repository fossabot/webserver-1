#ifndef TELEMETRYHELPER_H
#define TELEMETRYHELPER_H

#include "../Telemetry.h"

NMMSS::IPreset* CreatePreset(unsigned long pos, const wchar_t* label, bool savedOnDevice, NMMSS::AbsolutePositionInformation positionInfo);
NMMSS::IFlaggedRange* CreateFlaggedRange(NMMSS::ERangeFlag flag, long min, long max);

#endif // TELEMETRYHELPER_H
