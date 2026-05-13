#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "BuzzerStub.h"
#include "FinishSensorStub.h"
#include "FinishState.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
#include "TimeUtils.h"

class FinishStationApp {
public:
  void begin();
  void loop();

private:
  void beginRadio();
  void pollRadio();
  bool sendRadio(const RadioMessage& message);
  void sendStatus();
  void sendFinish();
  void handleRadioMessage(const RadioMessage& message);
  void updateDisplay();

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  FinishSensorStub sensor_;
  FinishState state_;

  bool radioReady_ = false;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastFinishSendMs_ = 0;
  uint8_t finishAttempts_ = 0;
  String lastPacket_ = "-";
};
