#include "StartState.h"

void StartState::begin() {
  state_ = StartRunState::Ready;
}

bool StartState::startCountdown(const String& riderId, const String& riderName, const String& trailId, const String& trailName, String& error) {
  if (state_ != StartRunState::Ready) {
    error = "Run can start only from Ready state";
    return false;
  }

  currentRun_ = RunRecord{};
  currentRun_.runId = makeRunId();
  currentRun_.riderId = riderId;
  currentRun_.riderName = riderName.length() > 0 ? riderName : String("Test Rider");
  currentRun_.trailId = trailId;
  currentRun_.trailName = trailName.length() > 0 ? trailName : String("Трасса по умолчанию");
  currentRun_.status = "Countdown";
  countdownActive_ = true;
  countdownStepIndex_ = 0;
  countdownStepStartedMs_ = millis();
  countdownText_ = countdownStepText(countdownStepIndex_);
  state_ = StartRunState::Countdown;
  return true;
}

void StartState::resetActiveRun() {
  currentRun_ = RunRecord{};
  countdownActive_ = false;
  countdownStepIndex_ = 0;
  countdownStepStartedMs_ = 0;
  countdownText_ = "";
  state_ = StartRunState::Ready;
}

void StartState::setError() {
  state_ = StartRunState::Error;
}

bool StartState::updateCountdown(uint32_t nowMs, RunRecord& runToStart) {
  if (state_ != StartRunState::Countdown || !countdownActive_) return false;

  if (nowMs - countdownStepStartedMs_ < countdownStepDurationMs(countdownStepIndex_)) return false;

  countdownStepIndex_ += 1;
  if (countdownStepIndex_ < CountdownStepCount) {
    countdownStepStartedMs_ = nowMs;
    countdownText_ = countdownStepText(countdownStepIndex_);
    return false;
  }

  countdownActive_ = false;
  countdownText_ = "";
  currentRun_.startTimestampMs = nowMs;
  currentRun_.status = "Riding";
  state_ = StartRunState::Riding;
  runToStart = currentRun_;
  return true;
}

bool StartState::completeRun(const String& runId, uint32_t finishTimestampMs, const String& source, RunRecord& completedRun) {
  if (state_ != StartRunState::Riding || currentRun_.runId != runId) return false;

  currentRun_.finishTimestampMs = finishTimestampMs;
  currentRun_.resultMs = finishTimestampMs >= currentRun_.startTimestampMs
                           ? finishTimestampMs - currentRun_.startTimestampMs
                           : 0;
  currentRun_.resultFormatted = formatDurationMs(currentRun_.resultMs);
  currentRun_.finishSource = source;
  currentRun_.status = "Finished";
  lastRun_ = currentRun_;
  completedRun = currentRun_;
  runs_.insert(runs_.begin(), currentRun_);
  if (runs_.size() > 20) runs_.pop_back();
  finishedAtMs_ = millis();
  state_ = StartRunState::Finished;
  return true;
}

void StartState::tickAutoReady(uint32_t nowMs) {
  if (state_ == StartRunState::Finished && nowMs - finishedAtMs_ >= 5000UL) {
    currentRun_ = RunRecord{};
    state_ = StartRunState::Ready;
  }
}

String StartState::stateText() const {
  switch (state_) {
    case StartRunState::Boot: return "Boot";
    case StartRunState::Ready: return "Ready";
    case StartRunState::Countdown: return "Countdown";
    case StartRunState::Riding: return "Riding";
    case StartRunState::Finished: return "Finished";
    case StartRunState::Error: return "Error";
  }
  return "Error";
}

String StartState::countdownText(uint32_t nowMs) const {
  if (state_ != StartRunState::Countdown) return "";

  (void)nowMs;
  return countdownText_;
}

String StartState::makeRunId() const {
  return "RUN-" + String(millis(), HEX);
}

String StartState::countdownStepText(uint8_t index) const {
  switch (index) {
    case 0: return "3";
    case 1: return "2";
    case 2: return "1";
    case 3: return "GO";
    default: return "";
  }
}

uint32_t StartState::countdownStepDurationMs(uint8_t index) const {
  return index == 3 ? 700UL : 1000UL;
}
