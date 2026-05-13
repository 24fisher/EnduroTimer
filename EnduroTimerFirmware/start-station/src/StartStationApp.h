#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "BuzzerStub.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
#include "StartState.h"
#include "TimeUtils.h"

class StartStationApp {
public:
  void begin();
  void loop();

  bool requestStartRun(String& error);
  void resetSystem();
  String statusJson() const;
  String runsJson() const;

private:
  void beginRadio();
  void pollRadio();
  bool sendRadio(const RadioMessage& message);
  void sendRunStart(const RunRecord& run);
  void sendFinishAck(const String& runId);
  void handleRadioMessage(const RadioMessage& message);
  void updateDisplay();

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  StartState state_;

  bool radioReady_ = false;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastFinishSeenMs_ = 0;
  String finishState_ = "UNKNOWN";
  float lastRssi_ = 0.0F;
  float lastSnr_ = 0.0F;
};
