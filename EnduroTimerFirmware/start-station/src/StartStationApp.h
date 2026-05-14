#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <RadioLib.h>
#include <vector>

#include "ButtonDebouncer.h"
#include "BuzzerStub.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
#include "LinkStatus.h"
#include "LedIndicator.h"
#include "StartState.h"
#include "TimeUtils.h"

#ifndef START_BUTTON_PIN
#define START_BUTTON_PIN 0
#endif


struct RiderRecord {
  String id;
  String displayName;
  bool isActive = true;
  uint32_t createdAtMs = 0;
  RiderRecord() = default;
  RiderRecord(const String& idValue, const String& nameValue, bool activeValue, uint32_t createdValue)
      : id(idValue), displayName(nameValue), isActive(activeValue), createdAtMs(createdValue) {}
};

struct TrailRecord {
  String id;
  String displayName;
  bool isActive = true;
  uint32_t createdAtMs = 0;
  TrailRecord() = default;
  TrailRecord(const String& idValue, const String& nameValue, bool activeValue, uint32_t createdValue)
      : id(idValue), displayName(nameValue), isActive(activeValue), createdAtMs(createdValue) {}
};

struct AppSettings {
  String selectedRiderId;
  String selectedTrailId;
};

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
  String ridersJson() const;
  String trailsJson() const;
  String settingsJson() const;
  bool addRider(const String& displayName, String& error, RiderRecord* addedRider = nullptr);
  bool deactivateRider(const String& riderId, String& error);
  bool addTrail(const String& displayName, String& error, TrailRecord* addedTrail = nullptr);
  bool deactivateTrail(const String& trailId, String& error);
  bool updateSettings(const String& selectedRiderId, const String& selectedTrailId, String& error);
  String runsCsv() const;

private:
  void configureButton();
  void updateButton(uint32_t nowMs);
  void updateLed(uint32_t nowMs);
  String startHeader() const;
  String startShortHeader() const;
  void pollRadio();
  bool sendRadio(const RadioMessage& message, int* resultCode = nullptr);
  void restoreRadioReceiveMode();
  void sendRunStart(const RunRecord& run);
  void retryRunStartAck(uint32_t nowMs);
  bool priorityTxPending() const;
  void sendFinishAck(const String& runId);
  void sendStatus(uint32_t nowMs);
  void sendHello(uint32_t nowMs);
  void sendHelloAck(uint32_t nowMs);
  bool discoveryActive() const;
  String finishSignalText() const;
  String finishReportedStartSignalText() const;
  String makeBootId(const char* stationId) const;
  void updateFinishLink(const RadioMessage& message, int packetRssi, float packetSnr);
  uint8_t ridingAnimationFrame() const;
  void handleRadioMessage(const RadioMessage& message);
  bool finishOnline() const;
  uint32_t finishLastSeenAgoMs() const;
  void updateCountdownDisplay(uint32_t nowMs);
  void updateDisplay();
  void logHeartbeat(uint32_t nowMs);
  void loadStorage();
  void loadRiders();
  void loadTrails();
  void loadSettings();
  void ensureDefaults();
  bool saveRiders();
  bool saveTrails();
  bool saveSettings();
  bool appendRunCsv(const RunRecord& run) const;
  RiderRecord selectRider() const;
  TrailRecord selectTrail() const;
  bool findActiveRider(const String& id, RiderRecord& rider) const;
  bool findActiveTrail(const String& id, TrailRecord& trail) const;
  String makeEntityId(const char* prefix) const;
  String escapeCsv(const String& value) const;

  ClockService clock_;
  OledDisplay display_;
  BuzzerStub buzzer_;
  LedIndicator led_;
  StartState state_;

  bool oledReady_ = false;
  bool radioReady_ = false;
  bool wifiApStarted_ = false;
  bool webStarted_ = false;
  IPAddress wifiIp_;
  String wifiMac_ = "-";
  uint32_t lastDisplayMs_ = 0;
  LinkStatus finishLink_;
  uint32_t finishLastStatusMs_ = 0;
  uint32_t finishHeartbeatCount_ = 0;
  uint32_t startHeartbeatCount_ = 0;
  uint32_t lastStatusSendMs_ = 0;
  uint32_t lastStatusSentOkMs_ = 0;
  uint32_t lastDiscoverySentMs_ = 0;
  uint32_t lastHelloReceivedMs_ = 0;
  uint32_t lastPriorityTxMs_ = 0;
  bool pendingRunStartAck_ = false;
  bool runStartAckReceived_ = false;
  bool runStartAckTimedOut_ = false;
  uint8_t runStartAckAttempts_ = 0;
  uint32_t lastRunStartSendMs_ = 0;
  uint32_t lastRunStartAckMs_ = 0;
  uint32_t lastAnyPacketMs_ = 0;
  String finishState_ = "Unknown";
  String bootId_;
  String finishFirmwareVersion_;
  String finishActiveRunId_;
  String finishRiderName_;
  uint32_t finishElapsedMs_ = 0;
  int finishReportedStartRssi_ = 0;
  float finishReportedStartSnr_ = 0.0F;
  uint32_t finishReportedStartLastSeenAgoMs_ = 0;
  bool hasFinishReportedStartSignal_ = false;
  bool finishReportedStartLinkActive_ = false;
  uint32_t finishReportedStartPacketCount_ = 0;
  String lastFinishPacketType_ = "-";
  String lastLoRaRaw_ = "-";
  int lastRssi_ = 0;
  float lastSnr_ = 0.0F;
  ButtonDebouncer startButton_;
  String lastCountdownText_;
  uint32_t lastHeartbeatMs_ = 0;
  std::vector<RiderRecord> riders_;
  std::vector<TrailRecord> trails_;
  AppSettings settings_;
};
