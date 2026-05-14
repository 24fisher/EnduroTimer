#include "StartState.h"

void StartState::begin() {
  state_ = StartRunState::Ready;
}

bool StartState::startCountdown(uint32_t runNumber, const String& startedAtText, uint64_t startedAtEpochMs, const String& riderId, const String& riderName, const String& trailId, const String& trailName, String& error) {
  if (state_ != StartRunState::Ready) {
    error = "Run can start only from Ready state";
    return false;
  }

  currentRun_ = RunRecord{};
  currentRun_.runNumber = runNumber;
  currentRun_.runId = makeRunId();
  currentRun_.riderId = riderId;
  currentRun_.riderName = riderName.length() > 0 ? riderName : String("Test Rider");
  currentRun_.trailId = trailId;
  currentRun_.trailName = trailName.length() > 0 ? trailName : String("Трасса по умолчанию");
  currentRun_.startedAtEpochMs = startedAtEpochMs;
  currentRun_.startedAtText = startedAtText.length() > 0 ? startedAtText : String("TIME NOT SYNCED");
  currentRun_.timingSource = "PENDING";
  currentRun_.timingNote = "Start timestamp will be fixed on GO; countdown is not part of result";
  currentRun_.status = "Countdown";
  countdownActive_ = true;
  countdownStepIndex_ = 0;
  countdownStartedMs_ = millis();
  countdownStepStartedMs_ = countdownStartedMs_;
  goTimestampMs_ = 0;
  countdownText_ = countdownStepText(countdownStepIndex_);
  state_ = StartRunState::Countdown;
  Serial.printf("New run created runNumber=%lu runId=%s\n", static_cast<unsigned long>(currentRun_.runNumber), currentRun_.runId.c_str());
  Serial.printf("New run startedAtText=%s\n", currentRun_.startedAtText.c_str());
  return true;
}

void StartState::resetActiveRun() {
  currentRun_ = RunRecord{};
  countdownActive_ = false;
  countdownStepIndex_ = 0;
  countdownStartedMs_ = 0;
  countdownStepStartedMs_ = 0;
  goTimestampMs_ = 0;
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
  goTimestampMs_ = nowMs;
  currentRun_.startTimestampMs = nowMs;
  currentRun_.timingSource = "RUNNING";
  currentRun_.timingNote = "Countdown excluded; raceStartTimeMs will be fixed from synced RaceClock";
  currentRun_.status = "Riding";
  state_ = StartRunState::Riding;
  runToStart = currentRun_;
  return true;
}

void StartState::setRaceStartTime(uint32_t raceStartTimeMs, uint32_t syncAccuracyMs) {
  currentRun_.raceStartTimeMs = raceStartTimeMs;
  currentRun_.startTimestampMs = raceStartTimeMs;
  currentRun_.syncAccuracyMs = syncAccuracyMs;
  currentRun_.timingSource = "SYNCED_RACE_CLOCK";
  currentRun_.timingNote = "Sport result uses synced relative RaceClock; browser time is stats only";
}

bool StartState::completeRun(const String& runId, uint32_t finishTimestampMs, const String& source, RunRecord& completedRun) {
  if (state_ != StartRunState::Riding || currentRun_.runId != runId) return false;

  currentRun_.finishSource = source;
  if (finishTimestampMs > 0) {
    currentRun_.finishTimestampMs = finishTimestampMs;
    currentRun_.resultMs = finishTimestampMs >= currentRun_.startTimestampMs
                             ? finishTimestampMs - currentRun_.startTimestampMs
                             : 0;
    currentRun_.timingSource = "FINISH_LOCAL_ELAPSED";
    currentRun_.timingNote = "No RTC; result based on FinishStation elapsed since RUN_START receipt";
  } else {
    currentRun_.finishTimestampMs = millis();
    currentRun_.resultMs = currentRun_.finishTimestampMs >= currentRun_.startTimestampMs
                             ? currentRun_.finishTimestampMs - currentRun_.startTimestampMs
                             : 0;
    currentRun_.finishSource = "START_RECEIVE_FALLBACK";
    currentRun_.timingSource = "START_RECEIVE_FALLBACK";
    currentRun_.timingNote = "Finish timestamp missing; result based on StartStation FINISH receive time";
  }
  currentRun_.resultFormatted = formatSeconds(currentRun_.resultMs);
  currentRun_.status = "Finished";
  lastRun_ = currentRun_;
  completedRun = currentRun_;
  runs_.insert(runs_.begin(), currentRun_);
  if (runs_.size() > 20) runs_.pop_back();
  finishedAtMs_ = millis();
  state_ = StartRunState::Finished;
  return true;
}

bool StartState::completeRunSynced(const String& runId, uint32_t finishRaceTimeMs, uint32_t resultMs, const String& source, uint32_t syncAccuracyMs, RunRecord& completedRun) {
  if (state_ != StartRunState::Riding || currentRun_.runId != runId) return false;

  currentRun_.finishSource = source;
  currentRun_.finishRaceTimeMs = finishRaceTimeMs;
  currentRun_.finishTimestampMs = finishRaceTimeMs;
  currentRun_.resultMs = resultMs > 0 ? resultMs : (finishRaceTimeMs >= currentRun_.raceStartTimeMs ? finishRaceTimeMs - currentRun_.raceStartTimeMs : 0);
  currentRun_.resultFormatted = formatSeconds(currentRun_.resultMs);
  currentRun_.timingSource = "SYNCED_RACE_CLOCK";
  currentRun_.timingNote = "Result accepted from FinishStation synced RaceClock; FINISH receive time is not used";
  currentRun_.syncAccuracyMs = syncAccuracyMs;
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
  if (state_ == StartRunState::Finished && nowMs - finishedAtMs_ >= 8000UL) {
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
