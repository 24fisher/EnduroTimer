#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "BatteryService.h"
#include "BuzzerStub.h"
#include "FinishSensorStub.h"
#include "FinishState.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
#include "LinkStatus.h"
#include "LedIndicator.h"
#include "TimeUtils.h"
#include "RaceClock.h"
#include "LoopMonitor.h"

class FinishStationApp {
public:
  void begin();
  void loop();

private:
  void beginRadio();
  void updateLed(uint32_t nowMs);
  String finishHeader() const;
  String startSignalText() const;
  void pollRadio();
  bool sendRadio(const RadioMessage& message, int* resultCode = nullptr);
  void restoreRadioReceiveMode();
  void sendStatus(uint32_t nowMs);
  void sendHello(uint32_t nowMs);
  void sendHelloAck(uint32_t nowMs);
  void sendRunStartAck(const RadioMessage& runStart);
#if ENABLE_LORA_TIME_SYNC
  void sendSyncRequest(uint32_t nowMs);
  void sendSyncPong(const RadioMessage& ping);
  void sendSyncAck(const RadioMessage& apply);
  void enterSyncReady(uint32_t nowMs);
#endif
  void beginWifi();
  void updateWifiSync(uint32_t nowMs);
  bool takeWifiSyncSample(uint8_t sampleNumber, uint32_t& rttMs, int32_t& offsetMs, String& startBootId);
  void finishWifiSyncSuccess(int32_t offsetMs, uint32_t rttMs, const String& startBootId);
  bool postFinishSyncStatus();
  bool discoveryActive() const;
  bool priorityTxPending(uint32_t nowMs) const;
  void acceptFinishButton(uint32_t nowMs);
  void handleFinishButton(uint32_t nowMs);
  void sendFinish();
  void resendFinishFromButton(uint32_t nowMs);
  uint8_t ridingAnimationFrame() const;
  void handleRadioMessage(const RadioMessage& message);
  void updateStartLink(const RadioMessage& message, int packetRssi, float packetSnr);
  void updateDisplay();
  void logHeartbeat(uint32_t nowMs);
  String makeBootId(const char* stationId) const;

  ClockService clock_;
  RaceClock raceClock_;
  BatteryService battery_;
  LoopMonitor loopMonitor_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  LedIndicator led_;
  FinishSensorStub sensor_;
  FinishState state_;

  bool oledReady_ = false;
  bool radioReady_ = false;
  uint32_t lastStatusMs_ = 0;
  uint32_t nextFinishStatusDueMs_ = 0;
  uint32_t lastDiscoverySentMs_ = 0;
  uint32_t lastHelloReceivedMs_ = 0;
  uint32_t lastStatusSentOkMs_ = 0;
  uint32_t lastAnyPacketMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastPriorityTxMs_ = 0;
  uint32_t lastFinishSendMs_ = 0;
  uint32_t heartbeatCounter_ = 0;
  uint32_t startHeartbeatCount_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  String bootId_;
  uint8_t finishAttempts_ = 0;
  uint8_t manualResendCount_ = 0;
  bool finishAckReceived_ = false;
  String lastFinishedRunId_;
  uint32_t lastResultMs_ = 0;
  String lastResultFormatted_;
  uint32_t localResultMs_ = 0;
  uint32_t finishLocalElapsedMs_ = 0;
  uint32_t remoteStartTimestampMs_ = 0;
  String lastPacket_ = "-";
  String lastRunStartAckRunId_;
  uint32_t lastRunStartRxMs_ = 0;
  uint32_t lastStatusRidingWarnMs_ = 0;
  String lastLoRaRaw_ = "-";
  int lastRssi_ = 0;
  float lastSnr_ = 0.0F;
  LinkStatus startLink_;
  String startState_ = "Unknown";
  bool syncReady_ = false;
  bool syncInProgress_ = false;
  String activeSyncId_;
  uint32_t syncReadyUntilMs_ = 0;
  uint32_t syncAccuracyMs_ = 0;
  uint32_t lastSyncMs_ = 0;
  String syncStatusText_ = "WIFI SEARCH";
  bool wifiStarted_ = false;
  bool wifiConnected_ = false;
  bool syncDoneOnce_ = false;
  uint32_t nextWifiConnectAttemptMs_ = 0;
  uint32_t nextWifiSyncAttemptMs_ = 0;
  uint8_t wifiSyncSampleIndex_ = 0;
  uint32_t bestWifiSyncRttMs_ = UINT32_MAX;
  int32_t bestWifiSyncOffsetMs_ = 0;
  String startHttpBootId_ = "";
  String wifiStatusText_ = "WIFI SEARCH";
  uint32_t showNoRunUntilMs_ = 0;
  uint32_t showAckOkUntilMs_ = 0;
  uint32_t finishLineCrossedUntilMs_ = 0;
};
