#include "FinishState.h"

void FinishState::begin() {
  state_ = FinishRunState::Idle;
}

void FinishState::startRun(const String& runId, uint32_t runNumber, const String& riderName, const String& trailName, uint32_t raceStartTimeMs, uint32_t localReceivedMs) {
  runId_ = runId;
  runNumber_ = runNumber;
  riderName_ = riderName.length() > 0 ? riderName : String("Test Rider");
  trailName_ = trailName;
  startTimestampMs_ = raceStartTimeMs;
  localRunStartReceivedMillis_ = localReceivedMs;
  finishTimestampMs_ = 0;
  state_ = FinishRunState::Riding;
}

void FinishState::markFinishSent(uint32_t finishTimestampMs) {
  finishTimestampMs_ = finishTimestampMs;
  state_ = FinishRunState::FinishSent;
}

void FinishState::ackFinish() {
  runId_ = "";
  runNumber_ = 0;
  riderName_ = "";
  trailName_ = "";
  startTimestampMs_ = 0;
  localRunStartReceivedMillis_ = 0;
  finishTimestampMs_ = 0;
  state_ = FinishRunState::Idle;
}

void FinishState::fail() {
  state_ = FinishRunState::Error;
}

void FinishState::ackTimeout() {
  state_ = FinishRunState::AckTimeout;
}

uint32_t FinishState::elapsedMs(uint32_t nowMs) const {
  if (startTimestampMs_ == 0 || state_ == FinishRunState::Idle) return 0;
  return nowMs >= startTimestampMs_ ? nowMs - startTimestampMs_ : 0;
}

String FinishState::stateText() const {
  switch (state_) {
    case FinishRunState::Boot: return "Boot";
    case FinishRunState::Idle: return "Idle";
    case FinishRunState::Riding: return "Riding";
    case FinishRunState::FinishSent: return "FinishSent";
    case FinishRunState::AckTimeout: return "AckTimeout";
    case FinishRunState::Error: return "Error";
  }
  return "Error";
}
