#pragma once

#include <Arduino.h>

class LoopMonitor {
public:
  void tick(uint32_t nowMs = millis()) {
    if (lastLoopMs_ != 0) {
      lastLoopGapMs_ = nowMs - lastLoopMs_;
      if (lastLoopGapMs_ > maxLoopGapMs_) maxLoopGapMs_ = lastLoopGapMs_;
      if (lastLoopGapMs_ > 200UL) {
        Serial.printf("WARN loop gap=%lums\n", static_cast<unsigned long>(lastLoopGapMs_));
      }
    }
    lastLoopMs_ = nowMs;
  }

  uint32_t lastLoopGapMs() const { return lastLoopGapMs_; }
  uint32_t maxLoopGapMs() const { return maxLoopGapMs_; }

private:
  uint32_t lastLoopMs_ = 0;
  uint32_t lastLoopGapMs_ = 0;
  uint32_t maxLoopGapMs_ = 0;
};
