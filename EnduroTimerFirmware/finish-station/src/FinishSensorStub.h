#pragma once

#include <Arduino.h>

#include "ButtonDebouncer.h"

#ifndef FINISH_BUTTON_PIN
#define FINISH_BUTTON_PIN 0
#endif

class FinishSensorStub {
public:
  void begin();
  void arm(const String& runId, uint32_t startTimestampMs);
  void reset();
  bool update(uint32_t nowMs, uint32_t& finishTimestampMs);
  uint32_t elapsedMs(uint32_t nowMs) const;
  bool armed() const { return armed_; }
  bool buttonPressed() const { return button_.pressed(); }
  int buttonRaw() const { return button_.rawState(); }

private:
  bool armed_ = false;
  String runId_;
  uint32_t localArmedAtMs_ = 0;
  uint32_t startTimestampMs_ = 0;
  int lastRawState_ = HIGH;
  ButtonDebouncer button_;
};
