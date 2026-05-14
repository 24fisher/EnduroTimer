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
  void updateLed(uint32_t nowMs);
  void pollRadio();
  bool sendRadio(const RadioMessage& message);
  void sendStatus();
  void sendFinish();
  void handleRadioMessage(const RadioMessage& message);
  void updateDisplay();
  void logHeartbeat(uint32_t nowMs);

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  FinishSensorStub sensor_;
  FinishState state_;

  bool oledReady_ = false;
  bool radioReady_ = false;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastFinishSendMs_ = 0;
  uint32_t heartbeatCounter_ = 0;
  uint32_t lastLedMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint8_t finishAttempts_ = 0;
  bool ledOn_ = false;
  String lastPacket_ = "-";
  int startRssi_ = 0;
  float startSnr_ = 0.0F;
  bool hasStartSignal_ = false;
  uint32_t showNoRunUntilMs_ = 0;
  uint32_t showAckOkUntilMs_ = 0;
  uint32_t finishLineCrossedUntilMs_ = 0;
};
