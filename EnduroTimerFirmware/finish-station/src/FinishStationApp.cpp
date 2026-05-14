#include "FinishStationApp.h"

#include <SPI.h>
#include <esp_system.h>
#if ENABLE_WIFI
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#endif

#include "RadioProtocol.h"
#include "DisplayText.h"
#include "BuildConfig.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint8_t FINISH_MAX_RETRY_ATTEMPTS = 15;
static constexpr uint32_t FINISH_RETRY_INTERVAL_MS = 1000UL;
static constexpr uint32_t DisplayRefreshMs = 200UL;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 2000UL;
static constexpr uint8_t WIFI_SYNC_SAMPLE_COUNT = 5;
RTC_DATA_ATTR static uint32_t finishBootCounter = 0;
#if ENABLE_LORA
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
#endif

void FinishStationApp::begin() {
  finishBootCounter += 1;

  Serial.println("Firmware: EnduroTimer FinishStation");
  Serial.println("Version: v" FIRMWARE_VERSION);
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Board/role: Heltec WiFi LoRa 32 V3 / FinishStation");
  bootId_ = makeBootId("finish");
  Serial.printf("Boot counter: %lu\n", static_cast<unsigned long>(finishBootCounter));
  Serial.printf("BootId: %s\n", bootId_.c_str());
  Serial.printf("Chip: %s rev %u cores=%u efuseMac=%llX\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getEfuseMac());
  Serial.println("Power save: disabled");

#if ENABLE_OLED
  Serial.println("[BOOT] OLED init...");
  oledReady_ = display_.begin();
  Serial.println(oledReady_ ? "[BOOT] OLED OK" : "[BOOT] OLED FAIL");
  if (oledReady_) {
    display_.showBootScreen(finishHeader());
  }
#else
  Serial.println("[BOOT] OLED init skipped (ENABLE_OLED=0)");
  oledReady_ = false;
#endif

  Serial.println("[BOOT] Finish button init...");
  sensor_.begin();
  Serial.println("[BOOT] Finish button OK");
  buzzer_.begin();
  battery_.begin();
  led_.begin();
  beginRadio();
  state_.begin();
  raceClock_.begin();
  beginWifi();
  nextFinishStatusDueMs_ = millis() + 500UL + static_cast<uint32_t>(random(0, 301));
  Serial.println("[BOOT] State Idle");
}

void FinishStationApp::loop() {
  const uint32_t now = clock_.nowMs();
#if ENABLE_LORA
  pollRadio();
#endif
  updateLed(now);
  updateWifiSync(now);
#if ENABLE_LORA_TIME_SYNC
  if (syncInProgress_ && syncReadyUntilMs_ > 0 && now > syncReadyUntilMs_ && !raceClock_.isSynced()) {
    syncInProgress_ = false;
    syncReady_ = false;
    activeSyncId_ = "";
    syncReadyUntilMs_ = 0;
    syncStatusText_ = "SYNC REQUIRED";
    Serial.println("SYNC ready timeout, waiting for new request");
  }
#endif

  uint32_t finishTimestampMs = 0;
  if (sensor_.update(now, finishTimestampMs)) {
    (void)finishTimestampMs;
    Serial.println("FINISH button debounced short press");
    Serial.printf("Finish button state=%s canFinish=%d activeRunId=%s\n", state_.stateText().c_str(), state_.canFinish() ? 1 : 0, state_.runId().c_str());
    handleFinishButton(now);
  }

  bool prioritySentThisCycle = false;
  if (!finishAckReceived_ && state_.state() == FinishRunState::FinishSent && finishAttempts_ < FINISH_MAX_RETRY_ATTEMPTS &&
      now - lastFinishSendMs_ >= FINISH_RETRY_INTERVAL_MS) {
    sendFinish();
    prioritySentThisCycle = true;
  }

  if (!finishAckReceived_ && state_.state() == FinishRunState::FinishSent && finishAttempts_ >= FINISH_MAX_RETRY_ATTEMPTS &&
      now - lastFinishSendMs_ >= FINISH_RETRY_INTERVAL_MS) {
    Serial.printf("FINISH_ACK timeout: sent %u/%u.\n", finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS);
    state_.ackTimeout();
  }

  const bool priorityPending = prioritySentThisCycle || priorityTxPending(now);
  if (discoveryActive() && now - lastDiscoverySentMs_ >= LINK_DISCOVERY_INTERVAL_MS) {
    if (priorityPending) {
      Serial.println("TX deferred HELLO because priority message pending");
    } else {
      sendHello(now);
      lastDiscoverySentMs_ = now;
    }
  }

  if (nextFinishStatusDueMs_ > 0 && now >= nextFinishStatusDueMs_) {
    if (priorityPending) {
      Serial.println("TX deferred STATUS because priority message pending");
      nextFinishStatusDueMs_ = now + 500UL;
    } else {
      sendStatus(now);
      lastStatusMs_ = now;
      nextFinishStatusDueMs_ = now + FINISH_STATUS_INTERVAL_MS + static_cast<uint32_t>(random(0, 301));
    }
  }

#if ENABLE_OLED
  display_.update();
  if (!display_.testPatternOnly() && now - lastDisplayMs_ >= DisplayRefreshMs) {
    updateDisplay();
    lastDisplayMs_ = now;
  }
#endif

  logHeartbeat(now);
}


void FinishStationApp::beginWifi() {
#if ENABLE_WIFI
  Serial.println("WiFi STA starting...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  wifiStarted_ = true;
  nextWifiConnectAttemptMs_ = 0;
#else
  Serial.println("[BOOT] WiFi STA init skipped (ENABLE_WIFI=0)");
#endif
}

void FinishStationApp::updateWifiSync(uint32_t nowMs) {
#if ENABLE_WIFI
  if (!wifiStarted_ || syncDoneOnce_) return;

  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected_ = false;
    if (nowMs >= nextWifiConnectAttemptMs_) {
      if (nextWifiConnectAttemptMs_ > 0) Serial.println("WiFi connect failed, retry...");
      Serial.println("Connecting to EnduroTimer...");
      WiFi.disconnect(false);
      WiFi.begin("EnduroTimer", "endurotimer");
      nextWifiConnectAttemptMs_ = nowMs + WIFI_RETRY_INTERVAL_MS;
      wifiStatusText_ = "WIFI SEARCH";
    }
    return;
  }

  if (!wifiConnected_) {
    wifiConnected_ = true;
    Serial.printf("WiFi connected ip=%s\n", WiFi.localIP().toString().c_str());
    wifiStatusText_ = "WIFI OK";
    nextWifiSyncAttemptMs_ = nowMs;
    wifiSyncSampleIndex_ = 0;
    bestWifiSyncRttMs_ = UINT32_MAX;
  }

  if (nowMs < nextWifiSyncAttemptMs_) return;
  if (wifiSyncSampleIndex_ >= WIFI_SYNC_SAMPLE_COUNT) return;

  uint32_t rttMs = 0;
  int32_t offsetMs = 0;
  String startBootId;
  const uint8_t sampleNumber = wifiSyncSampleIndex_ + 1;
  if (!takeWifiSyncSample(sampleNumber, rttMs, offsetMs, startBootId)) {
    wifiSyncSampleIndex_ = 0;
    bestWifiSyncRttMs_ = UINT32_MAX;
    nextWifiSyncAttemptMs_ = nowMs + WIFI_RETRY_INTERVAL_MS;
    return;
  }

  if (rttMs < bestWifiSyncRttMs_) {
    bestWifiSyncRttMs_ = rttMs;
    bestWifiSyncOffsetMs_ = offsetMs;
    startHttpBootId_ = startBootId;
  }
  wifiSyncSampleIndex_ += 1;

  if (wifiSyncSampleIndex_ >= WIFI_SYNC_SAMPLE_COUNT) {
    if (bestWifiSyncRttMs_ <= 200UL) {
      finishWifiSyncSuccess(bestWifiSyncOffsetMs_, bestWifiSyncRttMs_, startHttpBootId_);
    } else if (bestWifiSyncRttMs_ > 1000UL) {
      Serial.printf("WIFI SYNC bad rtt=%lu, retry...\n", static_cast<unsigned long>(bestWifiSyncRttMs_));
      wifiSyncSampleIndex_ = 0;
      bestWifiSyncRttMs_ = UINT32_MAX;
      nextWifiSyncAttemptMs_ = nowMs + WIFI_RETRY_INTERVAL_MS;
    } else {
      finishWifiSyncSuccess(bestWifiSyncOffsetMs_, bestWifiSyncRttMs_, startHttpBootId_);
    }
  }
#else
  (void)nowMs;
#endif
}

bool FinishStationApp::takeWifiSyncSample(uint8_t sampleNumber, uint32_t& rttMs, int32_t& offsetMs, String& startBootId) {
#if ENABLE_WIFI
  HTTPClient http;
  const uint32_t t0FinishLocalMs = millis();
  http.begin("http://192.168.4.1/api/time/race-sync");
  const int code = http.GET();
  const uint32_t t3FinishLocalMs = millis();
  if (code != HTTP_CODE_OK) {
    Serial.printf("WIFI SYNC sample %u HTTP failed code=%d\n", sampleNumber, code);
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, body);
  if (jsonError) {
    Serial.printf("WIFI SYNC sample %u JSON failed error=%s\n", sampleNumber, jsonError.c_str());
    return false;
  }

  const uint32_t tStartRaceMs = doc["tStartRaceMs"] | 0;
  startBootId = doc["bootId"] | "";
  rttMs = t3FinishLocalMs - t0FinishLocalMs;
  const uint32_t localMiddleMs = t0FinishLocalMs + (rttMs / 2UL);
  offsetMs = static_cast<int32_t>(tStartRaceMs) - static_cast<int32_t>(localMiddleMs);
  Serial.printf("WIFI SYNC sample %u rtt=%lu offset=%ld\n", sampleNumber, static_cast<unsigned long>(rttMs), static_cast<long>(offsetMs));
#if ENABLE_OLED
  if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "WIFI SYNC", "sample " + String(sampleNumber) + "/5"});
#endif
  return true;
#else
  (void)sampleNumber; (void)rttMs; (void)offsetMs; (void)startBootId;
  return false;
#endif
}

void FinishStationApp::finishWifiSyncSuccess(int32_t offsetMs, uint32_t rttMs, const String& startBootId) {
  const uint32_t accuracyMs = rttMs / 2UL;
  raceClock_.setOffsetToMaster(offsetMs);
  raceClock_.markSynced(accuracyMs);
  syncAccuracyMs_ = accuracyMs;
  lastSyncMs_ = millis();
  syncDoneOnce_ = true;
  syncStatusText_ = "SYNCED";
  startHttpBootId_ = startBootId;
  Serial.printf("WIFI SYNC best rtt=%lu offset=%ld accuracy=%lu\n", static_cast<unsigned long>(rttMs), static_cast<long>(offsetMs), static_cast<unsigned long>(accuracyMs));
  Serial.printf("RaceClock synced once offset=%ld accuracy=%lu\n", static_cast<long>(offsetMs), static_cast<unsigned long>(accuracyMs));
  postFinishSyncStatus();
#if ENABLE_OLED
  if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "SYNCED", "READY", "acc: " + String(syncAccuracyMs_) + "ms"});
#endif
}

bool FinishStationApp::postFinishSyncStatus() {
#if ENABLE_WIFI
  if (WiFi.status() != WL_CONNECTED) return false;
  JsonDocument doc;
  doc["stationId"] = "finish";
  doc["version"] = FIRMWARE_VERSION;
  doc["bootId"] = bootId_;
  doc["raceClockSynced"] = raceClock_.isSynced();
  doc["syncDoneOnce"] = syncDoneOnce_;
  doc["offsetToMasterMs"] = raceClock_.offsetToMasterMs();
  doc["syncAccuracyMs"] = syncAccuracyMs_;
  doc["finishIp"] = WiFi.localIP().toString();
  doc["uptimeMs"] = millis();
  String body;
  serializeJson(doc, body);
  HTTPClient http;
  http.begin("http://192.168.4.1/api/finish/sync-status");
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  http.end();
  Serial.printf("Finish sync-status POST code=%d\n", code);
  return code >= 200 && code < 300;
#else
  return false;
#endif
}

void FinishStationApp::beginRadio() {
#if ENABLE_LORA
  Serial.println("[BOOT] LoRa init...");
  Serial.printf("LoRa init... %.0f MHz\n", LORA_FREQUENCY_MHZ);
  SPI.begin(9, 11, 10, LORA_NSS);
  const int initState = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = initState == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("LoRa FAIL code=%d\n", initState);
    Serial.println("[BOOT] LoRa FAIL");
#if ENABLE_OLED
    if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "LoRa FAIL", "code=" + String(initState)});
#endif
    return;
  }

  radio.setBandwidth(125.0);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setOutputPower(14);
  Serial.printf("LoRa OK freq=%.1f\n", static_cast<double>(LORA_FREQUENCY_MHZ));
  Serial.println("[BOOT] LoRa OK");
#else
  Serial.println("[BOOT] LoRa init skipped (ENABLE_LORA=0)");
  radioReady_ = false;
#endif
}

void FinishStationApp::updateLed(uint32_t nowMs) {
  (void)nowMs;
  LedMode mode = LedMode::ReadySlowBlink;
  switch (state_.state()) {
    case FinishRunState::Boot:
    case FinishRunState::Idle:
      mode = (isLinkActive(startLink_) || (syncInProgress_ && linkAgeMs(startLink_) < LINK_TIMEOUT_MS)) ? LedMode::ReadySlowBlink : LedMode::NoSignalBlink;
      break;
    case FinishRunState::Riding:
      mode = LedMode::RidingSolid;
      break;
    case FinishRunState::FinishSent:
      mode = LedMode::AckTimeoutBlink;
      break;
    case FinishRunState::AckTimeout:
      mode = LedMode::AckTimeoutBlink;
      break;
    case FinishRunState::Error:
      mode = LedMode::ErrorFastBlink;
      break;
  }
  led_.setMode(mode);
  led_.update();
}

String FinishStationApp::finishHeader() const {
  return String("FINISH TERM v") + FIRMWARE_VERSION;
}


void FinishStationApp::pollRadio() {
#if ENABLE_LORA
  if (!radioReady_) return;

  String payload;
  const int rxState = radio.receive(payload, 0);
  if (rxState == RADIOLIB_ERR_NONE) {
    lastAnyPacketMs_ = millis();
    lastLoRaRaw_ = payload;
    if (lastLoRaRaw_.length() > 96) lastLoRaRaw_ = lastLoRaRaw_.substring(0, 96);
    lastRssi_ = static_cast<int>(radio.getRSSI());
    lastSnr_ = radio.getSNR();
    RadioMessage message;
    String error;
    if (!RadioProtocol::deserialize(payload, message, &error)) {
      lastPacket_ = "PARSE_FAILED";
      Serial.printf("LORA parse failed raw=%s error=%s\n", payload.c_str(), error.c_str());
      return;
    }
    lastPacket_ = RadioProtocol::typeToString(message.type);
    Serial.printf("LORA RX raw=%s\n", lastLoRaRaw_.c_str());
    Serial.printf("LORA RX type=%s rssi=%d snr=%.1f raw=%s\n", lastPacket_.c_str(), lastRssi_, static_cast<double>(lastSnr_), lastLoRaRaw_.c_str());
    Serial.printf("LORA parsed type=%s stationId=%s runId=%s raceStartTimeMs=%lu hb=%lu\n", lastPacket_.c_str(), message.stationId.c_str(), message.runId.c_str(), static_cast<unsigned long>(message.raceStartTimeMs), static_cast<unsigned long>(message.heartbeat));
    if (message.type == RadioMessageType::Unknown) {
      Serial.printf("LORA unknown type=%s raw=%s\n", lastPacket_.c_str(), payload.c_str());
    }
    if (message.stationId.length() == 0) {
      Serial.printf("LORA missing stationId raw=%s\n", payload.c_str());
    }
    if (message.stationId == "start" && message.type != RadioMessageType::Unknown) {
      updateStartLink(message, lastRssi_, lastSnr_);
    }
    handleRadioMessage(message);
  }
#else
  (void)this;
#endif
}

bool FinishStationApp::sendRadio(const RadioMessage& message, int* resultCode) {
#if ENABLE_LORA
  if (!radioReady_) {
    if (resultCode != nullptr) *resultCode = -999;
    return false;
  }

  String payload;
  RadioProtocol::serialize(message, payload);
  const String typeText = RadioProtocol::typeToString(message.type);
  Serial.printf("%s payload len=%u\n", typeText.c_str(), static_cast<unsigned>(payload.length()));
  if (payload.length() > MAX_LORA_PAYLOAD_WARN) {
    Serial.printf("%s payload too large len=%u\n", typeText.c_str(), static_cast<unsigned>(payload.length()));
  }
  if (message.type == RadioMessageType::Status && payload.length() > MAX_LORA_PAYLOAD_HARD) {
    Serial.printf("STATUS skipped: payload too large len=%u\n", static_cast<unsigned>(payload.length()));
    RadioProtocol::serializeEmergencyStatus(message, payload);
    Serial.printf("STATUS emergency payload len=%u\n", static_cast<unsigned>(payload.length()));
  }
  const int result = radio.transmit(payload);
  if (resultCode != nullptr) *resultCode = result;
  restoreRadioReceiveMode();
  const bool logTxMode = true;
  Serial.println("LoRa RX mode restored");
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[FinishStation] LoRa TX failed: %d\n", result);
    return false;
  }
  if (logTxMode) Serial.printf("LoRa TX %s ok\n", typeText.c_str());
  return true;
#else
  (void)message;
  if (resultCode != nullptr) *resultCode = -1;
  return false;
#endif
}

void FinishStationApp::restoreRadioReceiveMode() {
#if ENABLE_LORA
  yield();
  delay(1);
#endif
}

void FinishStationApp::sendStatus(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::Status;
  message.stationId = "finish";
  message.state = state_.stateText();
  message.uptimeMs = nowMs;
  message.heartbeat = heartbeatCounter_ + 1;
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.raceClockSynced = raceClock_.isSynced();
  message.raceClockNowMs = raceClock_.nowRaceMs();
  if (state_.runId().length() > 0) { message.runId = state_.runId(); message.runNumber = state_.runNumber(); }
  message.elapsedMs = state_.isRiding() ? state_.elapsedMs(raceClock_.nowRaceMs()) : 0;
  message.offsetToMasterMs = raceClock_.offsetToMasterMs();
  message.syncAccuracyMs = syncAccuracyMs_;
  const BatteryStatus statusBattery = battery_.read();
  message.hasBatteryVoltage = statusBattery.available;
  message.batteryVoltage = statusBattery.voltage;
  message.batteryPercent = statusBattery.percent;
  if (isLinkActive(startLink_)) {
    message.hasStartRssi = true;
    message.startRssi = startLink_.lastRssi;
    message.hasStartSnr = true;
    message.startSnr = startLink_.lastSnr;
    message.startLastSeenAgoMs = linkAgeMs(startLink_);
  }

  String preview;
  RadioProtocol::serializeCompactStatus(message, preview);
  Serial.printf("FINISH STATUS TX compact hb=%lu len=%u\n",
                static_cast<unsigned long>(message.heartbeat), static_cast<unsigned>(preview.length()));

  int resultCode = 0;
  if (sendRadio(message, &resultCode)) {
    heartbeatCounter_ += 1;
    lastStatusSentOkMs_ = nowMs;
    Serial.printf("FINISH STATUS sent hb=%lu state=%s\n", static_cast<unsigned long>(heartbeatCounter_), message.state.c_str());
  } else {
    Serial.printf("FINISH STATUS send failed code=%d\n", resultCode);
  }
}

void FinishStationApp::sendHello(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::Hello;
  message.messageId = RadioProtocol::makeMessageId("hello");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.role = "FinishStation";
  message.uptimeMs = nowMs;
  if (sendRadio(message)) {
    Serial.printf("FINISH HELLO sent bootId=%s\n", bootId_.c_str());
  }
}

void FinishStationApp::sendHelloAck(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::HelloAck;
  message.messageId = RadioProtocol::makeMessageId("hello-ack");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.role = "FinishStation";
  message.uptimeMs = nowMs;
  if (sendRadio(message)) {
    Serial.printf("FINISH HELLO_ACK sent bootId=%s\n", bootId_.c_str());
  }
}

bool FinishStationApp::discoveryActive() const {
  const bool activeRun = state_.isRiding() || state_.state() == FinishRunState::FinishSent || state_.state() == FinishRunState::AckTimeout;
  return !activeRun && !syncInProgress_ && !isLinkActive(startLink_) && linkAgeMs(startLink_) >= LINK_TIMEOUT_MS;
}

bool FinishStationApp::priorityTxPending(uint32_t nowMs) const {
  const bool priorityTxRecently = lastPriorityTxMs_ > 0 && nowMs - lastPriorityTxMs_ < LORA_POST_PRIORITY_QUIET_MS;
  return priorityTxRecently ||
         (!finishAckReceived_ && state_.state() == FinishRunState::FinishSent &&
          (finishAttempts_ < FINISH_MAX_RETRY_ATTEMPTS || nowMs - lastFinishSendMs_ < FINISH_RETRY_INTERVAL_MS));
}

#if ENABLE_LORA_TIME_SYNC
void FinishStationApp::enterSyncReady(uint32_t nowMs) {
  if (syncInProgress_) {
    Serial.printf("SYNC already in progress syncId=%s, not creating new sync request\n", activeSyncId_.c_str());
    return;
  }
  syncInProgress_ = true;
  syncReady_ = true;
  activeSyncId_ = "";
  syncReadyUntilMs_ = nowMs + 15000UL;
  syncStatusText_ = "SYNC READY";
  Serial.println("Finish sync-ready mode: waiting SYNC_PING");
  sendSyncRequest(nowMs);
}

void FinishStationApp::sendSyncRequest(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::SyncRequest;
  message.messageId = RadioProtocol::makeMessageId("sync-request");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.uptimeMs = nowMs;
  lastPriorityTxMs_ = nowMs;
  sendRadio(message);
}

void FinishStationApp::sendSyncPong(const RadioMessage& ping) {
  const uint32_t t2 = millis();
  RadioMessage message;
  message.type = RadioMessageType::SyncPong;
  message.messageId = RadioProtocol::makeMessageId("sync-pong");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.syncId = ping.syncId;
  message.t1StartRaceMs = ping.t1StartRaceMs;
  message.t2FinishLocalMs = t2;
  message.t3FinishLocalMs = millis();
  lastPriorityTxMs_ = clock_.nowMs();
  syncStatusText_ = "SYNC... PONG";
  Serial.printf("SYNC_PONG TX syncId=%s t2=%lu t3=%lu\n", message.syncId.c_str(), static_cast<unsigned long>(message.t2FinishLocalMs), static_cast<unsigned long>(message.t3FinishLocalMs));
  sendRadio(message);
}

void FinishStationApp::sendSyncAck(const RadioMessage& apply) {
  if (syncInProgress_ && activeSyncId_.length() > 0 && apply.syncId != activeSyncId_) {
    Serial.printf("SYNC_APPLY ignored mismatched syncId active=%s rx=%s\n", activeSyncId_.c_str(), apply.syncId.c_str());
    return;
  }
  raceClock_.setOffsetToMaster(apply.offsetToMasterMs);
  raceClock_.markSynced();
  syncAccuracyMs_ = apply.networkDelayMs > 0 ? apply.networkDelayMs : (apply.roundTripMs / 2UL);
  lastSyncMs_ = millis();
  syncReady_ = false;
  syncInProgress_ = false;
  activeSyncId_ = apply.syncId;
  syncReadyUntilMs_ = 0;
  syncStatusText_ = "SYNC OK";

  RadioMessage message;
  message.type = RadioMessageType::SyncAck;
  message.messageId = RadioProtocol::makeMessageId("sync-ack");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.syncId = apply.syncId;
  message.offsetToMasterMs = raceClock_.offsetToMasterMs();
  message.syncAccuracyMs = syncAccuracyMs_;
  lastPriorityTxMs_ = clock_.nowMs();
  Serial.printf("SYNC_ACK TX syncId=%s offset=%ld acc=%lu\n", apply.syncId.c_str(), static_cast<long>(raceClock_.offsetToMasterMs()), static_cast<unsigned long>(syncAccuracyMs_));
  sendRadio(message);
}

#endif

void FinishStationApp::sendRunStartAck(const String& runId) {
  RadioMessage message;
  message.type = RadioMessageType::RunStartAck;
  message.messageId = RadioProtocol::makeMessageId("run-start-ack");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.runId = runId;
  message.state = state_.stateText();
  message.timestampMs = clock_.nowMs();
  message.raceClockSynced = raceClock_.isSynced() && syncDoneOnce_;
  message.raceClockNowMs = raceClock_.nowRaceMs();
  Serial.printf("RUN_START_ACK TX runId=%s\n", runId.c_str());
  Serial.printf("TX priority RUN_START_ACK runId=%s\n", runId.c_str());
  lastPriorityTxMs_ = clock_.nowMs();
  if (sendRadio(message)) {
    lastRunStartAckRunId_ = runId;
    Serial.printf("RUN_START_ACK sent runId=%s\n", runId.c_str());
  }
}

void FinishStationApp::acceptFinishButton(uint32_t nowMs) {
  Serial.println("FINISH button short press");
  Serial.printf("canFinish=%s\n", state_.canFinish() ? "true" : "false");
  Serial.printf("Finish accepted runId=%s\n", state_.runId().c_str());
  const uint32_t finishRaceTimeMs = raceClock_.nowRaceMs();
  finishLocalElapsedMs_ = finishRaceTimeMs >= state_.raceStartTimeMs() ? finishRaceTimeMs - state_.raceStartTimeMs() : 0;
  const uint32_t finishTimestampMs = finishRaceTimeMs;
  localResultMs_ = finishLocalElapsedMs_;
  Serial.printf("FINISH accepted localElapsedMs=%lu\n", static_cast<unsigned long>(finishLocalElapsedMs_));
  Serial.printf("resultMs=%lu\n", static_cast<unsigned long>(localResultMs_));
  Serial.printf("remoteStartTimestampMs=%lu\n", static_cast<unsigned long>(state_.startTimestampMs()));
  Serial.printf("finishTimestampMs=%lu\n", static_cast<unsigned long>(finishTimestampMs));
  lastResultMs_ = localResultMs_;
  lastResultFormatted_ = formatSeconds(localResultMs_);
  lastFinishedRunId_ = state_.runId();
  finishAckReceived_ = false;
  state_.markFinishSent(finishTimestampMs);
  finishAttempts_ = 0;
  manualResendCount_ = 0;
  lastFinishSendMs_ = 0;
  finishLineCrossedUntilMs_ = nowMs + 2000UL;
  led_.flash(2, 100, 120);
#if ENABLE_OLED
  if (!display_.testPatternOnly()) {
    display_.showLines({finishHeader(), "FINISH LINE", "CROSSED", lastResultFormatted_});
  }
#endif
  sendFinish();
}

void FinishStationApp::handleFinishButton(uint32_t nowMs) {
  Serial.printf("current finish state=%s canFinish=%s active runId=%s\n", state_.stateText().c_str(), state_.canFinish() ? "true" : "false", state_.runId().c_str());
  if (!raceClock_.isSynced() || !syncDoneOnce_) {
    Serial.println("Finish button ignored: wifi sync not ready");
    return;
  }
  if (state_.canFinish()) {
    acceptFinishButton(nowMs);
    return;
  }
  if (state_.state() == FinishRunState::FinishSent || state_.state() == FinishRunState::AckTimeout) {
    resendFinishFromButton(nowMs);
    return;
  }
  if (state_.state() == FinishRunState::Idle) {
    Serial.println("FINISH button ignored: no active run");
    showNoRunUntilMs_ = nowMs + 1500UL;
#if ENABLE_OLED
    if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "NO ACTIVE RUN"});
#endif
    return;
  }
  Serial.printf("button ignored: state=%s\n", state_.stateText().c_str());
}

void FinishStationApp::sendFinish() {
  if (finishAckReceived_) return;
  RadioMessage message;
  message.type = RadioMessageType::Finish;
  message.messageId = RadioProtocol::makeMessageId("finish");
  message.stationId = "finish";
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.runId = state_.runId();
  message.finishTimestampMs = state_.finishTimestampMs();
  message.finishRaceTimeMs = state_.finishTimestampMs();
  message.resultMs = localResultMs_ > 0 ? localResultMs_ : lastResultMs_;
  message.timingSource = "WIFI_SYNCED_RACE_CLOCK_ONCE";
  message.syncAccuracyMs = syncAccuracyMs_;
  message.source = "BUTTON_STUB";
  finishAttempts_ += 1;
  lastFinishSendMs_ = clock_.nowMs();
  lastPriorityTxMs_ = lastFinishSendMs_;
  Serial.printf("FINISH TX attempt=%u/%u runId=%s\n", finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS, state_.runId().c_str());
  Serial.printf("Waiting FINISH_ACK runId=%s\n", state_.runId().c_str());
  if (sendRadio(message)) {
    Serial.printf("[FinishStation] FINISH sent: run=%s attempt=%u/%u\n", state_.runId().c_str(),
                  finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS);
  } else {
    Serial.printf("[FinishStation] FINISH send failed: run=%s attempt=%u/%u\n", state_.runId().c_str(),
                  finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS);
  }
}

void FinishStationApp::resendFinishFromButton(uint32_t nowMs) {
  if (state_.runId().length() == 0 || state_.finishTimestampMs() == 0) {
    Serial.println("FINISH button ignored: no saved finish to resend");
    showNoRunUntilMs_ = nowMs + 1500UL;
    return;
  }
  const bool resendAfterTimeout = state_.state() == FinishRunState::AckTimeout;
  if (resendAfterTimeout) {
    finishAttempts_ = 0;
    state_.markFinishSent(state_.finishTimestampMs());
    Serial.printf("FINISH button short press: manual resend runId=%s\n", state_.runId().c_str());
  } else {
    manualResendCount_ += 1;
    Serial.printf("FINISH button short press: manual resend runId=%s\n", state_.runId().c_str());
  }
  finishLineCrossedUntilMs_ = 0;
  led_.flash(3, 100, 120);
  sendFinish();
#if ENABLE_OLED
  if (!display_.testPatternOnly()) {
    display_.showLines({finishHeader(), resendAfterTimeout ? "RESEND FINISH" : "FINISH RESENT", "Sent: " + String(finishAttempts_) + "/" + String(FINISH_MAX_RETRY_ATTEMPTS)});
  }
#endif
}

void FinishStationApp::updateStartLink(const RadioMessage& message, int packetRssi, float packetSnr) {
  const String packetType = RadioProtocol::typeToString(message.type);
  const String oldBootId = startLink_.remoteBootId;
  const bool rebootDetected = updateLinkStatus(startLink_, message.stationId, packetType, message.bootId, packetRssi, packetSnr);
  if (rebootDetected) {
    Serial.printf("REMOTE REBOOT detected station=start oldBootId=%s newBootId=%s\n", oldBootId.c_str(), message.bootId.c_str());
    startHeartbeatCount_ = 0;
    raceClock_.clearSync();
    syncDoneOnce_ = false;
    wifiSyncSampleIndex_ = 0;
    bestWifiSyncRttMs_ = UINT32_MAX;
    syncStatusText_ = "WIFI SEARCH";
    wifiStatusText_ = "WIFI SEARCH";
  }
  if (message.state.length() > 0) startState_ = message.state;
  Serial.printf("LORA RX from=%s type=%s rssi=%d snr=%.1f age=0 count=%lu\n",
                message.stationId.c_str(), packetType.c_str(), packetRssi, static_cast<double>(packetSnr),
                static_cast<unsigned long>(startLink_.packetCount));
}

void FinishStationApp::handleRadioMessage(const RadioMessage& message) {
  if (message.type == RadioMessageType::Status && message.stationId == "start") {
    startHeartbeatCount_ = message.heartbeat;
    Serial.printf("STATUS RX from=start hb=%lu rssi=%d\n", static_cast<unsigned long>(message.heartbeat), startLink_.lastRssi);
    if ((message.state == "Riding" || message.state == "G") && state_.state() == FinishRunState::Idle) {
      const uint32_t nowMs = millis();
      if (nowMs - lastStatusRidingWarnMs_ >= 2000UL) {
        const uint32_t lastRunStartRxAge = lastRunStartRxMs_ > 0 ? nowMs - lastRunStartRxMs_ : UINT32_MAX;
        Serial.println("WARN: Start reports Riding but Finish has no active RUN_START");
        Serial.printf("lastRunStartRxAge=%lu activeRunId=%s\n", static_cast<unsigned long>(lastRunStartRxAge), state_.runId().c_str());
        lastStatusRidingWarnMs_ = nowMs;
#if ENABLE_OLED
        if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "WAIT RUN_START"});
#endif
      }
    }
    return;
  }

  if (message.type == RadioMessageType::Hello && message.stationId == "start") {
    lastHelloReceivedMs_ = millis();
    if (priorityTxPending(millis())) {
      Serial.println("TX deferred HELLO_ACK because priority message pending");
    } else {
      sendHelloAck(millis());
    }
    return;
  }

  if (message.type == RadioMessageType::HelloAck && message.stationId == "start") {
    lastHelloReceivedMs_ = millis();
    return;
  }

#if ENABLE_LORA_TIME_SYNC
  if (message.type == RadioMessageType::SyncRequest && message.stationId == "start") {
    Serial.println("SYNC_REQUEST RX from start");
    enterSyncReady(millis());
    return;
  }

  if (message.type == RadioMessageType::SyncPing && message.stationId == "start") {
    if (!syncReady_ && !raceClock_.isSynced()) {
      Serial.println("SYNC_PING RX while not ready; responding because StartStation is sync master");
    }
    if (syncInProgress_ && activeSyncId_.length() > 0 && message.syncId != activeSyncId_) {
      Serial.printf("SYNC_PING ignored mismatched syncId active=%s rx=%s\n", activeSyncId_.c_str(), message.syncId.c_str());
      return;
    }
    syncInProgress_ = true;
    syncReady_ = true;
    activeSyncId_ = message.syncId;
    syncReadyUntilMs_ = millis() + 15000UL;
    sendSyncPong(message);
    return;
  }

  if (message.type == RadioMessageType::SyncApply && message.stationId == "start") {
    sendSyncAck(message);
    return;
  }

#endif

  if (message.type == RadioMessageType::RunStart && message.stationId == "start") {
    lastRunStartRxMs_ = millis();
    const bool duplicate = state_.runId() == message.runId && message.runId.length() > 0;
    Serial.printf("RUN_START received runId=%s stationId=%s\n", message.runId.c_str(), message.stationId.c_str());
    Serial.printf("parsed type=RUN_START stationId=%s runId=%s raceStartTimeMs=%lu\n", message.stationId.c_str(), message.runId.c_str(), static_cast<unsigned long>(message.raceStartTimeMs));
    Serial.printf("RUN_START duplicate? %s\n", duplicate ? "yes" : "no");
    if (!raceClock_.isSynced() || !syncDoneOnce_) {
      Serial.println("RUN_START ignored: race clock not synced");
#if ENABLE_OLED
      if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "NO TIME SYNC"});
#endif
      return;
    }
    if (message.runId.length() == 0) {
      Serial.println("RUN_START invalid: missing runId");
      return;
    }
    if (message.raceStartTimeMs == 0) {
      Serial.println("RUN_START invalid: missing raceStartTimeMs");
      return;
    }
    if (!duplicate) {
      const uint32_t nowRaceMs = raceClock_.nowRaceMs();
      remoteStartTimestampMs_ = message.raceStartTimeMs;
      const uint32_t elapsedAtReceiveMs = nowRaceMs >= remoteStartTimestampMs_ ? nowRaceMs - remoteStartTimestampMs_ : 0;
      Serial.printf("raceStartTimeMs=%lu\n", static_cast<unsigned long>(remoteStartTimestampMs_));
      Serial.printf("nowRaceMs=%lu\n", static_cast<unsigned long>(nowRaceMs));
      Serial.printf("elapsedAtReceiveMs=%lu\n", static_cast<unsigned long>(elapsedAtReceiveMs));
      state_.startRun(message.runId, message.runNumber, message.riderName, message.trailName, remoteStartTimestampMs_, nowRaceMs);
      Serial.println("Finish state -> Riding");
      Serial.printf("localRunStartReceivedMillis=%lu\n", static_cast<unsigned long>(nowRaceMs));
      sensor_.arm(message.runId, remoteStartTimestampMs_);
      finishAttempts_ = 0;
      manualResendCount_ = 0;
      finishAckReceived_ = false;
      localResultMs_ = 0;
      finishLocalElapsedMs_ = 0;
      lastFinishSendMs_ = 0;
      finishLineCrossedUntilMs_ = 0;
      showNoRunUntilMs_ = 0;
      showAckOkUntilMs_ = 0;
      buzzer_.beep("RUN_START");
#if ENABLE_OLED
      if (!display_.testPatternOnly()) display_.showLines({finishHeader(), "RIDING", formatDurationMs(elapsedAtReceiveMs).substring(0, 5), "Btn: FINISH"});
#endif
    } else {
      Serial.printf("Duplicate RUN_START, ACK resent runId=%s\n", message.runId.c_str());
    }
    sendRunStartAck(message.runId);
    return;
  }

  if (message.type == RadioMessageType::FinishAck && message.stationId == "start") {
    Serial.printf("FINISH_ACK RX runId=%s resultMs=%lu\n", message.runId.c_str(), static_cast<unsigned long>(message.resultMs));
    const String expectedRunId = state_.runId().length() > 0 ? state_.runId() : lastFinishedRunId_;
    if (message.runId == expectedRunId && message.runId.length() > 0) {
      Serial.println(state_.runId().length() > 0 ? "FINISH_ACK matched active run" : "FINISH_ACK matched last finished run");
      finishAckReceived_ = true;
      if (message.resultMs > 0) lastResultMs_ = message.resultMs;
      if (message.resultFormatted.length() > 0) {
        lastResultFormatted_ = message.resultFormatted;
      } else if (lastResultMs_ > 0) {
        lastResultFormatted_ = formatSeconds(lastResultMs_);
      }
      lastFinishedRunId_ = message.runId;
      sensor_.reset();
      state_.ackFinish();
      finishAttempts_ = 0;
      manualResendCount_ = 0;
      lastFinishSendMs_ = 0;
      finishLineCrossedUntilMs_ = 0;
      showAckOkUntilMs_ = millis() + 2000UL;
      Serial.println("ACK matched, retry stopped");
      Serial.println("Finish retry stopped");
      Serial.println("Finish state -> AckOk / Idle");
      Serial.printf("Last result = %s\n", lastResultFormatted_.c_str());
#if ENABLE_OLED
      if (!display_.testPatternOnly()) {
        display_.showLines({finishHeader(), "ACK OK", lastResultFormatted_, batteryText(battery_.read())});
      }
#endif
    } else {
      Serial.printf("FINISH_ACK ignored mismatch expected=%s got=%s\n", expectedRunId.c_str(), message.runId.c_str());
    }
    return;
  }
}

void FinishStationApp::updateDisplay() {
  const String runShort = state_.runId().length() > 0 ? state_.runId().substring(max(0, static_cast<int>(state_.runId().length()) - 6)) : "-";
  const bool startOnline = isLinkActive(startLink_);
  const BatteryStatus battery = battery_.read();
  const String bat = batteryText(battery);
  const String startSignal = startOnline ? String("START:") + String(startLink_.lastRssi) + "dBm" : String("START:NO SIGNAL");
  const String linkDebug = startLink_.lastPacketType.length() > 0 ? String("PKT:") + startLink_.lastPacketType : String("HB:") + String(startHeartbeatCount_);
  if (finishLineCrossedUntilMs_ > millis()) {
    display_.showLines({finishHeader(), "FINISH LINE", "CROSSED", lastResultFormatted_});
    return;
  }

  if (showAckOkUntilMs_ > millis()) {
    display_.showLines({finishHeader(), "ACK OK", lastResultFormatted_.length() > 0 ? lastResultFormatted_ : String("-"), bat});
    return;
  }

  if (showNoRunUntilMs_ > millis()) {
    display_.showLines({finishHeader(), "NO ACTIVE RUN", "Press START"});
    return;
  }

  if (state_.state() == FinishRunState::AckTimeout) {
    display_.showLines({finishHeader(), "ACK TIMEOUT", lastResultFormatted_.length() > 0 ? lastResultFormatted_ : String("-"), "PRESS RESEND"});
    return;
  }

  if (state_.state() == FinishRunState::Error) {
    display_.showLines({finishHeader(), "ERROR", startSignal, linkDebug});
    return;
  }

  if (!raceClock_.isSynced() || !syncDoneOnce_) {
    if (!wifiConnected_) display_.showLines({finishHeader(), "WIFI SEARCH", "EnduroTimer"});
    else display_.showLines({finishHeader(), "WIFI OK", "SYNC TIME"});
    return;
  }

  if (state_.isRiding()) {
    String anim = String("RIDING ");
    for (uint8_t i = 0; i < ridingAnimationFrame(); ++i) anim += " ";
    anim += ">";
    display_.showLines({finishHeader(), anim, formatDurationMs(state_.elapsedMs(raceClock_.nowRaceMs())).substring(0, 5), startSignal, bat});
    return;
  }

  if (state_.state() == FinishRunState::FinishSent) {
    display_.showLines({finishHeader(), "FINISH SENT", lastResultFormatted_.length() > 0 ? lastResultFormatted_ : String("-"), "Sent: " + String(finishAttempts_) + "/" + String(FINISH_MAX_RETRY_ATTEMPTS)});
    return;
  }

  display_.showLines({
    finishHeader(),
    "SYNCED",
    "READY",
    "acc: " + String(syncAccuracyMs_) + "ms",
  });
}

uint8_t FinishStationApp::ridingAnimationFrame() const {
  return static_cast<uint8_t>((millis() / 250UL) % 10UL);
}

String FinishStationApp::makeBootId(const char* stationId) const {
  const uint64_t mac = ESP.getEfuseMac();
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%s-%08lX%08lX-%lu", stationId,
           static_cast<unsigned long>(mac >> 32), static_cast<unsigned long>(mac & 0xFFFFFFFFULL),
           static_cast<unsigned long>(finishBootCounter));
  return String(buffer);
}

void FinishStationApp::logHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs_ < 5000UL) return;

  String heartbeatState = state_.stateText();
  heartbeatState.toUpperCase();
  Serial.printf("FINISH BUTTON raw=%d pressed=%d state=%s\n", sensor_.buttonRaw(), sensor_.buttonPressed() ? 1 : 0, state_.stateText().c_str());
  Serial.printf("FINISH alive state=%s uptime=%lu heap=%lu minHeap=%lu buttonRaw=%d buttonPressed=%s canFinish=%s localRunStartReceivedMillis=%lu finishLocalElapsedMs=%lu remoteStartTimestampMs=%lu\n", heartbeatState.c_str(),
                static_cast<unsigned long>(nowMs), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), sensor_.buttonRaw(), sensor_.buttonPressed() ? "true" : "false", state_.canFinish() ? "true" : "false", static_cast<unsigned long>(state_.localRunStartReceivedMillis()), static_cast<unsigned long>(finishLocalElapsedMs_), static_cast<unsigned long>(remoteStartTimestampMs_));
  Serial.printf("FINISH LINK DEBUG:\n  finishHbSent=%lu\n  startActive=%d\n  startAge=%lu\n  startRssi=%d\n  startLastType=%s\n  startHbReceived=%lu\n",
                static_cast<unsigned long>(heartbeatCounter_), isLinkActive(startLink_) ? 1 : 0,
                static_cast<unsigned long>(linkAgeMs(startLink_)), startLink_.lastRssi,
                startLink_.lastPacketType.c_str(), static_cast<unsigned long>(startHeartbeatCount_));
  lastHeartbeatMs_ = nowMs;
}

String FinishStationApp::batteryText(const BatteryStatus& status) const {
  if (status.available && status.percent >= 0) return String("BAT:") + String(status.percent) + "%";
  return "BAT:USB";
}
