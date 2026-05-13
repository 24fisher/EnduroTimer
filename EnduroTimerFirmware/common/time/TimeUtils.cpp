#include "TimeUtils.h"

uint32_t ClockService::nowMs() const {
  return millis();
}

String formatDurationMs(uint32_t durationMs) {
  const uint32_t minutes = durationMs / 60000UL;
  const uint32_t seconds = (durationMs % 60000UL) / 1000UL;
  const uint32_t millisPart = durationMs % 1000UL;

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu.%03lu", static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds), static_cast<unsigned long>(millisPart));
  return String(buffer);
}
