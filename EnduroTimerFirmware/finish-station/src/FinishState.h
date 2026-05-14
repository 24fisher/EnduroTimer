#pragma once

#include <Arduino.h>

enum class FinishRunState {
  Boot,
  Idle,
  Riding,
  FinishSent,
  AckTimeout,
  Error
};

class FinishState {
public:
  void begin();
  void startRun(const String& runId, uint32_t runNumber, const String& riderName, const String& trailName, uint32_t raceStartTimeMs, uint32_t localReceivedMs);
  void markFinishSent(uint32_t finishTimestampMs);
  void ackFinish();
  void fail();
  void ackTimeout();

  FinishRunState state() const { return state_; }
  bool isRiding() const { return state_ == FinishRunState::Riding; }
  bool canFinish() const { return isRiding(); }
  String stateText() const;
  const String& runId() const { return runId_; }
  uint32_t runNumber() const { return runNumber_; }
  const String& riderName() const { return riderName_; }
  const String& trailName() const { return trailName_; }
  uint32_t startTimestampMs() const { return startTimestampMs_; }
  uint32_t raceStartTimeMs() const { return startTimestampMs_; }
  uint32_t finishTimestampMs() const { return finishTimestampMs_; }
  uint32_t localRunStartReceivedMillis() const { return localRunStartReceivedMillis_; }
  uint32_t elapsedMs(uint32_t nowMs) const;

private:
  FinishRunState state_ = FinishRunState::Boot;
  String runId_;
  uint32_t runNumber_ = 0;
  String riderName_;
  String trailName_;
  uint32_t startTimestampMs_ = 0;
  uint32_t localRunStartReceivedMillis_ = 0;
  uint32_t finishTimestampMs_ = 0;
};
