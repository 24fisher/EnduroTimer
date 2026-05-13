#include "FinishSensorStub.h"

static constexpr uint32_t ButtonDebounceMs = 50UL;

void FinishSensorStub::begin() {
  button_.begin(FINISH_BUTTON_PIN, ButtonDebounceMs);
  Serial.printf("Button: configured FINISH_BUTTON_PIN=%d INPUT_PULLUP\n", FINISH_BUTTON_PIN);
}

void FinishSensorStub::arm(const String& runId, uint32_t startTimestampMs) {
  armed_ = true;
  runId_ = runId;
  startTimestampMs_ = startTimestampMs;
  localArmedAtMs_ = millis();
  Serial.printf("[FinishSensorStub] button armed run=%s\n", runId.c_str());
}

void FinishSensorStub::reset() {
  armed_ = false;
  runId_ = "";
  localArmedAtMs_ = 0;
  startTimestampMs_ = 0;
}

bool FinishSensorStub::update(uint32_t nowMs, uint32_t& finishTimestampMs) {
  if (!button_.update(nowMs)) return false;

  Serial.println("FINISH button short press");
  if (!armed_) {
    finishTimestampMs = 0;
    return true;
  }

  finishTimestampMs = startTimestampMs_ + (nowMs - localArmedAtMs_);
  armed_ = false;
  Serial.printf("Finish accepted: runId=%s finish=%lu\n", runId_.c_str(), static_cast<unsigned long>(finishTimestampMs));
  return true;
}

uint32_t FinishSensorStub::elapsedMs(uint32_t nowMs) const {
  if (!armed_) return 0;
  return nowMs - localArmedAtMs_;
}
