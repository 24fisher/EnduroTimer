#pragma once

#include <Arduino.h>

class LoopMonitor {
public:
  void tick(uint32_t nowMs = millis()) {
    if (lastLoopMs_ != 0) {
      lastLoopGapMs_ = nowMs - lastLoopMs_;
      if (lastLoopGapMs_ > maxLoopGapMs_) maxLoopGapMs_ = lastLoopGapMs_;
      if (lastLoopGapMs_ > 200UL) {
        Serial.printf("WARN loop gap=%lums lastSlowBlock=%s\n", static_cast<unsigned long>(lastLoopGapMs_), lastSlowBlock_.c_str());
      }
    }
    lastLoopMs_ = nowMs;
  }

  void recordBlock(const char* name, uint32_t durationMs, uint32_t warnMs = 100UL) {
    if (durationMs > lastSlowBlockDurationMs_) {
      lastSlowBlock_ = name;
      lastSlowBlockDurationMs_ = durationMs;
    }
    if (durationMs > warnMs) {
      lastSlowBlock_ = name;
      lastSlowBlockDurationMs_ = durationMs;
      Serial.printf("WARN slow block name=%s durationMs=%lu\n", name, static_cast<unsigned long>(durationMs));
    }
  }

  uint32_t lastLoopGapMs() const { return lastLoopGapMs_; }
  uint32_t maxLoopGapMs() const { return maxLoopGapMs_; }
  String lastSlowBlock() const { return lastSlowBlock_; }
  uint32_t lastSlowBlockDurationMs() const { return lastSlowBlockDurationMs_; }

private:
  uint32_t lastLoopMs_ = 0;
  uint32_t lastLoopGapMs_ = 0;
  uint32_t maxLoopGapMs_ = 0;
  String lastSlowBlock_ = "none";
  uint32_t lastSlowBlockDurationMs_ = 0;
};
