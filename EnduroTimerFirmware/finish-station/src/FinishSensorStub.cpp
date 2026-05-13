#include "FinishSensorStub.h"

static constexpr uint32_t ButtonDebounceMs = 70UL;

void FinishSensorStub::begin() {
  pinMode(FINISH_BUTTON_PIN, INPUT_PULLUP);
  stableState_ = digitalRead(FINISH_BUTTON_PIN);
  lastReading_ = stableState_;
  lastChangeMs_ = millis();
  Serial.printf("Button: configured FINISH_BUTTON_PIN=%d INPUT_PULLUP\n", FINISH_BUTTON_PIN);
}

void FinishSensorStub::arm(const String& runId, uint32_t startTimestampMs) {
  armed_ = true;
  runId_ = runId;
  startTimestampMs_ = startTimestampMs;
  localArmedAtMs_ = millis();
  pressConsumed_ = false;
  Serial.printf("[FinishSensorStub] button armed run=%s\n", runId.c_str());
}

void FinishSensorStub::reset() {
  armed_ = false;
  runId_ = "";
  localArmedAtMs_ = 0;
  startTimestampMs_ = 0;
  pressConsumed_ = false;
}

bool FinishSensorStub::update(uint32_t nowMs, uint32_t& finishTimestampMs) {
  const int reading = digitalRead(FINISH_BUTTON_PIN);
  if (reading != lastReading_) {
    lastReading_ = reading;
    lastChangeMs_ = nowMs;
  }

  if (nowMs - lastChangeMs_ < ButtonDebounceMs || reading == stableState_) return false;

  stableState_ = reading;
  if (stableState_ == HIGH) {
    pressConsumed_ = false;
    return false;
  }

  if (pressConsumed_) return false;
  pressConsumed_ = true;

  if (!armed_) {
    finishTimestampMs = 0;
    return true;
  }

  finishTimestampMs = startTimestampMs_ + (nowMs - localArmedAtMs_);
  armed_ = false;
  Serial.printf("FINISH BUTTON pressed: run=%s finish=%lu\n", runId_.c_str(), static_cast<unsigned long>(finishTimestampMs));
  return true;
}

uint32_t FinishSensorStub::elapsedMs(uint32_t nowMs) const {
  if (!armed_) return 0;
  return nowMs - localArmedAtMs_;
}
