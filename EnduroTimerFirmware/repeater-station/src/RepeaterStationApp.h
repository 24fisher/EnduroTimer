#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <array>

#include "BatteryMonitor.h"
#include "LinkStatus.h"
#include "LoopMonitor.h"
#include "OledDisplay.h"
#include "RadioMessage.h"

class RepeaterStationApp {
public:
  void begin();
  void loop();

private:
  struct SeenMessage { String mid; uint32_t seenMs = 0; };
  struct RelayPacket {
    String payload;
    String mid;
    String type;
    String src;
    String dst;
    uint8_t priority = 2;
    uint8_t hop = 0;
    uint32_t enqueueMs = 0;
  };

  void beginRadio();
  void pollRadioNonBlocking();
  void restoreRadioReceiveMode();
  void handlePacket(const String& payload, int rssi, float snr);
  bool relayable(RadioMessageType type) const;
  uint8_t priorityFor(RadioMessageType type) const;
  bool seenRecently(const String& mid, uint32_t nowMs);
  void remember(const String& mid, uint32_t nowMs);
  bool enqueueRelay(const String& payload, const RadioMessage& message, uint8_t priority, uint8_t hop);
  void processRelayQueue();
  void updateDisplay();
  void logCounters(uint32_t nowMs);
  String rssiText(const char* label, bool seen, int rssi) const;

  OledDisplay display_;
  BatteryMonitor battery_;
  LoopMonitor loopMonitor_;
  bool oledReady_ = false;
  bool radioReady_ = false;
  std::array<SeenMessage, 64> seen_;
  uint8_t seenCursor_ = 0;
  std::array<RelayPacket, 8> queue_;
  uint32_t rxCount_ = 0;
  uint32_t txCount_ = 0;
  uint32_t duplicateCount_ = 0;
  uint32_t hopLimitDrops_ = 0;
  uint32_t queueDrops_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastCountersMs_ = 0;
  uint32_t lastCriticalRelayMs_ = 0;
  uint32_t radioPollLastDurationMs_ = 0;
  uint32_t radioPollMaxDurationMs_ = 0;
  bool startSeen_ = false;
  bool finishSeen_ = false;
  int startRssi_ = 0;
  int finishRssi_ = 0;
  String lastCriticalType_ = "--";
};
