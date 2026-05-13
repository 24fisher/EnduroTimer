#include "FinishState.h"

void FinishState::begin() {
  state_ = FinishRunState::Idle;
}

void FinishState::startRun(const String& runId, uint32_t startTimestampMs) {
  runId_ = runId;
  startTimestampMs_ = startTimestampMs;
  finishTimestampMs_ = 0;
  state_ = FinishRunState::WaitFinish;
}

void FinishState::markFinishSent(uint32_t finishTimestampMs) {
  finishTimestampMs_ = finishTimestampMs;
  state_ = FinishRunState::FinishSent;
}

void FinishState::ackFinish() {
  runId_ = "";
  startTimestampMs_ = 0;
  finishTimestampMs_ = 0;
  state_ = FinishRunState::Idle;
}

void FinishState::fail() {
  state_ = FinishRunState::Error;
}

String FinishState::stateText() const {
  switch (state_) {
    case FinishRunState::Boot: return "Boot";
    case FinishRunState::Idle: return "Idle";
    case FinishRunState::WaitFinish: return "WaitFinish";
    case FinishRunState::FinishSent: return "FinishSent";
    case FinishRunState::Error: return "Error";
  }
  return "Error";
}
