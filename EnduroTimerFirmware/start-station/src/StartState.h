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
  String runId;
  String riderName;
  uint32_t startTimestampMs = 0;
  uint32_t finishTimestampMs = 0;
  uint32_t resultMs = 0;
  String resultFormatted;
  String finishSource;
  String status;
};

class StartState {
public:
  void begin();
  bool startCountdown(String& error);
  void resetActiveRun();
  bool updateCountdown(uint32_t nowMs, RunRecord& runToStart);
  bool completeRun(const String& runId, uint32_t finishTimestampMs, const String& source, RunRecord& completedRun);
  void tickAutoReady(uint32_t nowMs);

  StartRunState state() const { return state_; }
  String stateText() const;
  String countdownText(uint32_t nowMs) const;
  const RunRecord& currentRun() const { return currentRun_; }
  const RunRecord& lastRun() const { return lastRun_; }
  const std::vector<RunRecord>& runs() const { return runs_; }

private:
  String makeRunId() const;

  StartRunState state_ = StartRunState::Boot;
  RunRecord currentRun_;
  RunRecord lastRun_;
  std::vector<RunRecord> runs_;
  uint32_t countdownStartedMs_ = 0;
  uint32_t finishedAtMs_ = 0;
};
