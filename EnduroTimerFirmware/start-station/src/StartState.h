#pragma once

#include <Arduino.h>
#include <vector>

#include "TimeUtils.h"

enum class StartRunState {
  Boot,
  Ready,
  Countdown,
  Riding,
  Finished,
  Error
};

struct RunRecord {
  uint32_t runNumber = 0;
  String runId;
  String riderId;
  String riderName;
  String trailId;
  String trailName;
  uint64_t startedAtEpochMs = 0;
  String startedAtText;
  uint32_t startTimestampMs = 0;
  uint32_t finishTimestampMs = 0;
  uint32_t resultMs = 0;
  String resultFormatted;
  String timingSource;
  String timingNote;
  String finishSource;
  String status;
};

class StartState {
public:
  void begin();
  bool startCountdown(uint32_t runNumber, const String& startedAtText, uint64_t startedAtEpochMs, const String& riderId, const String& riderName, const String& trailId, const String& trailName, String& error);
  void resetActiveRun();
  void setError();
  bool updateCountdown(uint32_t nowMs, RunRecord& runToStart);
  bool completeRun(const String& runId, uint32_t finishTimestampMs, const String& source, RunRecord& completedRun);
  void tickAutoReady(uint32_t nowMs);

  StartRunState state() const { return state_; }
  String stateText() const;
  String countdownText(uint32_t nowMs) const;
  const RunRecord& currentRun() const { return currentRun_; }
  const RunRecord& lastRun() const { return lastRun_; }
  const std::vector<RunRecord>& runs() const { return runs_; }
  uint32_t countdownStartedMs() const { return countdownStartedMs_; }
  uint32_t goTimestampMs() const { return goTimestampMs_; }

private:
  String makeRunId() const;

  StartRunState state_ = StartRunState::Boot;
  RunRecord currentRun_;
  RunRecord lastRun_;
  std::vector<RunRecord> runs_;
  static constexpr uint8_t CountdownStepCount = 4;
  String countdownStepText(uint8_t index) const;
  uint32_t countdownStepDurationMs(uint8_t index) const;

  bool countdownActive_ = false;
  uint8_t countdownStepIndex_ = 0;
  uint32_t countdownStartedMs_ = 0;
  uint32_t countdownStepStartedMs_ = 0;
  uint32_t goTimestampMs_ = 0;
  String countdownText_;
  uint32_t finishedAtMs_ = 0;
};
