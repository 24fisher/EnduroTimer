#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <RadioLib.h>

#include "BuzzerStub.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
#include "StartState.h"
#include "TimeUtils.h"

#ifndef START_BUTTON_PIN
#define START_BUTTON_PIN 0
#endif

class StartStationApp {
public:
  void begin();
  void loop();

  bool requestStartRun(String& error);
  void resetSystem();
  void setWifiStatus(bool apStarted, const IPAddress& ip, const String& mac);
  String statusJson() const;
  String runsJson() const;

private:
  void beginRadio();
  void configureButton();
  void updateButton(uint32_t nowMs);
  void updateLed(uint32_t nowMs);
  void pollRadio();
  bool sendRadio(const RadioMessage& message);
  void sendRunStart(const RunRecord& run);
  void sendFinishAck(const String& runId);
  void handleRadioMessage(const RadioMessage& message);
  bool finishOnline() const;
  void updateDisplay();
  void logHeartbeat(uint32_t nowMs);

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  StartState state_;

  bool radioReady_ = false;
  bool wifiApStarted_ = false;
  IPAddress wifiIp_;
  String wifiMac_ = "-";
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastFinishSeenMs_ = 0;
  String finishState_ = "UNKNOWN";
  float lastRssi_ = 0.0F;
  float lastSnr_ = 0.0F;
  int buttonStableState_ = HIGH;
  int buttonLastReading_ = HIGH;
  uint32_t buttonLastChangeMs_ = 0;
  bool buttonPressConsumed_ = false;
  uint32_t lastLedMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  bool ledOn_ = false;
};
