#include "FinishSensorStub.h"

static constexpr uint32_t SimulatedFinishDelayMs = 20000UL;

void FinishSensorStub::arm(const String& runId, uint32_t startTimestampMs) {
  armed_ = true;
  runId_ = runId;
  startTimestampMs_ = startTimestampMs;
  localArmedAtMs_ = millis();
  Serial.printf("[FinishSensorStub] armed run=%s simulated delay=%lu ms\n", runId.c_str(),
                static_cast<unsigned long>(SimulatedFinishDelayMs));
}

void FinishSensorStub::reset() {
  armed_ = false;
  runId_ = "";
  localArmedAtMs_ = 0;
  startTimestampMs_ = 0;
}

bool FinishSensorStub::update(uint32_t nowMs, uint32_t& finishTimestampMs) {
  if (!armed_ || nowMs - localArmedAtMs_ < SimulatedFinishDelayMs) return false;

  finishTimestampMs = startTimestampMs_ + SimulatedFinishDelayMs;
  armed_ = false;
  return true;
}

uint32_t FinishSensorStub::elapsedMs(uint32_t nowMs) const {
  if (!armed_) return 0;
  return nowMs - localArmedAtMs_;
}
