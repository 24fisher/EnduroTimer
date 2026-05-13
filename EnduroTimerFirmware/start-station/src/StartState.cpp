#include "StartState.h"

void StartState::begin() {
  state_ = StartRunState::Ready;
}

bool StartState::startCountdown(String& error) {
  if (state_ != StartRunState::Ready) {
    error = "Run can start only from Ready state";
    return false;
  }

  currentRun_ = RunRecord{};
  currentRun_.runId = makeRunId();
  currentRun_.riderName = "Test Rider";
  currentRun_.status = "Countdown";
  countdownStartedMs_ = millis();
  state_ = StartRunState::Countdown;
  return true;
}

void StartState::resetActiveRun() {
  currentRun_ = RunRecord{};
  countdownStartedMs_ = 0;
  state_ = StartRunState::Ready;
}

void StartState::setError() {
  state_ = StartRunState::Error;
}

bool StartState::updateCountdown(uint32_t nowMs, RunRecord& runToStart) {
  if (state_ != StartRunState::Countdown) return false;

  if (nowMs - countdownStartedMs_ >= 4000UL) {
    currentRun_.startTimestampMs = nowMs;
    currentRun_.status = "Riding";
    state_ = StartRunState::Riding;
    runToStart = currentRun_;
    return true;
  }

  return false;
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
  if (state_ == StartRunState::Finished && nowMs - finishedAtMs_ >= 3000UL) {
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

  const uint32_t elapsed = nowMs - countdownStartedMs_;
  if (elapsed < 1000UL) return "3";
  if (elapsed < 2000UL) return "2";
  if (elapsed < 3000UL) return "1";
  return "GO";
}

String StartState::makeRunId() const {
  return "RUN-" + String(millis(), HEX);
}
