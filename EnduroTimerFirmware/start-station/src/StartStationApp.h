#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <RadioLib.h>
#include <vector>

#include "ButtonDebouncer.h"
#include "BuzzerStub.h"
#include "OledDisplay.h"
#include "RadioMessage.h"
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
  bool addRider(const String& displayName, String& error);
  bool deactivateRider(const String& riderId, String& error);
  bool addTrail(const String& displayName, String& error);
  bool deactivateTrail(const String& trailId, String& error);
  bool updateSettings(const String& selectedRiderId, const String& selectedTrailId, String& error);
  String runsCsv() const;

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
  void loadStorage();
  void loadRiders();
  void loadTrails();
  void loadSettings();
  void ensureDefaults();
  void saveRiders() const;
  void saveTrails() const;
  void saveSettings() const;
  void appendRunCsv(const RunRecord& run) const;
  RiderRecord selectRider() const;
  TrailRecord selectTrail() const;
  bool findActiveRider(const String& id, RiderRecord& rider) const;
  bool findActiveTrail(const String& id, TrailRecord& trail) const;
  String makeEntityId(const char* prefix) const;
  String escapeCsv(const String& value) const;

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
  int finishRssi_ = 0;
  float finishSnr_ = 0.0F;
  bool hasFinishSignal_ = false;
  int finishReportedStartRssi_ = 0;
  float finishReportedStartSnr_ = 0.0F;
  bool hasFinishReportedStartSignal_ = false;
  String lastFinishPacketType_ = "-";
  String lastLoRaRaw_ = "-";
  bool finishOnlineState_ = false;
  ButtonDebouncer startButton_;
  String lastCountdownText_;
  uint32_t lastLedMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  bool ledOn_ = false;
  std::vector<RiderRecord> riders_;
  std::vector<TrailRecord> trails_;
  AppSettings settings_;
};
