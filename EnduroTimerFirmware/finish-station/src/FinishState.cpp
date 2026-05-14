#include "FinishState.h"

void FinishState::begin() {
  state_ = FinishRunState::Idle;
}

void FinishState::startRun(const String& runId, const String& riderName, const String& trailName, uint32_t startTimestampMs, uint32_t localReceivedMs) {
  runId_ = runId;
  riderName_ = riderName.length() > 0 ? riderName : String("Test Rider");
  trailName_ = trailName;
  startTimestampMs_ = startTimestampMs;
  localRunStartReceivedMillis_ = localReceivedMs;
  finishTimestampMs_ = 0;
  state_ = FinishRunState::WaitFinish;
}

void FinishState::markFinishSent(uint32_t finishTimestampMs) {
  finishTimestampMs_ = finishTimestampMs;
  state_ = FinishRunState::FinishSent;
}

void FinishState::ackFinish() {
  runId_ = "";
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

uint32_t FinishState::elapsedMs(uint32_t nowMs) const {
  if (localRunStartReceivedMillis_ == 0 || state_ == FinishRunState::Idle) return 0;
  return nowMs - localRunStartReceivedMillis_;
}

String FinishState::stateText() const {
  switch (state_) {
    case FinishRunState::Boot: return "Boot";
    case FinishRunState::Idle: return "Idle";
    case FinishRunState::WaitFinish: return "Riding";
    case FinishRunState::FinishSent: return "FinishSent";
    case FinishRunState::Error: return "Error";
  }
  return "Error";
}
