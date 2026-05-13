#pragma once

#include <Arduino.h>

enum class FinishRunState {
  Boot,
  Idle,
  WaitFinish,
  FinishSent,
  Error
};

class FinishState {
public:
  void begin();
  void startRun(const String& runId, uint32_t startTimestampMs);
  void markFinishSent(uint32_t finishTimestampMs);
  void ackFinish();
  void fail();

  FinishRunState state() const { return state_; }
  String stateText() const;
  const String& runId() const { return runId_; }
  uint32_t startTimestampMs() const { return startTimestampMs_; }
  uint32_t finishTimestampMs() const { return finishTimestampMs_; }

private:
  FinishRunState state_ = FinishRunState::Boot;
  String runId_;
  uint32_t startTimestampMs_ = 0;
  uint32_t finishTimestampMs_ = 0;
};
