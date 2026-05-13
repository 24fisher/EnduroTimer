#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <RadioLib.h>

#include "ButtonDebouncer.h"
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
  void beginRadio();

  bool requestStartRun(String& error);
  void resetSystem();
  void setWifiStatus(bool apStarted, const IPAddress& ip, const String& mac);
  void setWebStatus(bool webStarted);
  String statusJson() const;
  String runsJson() const;

private:
  void configureButton();
  void updateButton(uint32_t nowMs);
  void updateLed(uint32_t nowMs);
  void pollRadio();
  bool sendRadio(const RadioMessage& message);
  void sendRunStart(const RunRecord& run);
  void sendFinishAck(const String& runId);
  void handleRadioMessage(const RadioMessage& message);
  bool finishOnline() const;
  uint32_t finishLastSeenAgoMs() const;
  void updateCountdownDisplay(uint32_t nowMs);
  void updateDisplay();
  void logHeartbeat(uint32_t nowMs);

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  StartState state_;

  bool oledReady_ = false;
  bool radioReady_ = false;
  bool wifiApStarted_ = false;
  bool webStarted_ = false;
  IPAddress wifiIp_;
  String wifiMac_ = "-";
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastFinishSeenMs_ = 0;
  uint32_t finishLastStatusMs_ = 0;
  uint32_t finishHeartbeatCount_ = 0;
  String finishState_ = "UNKNOWN";
  float lastRssi_ = 0.0F;
  float lastSnr_ = 0.0F;
  ButtonDebouncer startButton_;
  String lastCountdownText_;
  uint32_t lastLedMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  bool ledOn_ = false;
};
