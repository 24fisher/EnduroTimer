#pragma once

#include <Arduino.h>

class ClockService {
public:
  uint32_t nowMs() const;
};

String formatDurationMs(uint32_t durationMs);
