#pragma once

#include <Arduino.h>

class FinishSensorStub {
public:
  void arm(const String& runId, uint32_t startTimestampMs);
  void reset();
  bool update(uint32_t nowMs, uint32_t& finishTimestampMs);
  uint32_t elapsedMs(uint32_t nowMs) const;
  bool armed() const { return armed_; }

private:
  bool armed_ = false;
  String runId_;
  uint32_t localArmedAtMs_ = 0;
  uint32_t startTimestampMs_ = 0;
};
