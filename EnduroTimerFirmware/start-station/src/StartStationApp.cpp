#include "StartStationApp.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_system.h>

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
static constexpr uint32_t DisplayRefreshMs = 250UL;
static constexpr uint32_t ButtonDebounceMs = 50UL;
static constexpr uint32_t StatusIntervalMs = LINK_HEARTBEAT_INTERVAL_MS;
static constexpr uint8_t FINISH_ACK_REPEAT_COUNT = 3;
static constexpr uint32_t FINISH_ACK_REPEAT_INTERVAL_MS = 250UL;
RTC_DATA_ATTR static uint32_t startBootCounter = 0;
#if ENABLE_LORA
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
#endif

void StartStationApp::begin() {
  startBootCounter += 1;

  Serial.println("Firmware: EnduroTimer StartStation");
  Serial.println("Version: v" FIRMWARE_VERSION);
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Board/role: Heltec WiFi LoRa 32 V3 / StartStation");
  bootId_ = makeBootId("start");
  Serial.printf("Boot counter: %lu\n", static_cast<unsigned long>(startBootCounter));
  Serial.printf("BootId: %s\n", bootId_.c_str());
  Serial.printf("Chip: %s rev %u cores=%u efuseMac=%llX\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getEfuseMac());
  Serial.println("Power save: disabled");

#if ENABLE_OLED
  Serial.println("[BOOT] OLED init...");
  oledReady_ = display_.begin();
  Serial.println(oledReady_ ? "[BOOT] OLED OK" : "[BOOT] OLED FAIL");
  if (oledReady_) {
    display_.showBootScreen(startHeader());
  }
#else
  Serial.println("[BOOT] OLED init skipped (ENABLE_OLED=0)");
  oledReady_ = false;
#endif

  configureButton();
  Serial.println("[BOOT] Button OK");
  buzzer_.begin();
  battery_.begin();
  led_.begin();
  state_.begin();
  loadStorage();
}

void StartStationApp::loop() {
  const uint32_t now = clock_.nowMs();
#if ENABLE_LORA
  pollRadio();
#endif

  updateButton(now);
  updateLed(now);

  RunRecord runToStart;
  updateCountdownDisplay(now);
  if (state_.updateCountdown(now, runToStart)) {
    Serial.printf("COUNTDOWN GO at ms=%lu\n", static_cast<unsigned long>(now));
    Serial.printf("RUN GO timestamp startTimestampMs=%lu\n", static_cast<unsigned long>(runToStart.startTimestampMs));
    buzzer_.beep("GO");
    pendingRunStartAck_ = true;
    runStartAckReceived_ = false;
    runStartAckTimedOut_ = false;
    runStartAckAttempts_ = 0;
    lastRunStartAckMs_ = 0;
    sendRunStart(runToStart);
    lastPriorityTxMs_ = now;
  }

  retryRunStartAck(now);
  processFinishAckRepeats(now);

  const bool priorityPending = priorityTxPending();
  if (discoveryActive() && now - lastDiscoverySentMs_ >= LINK_DISCOVERY_INTERVAL_MS) {
    if (priorityPending) {
      Serial.println("TX deferred HELLO because priority message pending");
    } else {
      sendHello(now);
      lastDiscoverySentMs_ = now;
    }
  }

  if (now - lastStatusSendMs_ >= StatusIntervalMs) {
    if (priorityPending) {
      Serial.println("TX deferred STATUS because priority message pending");
    } else {
      sendStatus(now);
      lastStatusSendMs_ = now;
    }
  }

  state_.tickAutoReady(now);

#if ENABLE_OLED
  display_.update();
  if (!display_.testPatternOnly() && now - lastDisplayMs_ >= DisplayRefreshMs) {
    updateDisplay();
    lastDisplayMs_ = now;
  }
#endif

  logHeartbeat(now);
}

bool StartStationApp::requestStartRun(String& error) {
  Serial.printf("START BUTTON pressed at ms=%lu\n", static_cast<unsigned long>(millis()));
  Serial.println("Start run requested");
  RiderRecord rider = selectRider();
  TrailRecord trail = selectTrail();
  runSequenceCounter_ += 1;
  const uint64_t startedAtEpoch = wallClockSynced_ ? currentEpochMs() : 0;
  const String startedAtText = wallClockSynced_ ? formatEpochLocal(startedAtEpoch) : String("TIME NOT SYNCED");
  const bool ok = state_.startCountdown(runSequenceCounter_, startedAtText, startedAtEpoch, rider.id, rider.displayName, trail.id, trail.displayName, error);
  if (!ok) {
    if (runSequenceCounter_ > 0) runSequenceCounter_ -= 1;
    error = "Run already active";
    return false;
  }
  lastCountdownText_ = "";
  updateCountdownDisplay(millis());
  return true;
}

void StartStationApp::resetSystem() {
  Serial.println("[StartStation] system reset requested");
  pendingRunStartAck_ = false;
  runStartAckReceived_ = false;
  runStartAckTimedOut_ = false;
  runStartAckAttempts_ = 0;
  lastRunStartSendMs_ = 0;
  state_.resetActiveRun();
}

void StartStationApp::setWifiStatus(bool apStarted, const IPAddress& ip, const String& mac) {
  wifiApStarted_ = apStarted;
  wifiIp_ = ip;
  wifiMac_ = mac;
  if (!wifiApStarted_) {
    Serial.println("WiFi AP failed.");
  }
#if ENABLE_OLED
  if (!display_.testPatternOnly()) {
    updateDisplay();
  }
#endif
}

void StartStationApp::setWebStatus(bool webStarted) {
  webStarted_ = webStarted;
}

String StartStationApp::statusJson() const {
  JsonDocument doc;
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();

  doc["device"] = "StartStation";
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["wallClockSynced"] = wallClockSynced_;
  doc["currentTimeText"] = currentTimeText();
  doc["lastTimeSyncText"] = lastTimeSyncText_;
  doc["timeSource"] = timeSource_;
  doc["bootId"] = bootId_;
  doc["buildDate"] = __DATE__;
  doc["buildTime"] = __TIME__;
  doc["state"] = state_.stateText();
  doc["oledOk"] = oledReady_;
  doc["wifiOk"] = wifiApStarted_;
  doc["webOk"] = webStarted_;
  doc["loraOk"] = radioReady_;
  const BatteryStatus startBattery = battery_.read();
  doc["batteryAvailable"] = startBattery.available;
  doc["batteryVoltage"] = startBattery.available ? startBattery.voltage : 0.0F;
  doc["batteryPercent"] = startBattery.percent;
  doc["startBatteryAvailable"] = startBattery.available;
  doc["startBatteryVoltage"] = startBattery.available ? startBattery.voltage : 0.0F;
  doc["startBatteryPercent"] = startBattery.percent;
  doc["finishBatteryAvailable"] = finishBatteryAvailable_;
  doc["finishBatteryVoltage"] = finishBatteryVoltage_;
  doc["finishBatteryPercent"] = finishBatteryPercent_;
  doc["finishLocalRunStartReceivedMillis"] = finishLocalRunStartReceivedMillis_;
  doc["finishLocalElapsedMs"] = finishLocalElapsedMs_;
  doc["finishRemoteStartTimestampMs"] = finishRemoteStartTimestampMs_;
  const bool finishLinkActive = finishOnline();
  const uint32_t finishAgeMs = finishLastSeenAgoMs();
  doc["finishLinkActive"] = finishLinkActive;
  doc["finishStationOnline"] = finishLinkActive;
  doc["finishSignalText"] = finishSignalText();
  doc["finishLastSeenAgoMs"] = finishAgeMs == UINT32_MAX ? 0 : finishAgeMs;
  doc["finishPacketCount"] = finishLink_.packetCount;
  doc["discoveryActive"] = discoveryActive();
  doc["remoteBootId"] = finishLink_.remoteBootId;
  doc["finishBootId"] = finishLink_.remoteBootId;
  doc["remoteRebootCount"] = finishLink_.remoteRebootCount;
  doc["lastDiscoverySentAgoMs"] = lastDiscoverySentMs_ > 0 ? static_cast<uint32_t>(millis() - lastDiscoverySentMs_) : UINT32_MAX;
  doc["lastHelloReceivedAgoMs"] = lastHelloReceivedMs_ > 0 ? static_cast<uint32_t>(millis() - lastHelloReceivedMs_) : UINT32_MAX;
  doc["finishLastPacketType"] = finishLink_.lastPacketType;
  doc["finishLastStatusMs"] = finishLastStatusMs_;
  doc["finishHeartbeatCount"] = finishHeartbeatCount_;
  if (finishFirmwareVersion_.length() > 0) doc["finishFirmwareVersion"] = finishFirmwareVersion_; else doc["finishFirmwareVersion"] = nullptr;
  doc["finishActiveRunId"] = finishActiveRunId_;
  doc["finishRiderName"] = finishRiderName_;
  doc["finishElapsedMs"] = finishElapsedMs_;
  doc["startHeartbeatCount"] = startHeartbeatCount_;
  doc["pendingRunStartAck"] = pendingRunStartAck_;
  doc["runStartAckAttempts"] = runStartAckAttempts_;
  doc["runStartAckReceived"] = runStartAckReceived_;
  doc["lastRunStartAckAgoMs"] = lastRunStartAckMs_ > 0 ? static_cast<uint32_t>(millis() - lastRunStartAckMs_) : UINT32_MAX;
  if (lastStatusSentOkMs_ > 0) doc["lastStartStatusSentAgoMs"] = static_cast<uint32_t>(millis() - lastStatusSentOkMs_); else doc["lastStartStatusSentAgoMs"] = nullptr;
  const String displayedFinishState = finishLinkActive ? finishState_ : String("Unknown");
  doc["finishState"] = displayedFinishState;
  doc["finishLastKnownState"] = finishState_;
  doc["finishHasError"] = finishLinkActive && finishState_ == "Error";
  if (finishLinkActive && finishState_ == "Error") {
    doc["finishErrorMessage"] = "FinishStation reported Error";
  } else {
    doc["finishErrorMessage"] = nullptr;
  }
  if (finishLinkActive) {
    doc["finishRssi"] = finishLink_.lastRssi;
    doc["finishSnr"] = finishLink_.lastSnr;
  } else {
    doc["finishRssi"] = nullptr;
    doc["finishSnr"] = nullptr;
  }
  doc["finishReportedStartLinkActive"] = finishReportedStartLinkActive_;
  doc["finishReportedStartPacketCount"] = finishReportedStartPacketCount_;
  if (hasFinishReportedStartSignal_ && finishReportedStartLinkActive_) {
    doc["finishReportedStartRssi"] = finishReportedStartRssi_;
    doc["finishReportedStartSnr"] = finishReportedStartSnr_;
    doc["finishReportedStartLastSeenAgoMs"] = finishReportedStartLastSeenAgoMs_;
    doc["finishReportedStartSignalText"] = finishReportedStartSignalText();
  } else {
    doc["finishReportedStartRssi"] = nullptr;
    doc["finishReportedStartSnr"] = nullptr;
    doc["finishReportedStartLastSeenAgoMs"] = nullptr;
    doc["finishReportedStartSignalText"] = "NO SIGNAL";
  }
  doc["lastPacketType"] = lastFinishPacketType_;
  doc["lastLoRaPacketType"] = lastFinishPacketType_;
  doc["lastAnyPacketMs"] = lastAnyPacketMs_;
  doc["lastRssi"] = lastRssi_;
  doc["lastSnr"] = lastSnr_;
  doc["lastLoRaRaw"] = lastLoRaRaw_;
  doc["lastLoRaRawShort"] = lastLoRaRaw_;
  doc["currentRunId"] = current.runId;
  RiderRecord selectedRider = selectRider();
  TrailRecord selectedTrail = selectTrail();
  doc["selectedTrailId"] = selectedTrail.id;
  doc["selectedTrailName"] = selectedTrail.displayName;
  doc["currentRiderName"] = current.riderName.length() > 0 ? current.riderName : selectedRider.displayName;
  doc["currentTrailName"] = current.trailName.length() > 0 ? current.trailName : selectedTrail.displayName;
  uint32_t elapsedMs = (state_.state() == StartRunState::Riding && current.startTimestampMs > 0) ? millis() - current.startTimestampMs : 0;
  doc["countdownStartedMs"] = state_.countdownStartedMs();
  doc["goTimestampMs"] = state_.goTimestampMs();
  doc["startTimestampMs"] = current.startTimestampMs;
  doc["currentRunElapsedMs"] = elapsedMs;
  doc["currentRunElapsedFormatted"] = elapsedMs > 0 ? formatDurationMs(elapsedMs).substring(0, 5) : String("00:00");
  doc["ridingAnimationFrame"] = ridingAnimationFrame();
  doc["countdownText"] = state_.countdownText(millis());
  doc["lastResultMs"] = last.resultMs > 0 ? last.resultMs : 0;
  doc["lastResultFormatted"] = last.resultFormatted;
  doc["pendingFinishAck"] = pendingFinishAckRunId_.length() > 0;
  doc["finishAckRepeatCount"] = pendingFinishAckRunId_.length() > 0 ? finishAckSendCount_ : 0;
  doc["finishAckRepeatTotal"] = FINISH_ACK_REPEAT_COUNT;
  doc["lastFinishedRunId"] = last.runId;
  doc["lastFinishAckSentMs"] = lastFinishAckSentMs_;
  doc["lastFinishAckRunId"] = lastFinishAckRunId_;
  doc["finishAckSendCount"] = finishAckSendCount_;
  doc["lastFinishSource"] = last.finishSource.length() > 0 ? last.finishSource : String("");
  doc["lastTimingSource"] = last.timingSource;
  doc["lastTimingNote"] = last.timingNote;
  doc["uptimeMs"] = millis();
  doc["heap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();

  String output;
  serializeJson(doc, output);
  return output;
}

String StartStationApp::runsJson() const {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const RunRecord& run : state_.runs()) {
    JsonObject item = array.add<JsonObject>();
    item["runNumber"] = run.runNumber;
    item["runId"] = run.runId;
    item["startedAtEpochMs"] = run.startedAtEpochMs;
    item["startedAtText"] = run.startedAtText;
    item["runStartedAtText"] = run.startedAtText;
    item["riderId"] = run.riderId;
    item["riderName"] = run.riderName;
    item["trailId"] = run.trailId;
    item["trailName"] = run.trailName;
    item["startTimestampMs"] = run.startTimestampMs;
    item["finishTimestampMs"] = run.finishTimestampMs;
    item["resultMs"] = run.resultMs;
    item["resultFormatted"] = run.resultFormatted;
    item["status"] = run.status;
    item["finishSource"] = run.finishSource;
    item["source"] = run.finishSource;
    item["timingSource"] = run.timingSource;
    item["timingNote"] = run.timingNote;
  }

  Serial.printf("GET /api/runs count=%u\n", static_cast<unsigned>(state_.runs().size()));
  String output;
  serializeJson(doc, output);
  return output;
}

void StartStationApp::beginRadio() {
#if ENABLE_LORA
  Serial.println("[BOOT] LoRa init...");
  Serial.printf("LoRa init... %.0f MHz\n", LORA_FREQUENCY_MHZ);
  SPI.begin(9, 11, 10, LORA_NSS);
  const int state = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = state == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("LoRa FAIL code=%d\n", state);
    Serial.println("[BOOT] LoRa FAIL");
#if ENABLE_OLED
    if (!display_.testPatternOnly()) display_.showLines({startHeader(), "LoRa FAIL", "code=" + String(state)});
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

void StartStationApp::configureButton() {
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  startButton_.begin(START_BUTTON_PIN, ButtonDebounceMs);
  Serial.printf("Button: configured START_BUTTON_PIN=%d INPUT_PULLUP\n", START_BUTTON_PIN);
}

void StartStationApp::updateButton(uint32_t nowMs) {
  startButton_.update(nowMs);
  if (!startButton_.wasShortPressed()) return;

  Serial.println("START button short press event");
  if (state_.state() != StartRunState::Ready) {
    Serial.printf("button ignored: state=%s\n", state_.stateText().c_str());
    return;
  }

  String error;
  if (!requestStartRun(error)) {
    Serial.printf("button ignored: state=%s error=%s\n", state_.stateText().c_str(), error.c_str());
  }
}

void StartStationApp::updateLed(uint32_t nowMs) {
  (void)nowMs;
  LedMode mode = LedMode::ReadySlowBlink;
  switch (state_.state()) {
    case StartRunState::Boot:
    case StartRunState::Ready:
    case StartRunState::Finished:
      mode = finishOnline() || state_.state() != StartRunState::Ready ? LedMode::ReadySlowBlink : LedMode::NoSignalBlink;
      break;
    case StartRunState::Countdown:
      mode = LedMode::CountdownFastBlink;
      break;
    case StartRunState::Riding:
      mode = LedMode::RidingSolid;
      break;
    case StartRunState::Error:
      mode = LedMode::ErrorFastBlink;
      break;
  }
  led_.setMode(mode);
  led_.update();
}

String StartStationApp::startHeader() const {
  return String("START TERM v") + FIRMWARE_VERSION;
}

String StartStationApp::startShortHeader() const {
  return String("START v") + FIRMWARE_VERSION;
}

void StartStationApp::pollRadio() {
#if ENABLE_LORA
  if (!radioReady_) return;

  String payload;
  const int state = radio.receive(payload, 0);
  if (state == RADIOLIB_ERR_NONE) {
    lastAnyPacketMs_ = millis();
    lastLoRaRaw_ = payload;
    if (lastLoRaRaw_.length() > 96) lastLoRaRaw_ = lastLoRaRaw_.substring(0, 96);
    lastRssi_ = static_cast<int>(radio.getRSSI());
    lastSnr_ = radio.getSNR();
    RadioMessage message;
    String error;
    if (!RadioProtocol::deserialize(payload, message, &error)) {
      lastFinishPacketType_ = "PARSE_FAILED";
      Serial.printf("LORA parse failed raw=%s error=%s\n", payload.c_str(), error.c_str());
      return;
    }
    lastFinishPacketType_ = RadioProtocol::typeToString(message.type);
    Serial.printf("LORA RX type=%s rssi=%d snr=%.1f raw=%s\n", lastFinishPacketType_.c_str(), lastRssi_, static_cast<double>(lastSnr_), lastLoRaRaw_.c_str());
    if (message.type == RadioMessageType::Unknown) {
      Serial.printf("LORA unknown type=%s raw=%s\n", lastFinishPacketType_.c_str(), payload.c_str());
    }
    if (message.stationId.length() == 0) {
      Serial.printf("LORA missing stationId raw=%s\n", payload.c_str());
    }
    if (message.stationId == "finish" && message.type != RadioMessageType::Unknown) {
      updateFinishLink(message, lastRssi_, lastSnr_);
    }
    handleRadioMessage(message);
  }
#else
  (void)this;
#endif
}

bool StartStationApp::sendRadio(const RadioMessage& message, int* resultCode) {
#if ENABLE_LORA
  if (!radioReady_) {
    if (resultCode != nullptr) *resultCode = -999;
    return false;
  }

  String payload;
  RadioProtocol::serialize(message, payload);
  if (message.type == RadioMessageType::Status && (message.heartbeat == 0 || message.heartbeat % 5 == 0 || payload.length() > 200)) {
    Serial.printf("STATUS payload len=%u\n", static_cast<unsigned>(payload.length()));
  }
  const int result = radio.transmit(payload);
  if (resultCode != nullptr) *resultCode = result;
  restoreRadioReceiveMode();
  const bool logTxMode = true;
  Serial.println("LoRa RX mode restored");
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[StartStation] LoRa TX failed: %d\n", result);
    return false;
  }
  if (logTxMode) Serial.printf("LoRa TX %s ok\n", RadioProtocol::typeToString(message.type).c_str());
  return true;
#else
  (void)message;
  if (resultCode != nullptr) *resultCode = -1;
  return false;
#endif
}

void StartStationApp::restoreRadioReceiveMode() {
#if ENABLE_LORA
  yield();
  delay(1);
#endif
}

void StartStationApp::sendRunStart(const RunRecord& run) {
  if (runStartAckAttempts_ >= 10) return;
  runStartAckAttempts_ += 1;
  RadioMessage message;
  message.type = RadioMessageType::RunStart;
  message.stationId = "start";
  message.messageId = RadioProtocol::makeMessageId("start");
  message.runId = run.runId;
  message.riderName = run.riderName;
  message.trailName = run.trailName;
  message.startTimestampMs = run.startTimestampMs;
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;

  lastRunStartSendMs_ = clock_.nowMs();
  Serial.printf("RUN_START TX runId=%s startTimestampMs=%lu rider=%s\n", run.runId.c_str(), static_cast<unsigned long>(run.startTimestampMs), run.riderName.c_str());
  Serial.printf("RUN_START TX attempt=%u runId=%s\n", runStartAckAttempts_, run.runId.c_str());
  Serial.printf("TX priority RUN_START attempt=%u\n", runStartAckAttempts_);
  int resultCode = 0;
  if (sendRadio(message, &resultCode)) {
    lastPriorityTxMs_ = clock_.nowMs();
    lastRunStartSendMs_ = lastPriorityTxMs_;
    Serial.printf("RUN_START sent OK runId=%s start=%lu\n", run.runId.c_str(),
                  static_cast<unsigned long>(run.startTimestampMs));
  } else {
    Serial.printf("RUN_START failed code=%d runId=%s\n", resultCode, run.runId.c_str());
  }
}

void StartStationApp::retryRunStartAck(uint32_t nowMs) {
  if (!pendingRunStartAck_) return;
  if (runStartAckAttempts_ >= 10) {
    pendingRunStartAck_ = false;
    runStartAckTimedOut_ = true;
    Serial.printf("RUN_START ACK TIMEOUT attempts=%u\n", runStartAckAttempts_);
#if ENABLE_OLED
    if (!display_.testPatternOnly()) {
      display_.showLines({startShortHeader(), "FINISH DID NOT", "ACK START"});
    }
#endif
    return;
  }
  if (lastRunStartSendMs_ > 0 && nowMs - lastRunStartSendMs_ < 500UL) return;
  sendRunStart(state_.currentRun());
}

bool StartStationApp::priorityTxPending() const {
  const uint32_t nowMs = millis();
  const bool priorityTxRecently = lastPriorityTxMs_ > 0 && nowMs - lastPriorityTxMs_ < 2UL;
  return pendingRunStartAck_ || pendingFinishAckRunId_.length() > 0 || priorityTxRecently;
}

void StartStationApp::sendFinishAck(const RunRecord& run, uint8_t sequence, bool duplicateResend) {
  RadioMessage message;
  message.type = RadioMessageType::FinishAck;
  message.stationId = "start";
  message.messageId = RadioProtocol::makeMessageId("finish-ack");
  message.runId = run.runId;
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.resultMs = run.resultMs;
  message.resultFormatted = run.resultFormatted.length() > 0 ? run.resultFormatted : formatSeconds(run.resultMs);
  message.timestampMs = clock_.nowMs();

  Serial.printf("FINISH_ACK TX runId=%s resultMs=%lu\n", run.runId.c_str(), static_cast<unsigned long>(run.resultMs));
  int resultCode = 0;
  if (sendRadio(message, &resultCode)) {
    lastPriorityTxMs_ = clock_.nowMs();
    lastFinishAckSentMs_ = lastPriorityTxMs_;
    lastFinishAckRunId_ = run.runId;
    if (sequence > finishAckSendCount_) finishAckSendCount_ = sequence;
    Serial.println(duplicateResend ? "FINISH_ACK resend OK" : "FINISH_ACK sent OK");
  } else {
    Serial.printf("FINISH_ACK TX failed code=%d runId=%s\n", resultCode, run.runId.c_str());
  }
}

void StartStationApp::scheduleFinishAckRepeats(const RunRecord& run) {
  pendingFinishAckRunId_ = run.runId;
  pendingFinishAckResultMs_ = run.resultMs;
  pendingFinishAckResultFormatted_ = run.resultFormatted.length() > 0 ? run.resultFormatted : formatSeconds(run.resultMs);
  finishAckSendCount_ = 0;
  lastFinishAckSentMs_ = 0;
  Serial.printf("FINISH_ACK scheduled repeats=%u\n", FINISH_ACK_REPEAT_COUNT);
  sendFinishAck(run, 1);
}

void StartStationApp::processFinishAckRepeats(uint32_t nowMs) {
  if (pendingFinishAckRunId_.length() == 0) return;
  if (finishAckSendCount_ >= FINISH_ACK_REPEAT_COUNT) {
    pendingFinishAckRunId_ = "";
    return;
  }
  if (lastFinishAckSentMs_ > 0 && nowMs - lastFinishAckSentMs_ < FINISH_ACK_REPEAT_INTERVAL_MS) return;

  RunRecord ackRun = state_.lastRun();
  if (ackRun.runId != pendingFinishAckRunId_) {
    ackRun.runId = pendingFinishAckRunId_;
    ackRun.resultMs = pendingFinishAckResultMs_;
    ackRun.resultFormatted = pendingFinishAckResultFormatted_;
  }
  const uint8_t nextSequence = finishAckSendCount_ + 1;
  Serial.printf("FINISH_ACK repeat %u/%u runId=%s\n", nextSequence, FINISH_ACK_REPEAT_COUNT, ackRun.runId.c_str());
  sendFinishAck(ackRun, nextSequence, true);
}

void StartStationApp::sendStatus(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::Status;
  message.stationId = "start";
  message.messageId = RadioProtocol::makeMessageId("start-status");
  message.state = state_.stateText();
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.uptimeMs = nowMs;
  message.timestampMs = nowMs;
  message.heartbeat = startHeartbeatCount_ + 1;
  const RunRecord& current = state_.currentRun();
  if (current.runId.length() > 0) message.runId = current.runId;
  if (current.riderName.length() > 0) message.riderName = current.riderName;
  if (state_.state() == StartRunState::Riding && current.startTimestampMs > 0) message.elapsedMs = nowMs - current.startTimestampMs;
  int resultCode = 0;
  if (sendRadio(message, &resultCode)) {
    startHeartbeatCount_ += 1;
    lastStatusSentOkMs_ = nowMs;
    Serial.printf("START STATUS sent hb=%lu state=%s\n", static_cast<unsigned long>(startHeartbeatCount_), message.state.c_str());
  } else {
    Serial.printf("START STATUS send failed code=%d\n", resultCode);
  }
}

void StartStationApp::sendHello(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::Hello;
  message.stationId = "start";
  message.messageId = RadioProtocol::makeMessageId("hello");
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.role = "StartStation";
  message.uptimeMs = nowMs;
  if (sendRadio(message)) {
    Serial.printf("START HELLO sent bootId=%s\n", bootId_.c_str());
  }
}

void StartStationApp::sendHelloAck(uint32_t nowMs) {
  RadioMessage message;
  message.type = RadioMessageType::HelloAck;
  message.stationId = "start";
  message.messageId = RadioProtocol::makeMessageId("hello-ack");
  message.version = FIRMWARE_VERSION;
  message.bootId = bootId_;
  message.role = "StartStation";
  message.uptimeMs = nowMs;
  if (sendRadio(message)) {
    Serial.printf("START HELLO_ACK sent bootId=%s\n", bootId_.c_str());
  }
}

bool StartStationApp::discoveryActive() const {
  const bool activeRun = state_.state() == StartRunState::Countdown || state_.state() == StartRunState::Riding || state_.state() == StartRunState::Finished;
  return !activeRun && !pendingRunStartAck_ && !isLinkActive(finishLink_);
}

void StartStationApp::updateFinishLink(const RadioMessage& message, int packetRssi, float packetSnr) {
  const String packetType = RadioProtocol::typeToString(message.type);
  const String oldBootId = finishLink_.remoteBootId;
  const bool rebootDetected = updateLinkStatus(finishLink_, message.stationId, packetType, message.bootId, packetRssi, packetSnr);
  if (rebootDetected) {
    Serial.printf("REMOTE REBOOT detected station=finish oldBootId=%s newBootId=%s\n", oldBootId.c_str(), message.bootId.c_str());
    finishHeartbeatCount_ = 0;
    finishActiveRunId_ = "";
    finishRiderName_ = "";
    finishElapsedMs_ = 0;
  }
  Serial.printf("LORA RX from=%s type=%s rssi=%d snr=%.1f age=0 count=%lu\n",
                message.stationId.c_str(), packetType.c_str(), packetRssi, static_cast<double>(packetSnr),
                static_cast<unsigned long>(finishLink_.packetCount));
}

void StartStationApp::handleRadioMessage(const RadioMessage& message) {
  if (message.type == RadioMessageType::Status && message.stationId == "finish") {
    Serial.printf("STATUS RX from=finish hb=%lu rssi=%d\n", static_cast<unsigned long>(message.heartbeat), finishLink_.lastRssi);
    finishLastStatusMs_ = message.timestampMs > 0 ? message.timestampMs : message.uptimeMs;
    finishHeartbeatCount_ = message.heartbeat;
    if (message.state.length() > 0) finishState_ = message.state;
    if (message.version.length() > 0) finishFirmwareVersion_ = message.version;
    if (message.runId.length() > 0) finishActiveRunId_ = message.runId;
    if (message.riderName.length() > 0) finishRiderName_ = message.riderName;
    finishElapsedMs_ = message.elapsedMs;
    finishBatteryAvailable_ = message.hasBatteryVoltage;
    finishBatteryVoltage_ = message.batteryVoltage;
    finishBatteryPercent_ = message.batteryPercent;
    finishLocalRunStartReceivedMillis_ = message.localRunStartReceivedMillis;
    finishLocalElapsedMs_ = message.finishLocalElapsedMs;
    finishRemoteStartTimestampMs_ = message.remoteStartTimestampMs;
    finishReportedStartLinkActive_ = message.startLinkActive && message.startLastSeenAgoMs <= LINK_TIMEOUT_MS;
    finishReportedStartPacketCount_ = message.startPacketCount;
    if (message.hasStartRssi && message.hasStartSnr && finishReportedStartLinkActive_) {
      finishReportedStartRssi_ = message.startRssi;
      finishReportedStartSnr_ = message.startSnr;
      finishReportedStartLastSeenAgoMs_ = message.startLastSeenAgoMs;
      hasFinishReportedStartSignal_ = true;
    } else {
      hasFinishReportedStartSignal_ = false;
    }
    return;
  }

  if (message.type == RadioMessageType::Hello && message.stationId == "finish") {
    lastHelloReceivedMs_ = millis();
    if (message.version.length() > 0) finishFirmwareVersion_ = message.version;
    if (priorityTxPending()) {
      Serial.println("TX deferred HELLO_ACK because priority message pending");
    } else {
      sendHelloAck(millis());
    }
    return;
  }

  if (message.type == RadioMessageType::HelloAck && message.stationId == "finish") {
    lastHelloReceivedMs_ = millis();
    if (message.version.length() > 0) finishFirmwareVersion_ = message.version;
    return;
  }

  if (message.type == RadioMessageType::RunStartAck && message.stationId == "finish") {
    Serial.printf("RUN_START_ACK RX runId=%s\n", message.runId.c_str());
    if (message.runId == state_.currentRun().runId && pendingRunStartAck_) {
      pendingRunStartAck_ = false;
      runStartAckReceived_ = true;
      runStartAckTimedOut_ = false;
      lastRunStartAckMs_ = millis();
      if (message.state.length() > 0) finishState_ = message.state;
      if (message.version.length() > 0) finishFirmwareVersion_ = message.version;
      Serial.printf("RUN_START_ACK received runId=%s\n", message.runId.c_str());
      Serial.println("RUN_START ACK OK");
#if ENABLE_OLED
      if (!display_.testPatternOnly()) {
        display_.showLines({startShortHeader(), "FIN READY", "FIN ACK"});
      }
#endif
    }
    return;
  }

  if (message.type == RadioMessageType::Finish && message.stationId == "finish") {
    Serial.printf("FINISH RX raw=%s\n", lastLoRaRaw_.c_str());
    Serial.printf("FINISH RX finishTimestampMs=%lu startTimestampMs=%lu resultCandidate=%ld\n",
                  static_cast<unsigned long>(message.finishTimestampMs),
                  static_cast<unsigned long>(state_.currentRun().startTimestampMs),
                  static_cast<long>(message.finishTimestampMs) - static_cast<long>(state_.currentRun().startTimestampMs));
    Serial.printf("FINISH parsed runId=%s source=%s\n", message.runId.c_str(), message.source.c_str());
    RunRecord completed;
    if (state_.completeRun(message.runId, message.finishTimestampMs, message.source, completed)) {
      Serial.printf("FINISH resultMs=%lu\n", static_cast<unsigned long>(completed.resultMs));
      Serial.printf("FINISH accepted runId=%s resultMs=%lu\n", completed.runId.c_str(), static_cast<unsigned long>(completed.resultMs));
      Serial.printf("run saved to recentRuns count=%u\n", static_cast<unsigned>(state_.runs().size()));
      appendRunCsv(completed);
#if ENABLE_OLED
      if (!display_.testPatternOnly()) {
        display_.showLines({startHeader(), "FINISHED", completed.resultFormatted, "Rider: " + toDisplayText(completed.riderName, 10), "Trail: " + toDisplayText(completed.trailName, 10)});
      }
#endif
      buzzer_.beep("FINISH");
      led_.flash(2, 100, 120);
      scheduleFinishAckRepeats(completed);
    } else if (state_.lastRun().runId == message.runId && message.runId.length() > 0) {
      RunRecord last = state_.lastRun();
      Serial.printf("Duplicate FINISH RX runId=%s, ACK resent\n", message.runId.c_str());
      sendFinishAck(last, finishAckSendCount_ == 0 ? 1 : finishAckSendCount_, true);
    } else {
      Serial.println("[StartStation] FINISH ignored: unknown runId or state mismatch");
    }
  }
}

bool StartStationApp::finishOnline() const {
  return isLinkActive(finishLink_);
}

uint32_t StartStationApp::finishLastSeenAgoMs() const {
  return linkAgeMs(finishLink_);
}

void StartStationApp::updateCountdownDisplay(uint32_t nowMs) {
  if (state_.state() != StartRunState::Countdown) {
    lastCountdownText_ = "";
    return;
  }

  const String text = state_.countdownText(nowMs);
  if (text.length() == 0 || text == lastCountdownText_) return;

  lastCountdownText_ = text;
  Serial.printf("COUNTDOWN step=%s time=%lu\n", text.c_str(), static_cast<unsigned long>(nowMs));
#if ENABLE_OLED
  if (!display_.testPatternOnly()) {
    display_.showCountdown(text, startShortHeader());
    Serial.printf("OLED countdown rendered step=%s at ms=%lu\n", text.c_str(), static_cast<unsigned long>(millis()));
  }
#endif
}

void StartStationApp::updateDisplay() {
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();
  const String runShort = current.runId.length() > 0 ? current.runId.substring(max(0, static_cast<int>(current.runId.length()) - 6)) : "-";
  const String lastResult = last.resultFormatted.length() > 0 ? last.resultFormatted : "-";
  const BatteryStatus battery = battery_.read();
  const String bat = batteryText(battery);
  const String fin = finishOnline() ? String("FIN:") + String(finishLink_.lastRssi) + "dBm" : String("FIN:NO SIGNAL");
  const String linkDebug = finishLink_.lastPacketType.length() > 0 ? String("PKT:") + finishLink_.lastPacketType : String("HB:") + String(finishHeartbeatCount_);
  const String discoveryLine = discoveryActive() ? String("DISCOVERY...") : linkDebug;

  if (state_.state() == StartRunState::Countdown) {
    display_.showCountdown(state_.countdownText(millis()), startShortHeader());
    return;
  }

  if (state_.state() == StartRunState::Finished) {
    display_.showLines({startHeader(), "FINISHED", lastResult, bat});
    return;
  }

  if (state_.state() == StartRunState::Riding) {
    String anim = String("RIDING ");
    for (uint8_t i = 0; i < ridingAnimationFrame(); ++i) anim += " ";
    anim += ">";
    display_.showLines({startHeader(), anim, formatDurationMs(millis() - current.startTimestampMs).substring(0, 5), fin, bat});
    return;
  }

  if (state_.state() == StartRunState::Error) {
    display_.showLines({startHeader(), "ERROR", fin, discoveryLine});
    return;
  }

  RiderRecord rider = selectRider();
  TrailRecord trail = selectTrail();
  display_.showLines({
    startHeader(),
    fin,
    bat,
    wallClockSynced_ ? String("TIME OK") : String("NO TIME"),
    "Last:" + (last.resultFormatted.length() > 0 ? last.resultFormatted : String("-")),
  });
}

String StartStationApp::finishSignalText() const {
  return linkSignalText(finishLink_);
}

String StartStationApp::finishReportedStartSignalText() const {
  return (hasFinishReportedStartSignal_ && finishReportedStartLinkActive_) ? String(finishReportedStartRssi_) + " dBm" : String("NO SIGNAL");
}

uint8_t StartStationApp::ridingAnimationFrame() const {
  return static_cast<uint8_t>((millis() / 250UL) % 10UL);
}

String StartStationApp::makeBootId(const char* stationId) const {
  const uint64_t mac = ESP.getEfuseMac();
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%s-%08lX%08lX-%lu", stationId,
           static_cast<unsigned long>(mac >> 32), static_cast<unsigned long>(mac & 0xFFFFFFFFULL),
           static_cast<unsigned long>(startBootCounter));
  return String(buffer);
}

void StartStationApp::logHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs_ < 5000UL) return;

  String heartbeatState = state_.stateText();
  heartbeatState.toUpperCase();
  Serial.printf("START alive state=%s uptime=%lu heap=%lu minHeap=%lu finishOnline=%s buttonRaw=%d buttonPressed=%s\n", heartbeatState.c_str(),
                static_cast<unsigned long>(nowMs), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), finishOnline() ? "true" : "false", startButton_.raw(), startButton_.isPressed() ? "true" : "false");
  Serial.printf("START LINK DEBUG:\n  startHbSent=%lu\n  finishActive=%d\n  finishAge=%lu\n  finishRssi=%d\n  finishLastType=%s\n  finishHbReceived=%lu\n",
                static_cast<unsigned long>(startHeartbeatCount_), finishOnline() ? 1 : 0,
                static_cast<unsigned long>(finishLastSeenAgoMs()), finishLink_.lastRssi,
                finishLink_.lastPacketType.c_str(), static_cast<unsigned long>(finishHeartbeatCount_));
  lastHeartbeatMs_ = nowMs;
}

void StartStationApp::loadStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[StartStation] LittleFS unavailable for app storage");
    return;
  }
  loadRiders();
  loadTrails();
  loadSettings();
  ensureDefaults();
}

void StartStationApp::loadRiders() {
  riders_.clear();
  if (!LittleFS.exists("/riders.json")) return;
  File file = LittleFS.open("/riders.json", "r");
  JsonDocument doc;
  if (deserializeJson(doc, file)) { file.close(); return; }
  file.close();
  for (JsonObject item : doc.as<JsonArray>()) {
    RiderRecord rider;
    rider.id = item["riderId"] | "";
    rider.displayName = item["displayName"] | "";
    rider.isActive = item["isActive"] | true;
    rider.createdAtMs = item["createdAtMs"] | 0;
    if (rider.id.length() > 0 && rider.displayName.length() > 0) riders_.push_back(rider);
  }
}

void StartStationApp::loadTrails() {
  trails_.clear();
  if (!LittleFS.exists("/trails.json")) return;
  File file = LittleFS.open("/trails.json", "r");
  JsonDocument doc;
  if (deserializeJson(doc, file)) { file.close(); return; }
  file.close();
  for (JsonObject item : doc.as<JsonArray>()) {
    TrailRecord trail;
    trail.id = item["trailId"] | "";
    trail.displayName = item["displayName"] | "";
    trail.isActive = item["isActive"] | true;
    trail.createdAtMs = item["createdAtMs"] | 0;
    if (trail.id.length() > 0 && trail.displayName.length() > 0) trails_.push_back(trail);
  }
}

void StartStationApp::loadSettings() {
  settings_ = AppSettings{};
  if (!LittleFS.exists("/settings.json")) return;
  File file = LittleFS.open("/settings.json", "r");
  JsonDocument doc;
  if (!deserializeJson(doc, file)) {
    settings_.selectedRiderId = doc["selectedRiderId"] | "";
    settings_.selectedTrailId = doc["selectedTrailId"] | "";
  }
  file.close();
}

void StartStationApp::ensureDefaults() {
  bool changedRiders = false;
  if (riders_.empty()) {
    riders_.push_back({"r001", "Test Rider", true, millis()});
    settings_.selectedRiderId = "r001";
    changedRiders = true;
  }
  bool changedTrails = false;
  if (trails_.empty()) {
    trails_.push_back({"t001", "Default trail", true, millis()});
    settings_.selectedTrailId = "t001";
    changedTrails = true;
  }
  RiderRecord rider;
  if (!findActiveRider(settings_.selectedRiderId, rider)) settings_.selectedRiderId = selectRider().id;
  TrailRecord trail;
  if (!findActiveTrail(settings_.selectedTrailId, trail)) settings_.selectedTrailId = selectTrail().id;
  if (changedRiders) saveRiders();
  if (changedTrails) saveTrails();
  saveSettings();
}

bool StartStationApp::saveRiders() {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const RiderRecord& rider : riders_) {
    JsonObject item = array.add<JsonObject>();
    item["riderId"] = rider.id;
    item["displayName"] = rider.displayName;
    item["isActive"] = rider.isActive;
    item["createdAtMs"] = rider.createdAtMs;
  }
  File file = LittleFS.open("/riders.json", "w");
  if (!file) {
    Serial.println("rider add failed: LittleFS open /riders.json failed");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    Serial.println("rider add failed: LittleFS write /riders.json failed");
    file.close();
    return false;
  }
  file.close();
  loadRiders();
  Serial.printf("riders saved count=%u\n", static_cast<unsigned>(riders_.size()));
  return true;
}

bool StartStationApp::saveTrails() {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const TrailRecord& trail : trails_) {
    JsonObject item = array.add<JsonObject>();
    item["trailId"] = trail.id;
    item["displayName"] = trail.displayName;
    item["isActive"] = trail.isActive;
    item["createdAtMs"] = trail.createdAtMs;
  }
  File file = LittleFS.open("/trails.json", "w");
  if (!file) {
    Serial.println("trail add failed: LittleFS open /trails.json failed");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    Serial.println("trail add failed: LittleFS write /trails.json failed");
    file.close();
    return false;
  }
  file.close();
  loadTrails();
  Serial.printf("trails saved count=%u\n", static_cast<unsigned>(trails_.size()));
  return true;
}

bool StartStationApp::saveSettings() {
  JsonDocument doc;
  doc["selectedRiderId"] = settings_.selectedRiderId;
  doc["selectedTrailId"] = settings_.selectedTrailId;
  File file = LittleFS.open("/settings.json", "w");
  if (!file) {
    Serial.println("settings save failed: LittleFS open /settings.json failed");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    Serial.println("settings save failed: LittleFS write /settings.json failed");
    file.close();
    return false;
  }
  file.close();
  return true;
}

String StartStationApp::ridersJson() const {
  Serial.println("GET /api/riders");
  Serial.printf("riders count=%u\n", static_cast<unsigned>(riders_.size()));
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const RiderRecord& rider : riders_) {
    JsonObject item = array.add<JsonObject>();
    item["riderId"] = rider.id;
    item["displayName"] = rider.displayName;
    item["isActive"] = rider.isActive;
    item["createdAtMs"] = rider.createdAtMs;
  }
  String output;
  serializeJson(doc, output);
  return output;
}

String StartStationApp::trailsJson() const {
  Serial.println("GET /api/trails");
  Serial.printf("trails count=%u\n", static_cast<unsigned>(trails_.size()));
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const TrailRecord& trail : trails_) {
    JsonObject item = array.add<JsonObject>();
    item["trailId"] = trail.id;
    item["displayName"] = trail.displayName;
    item["isActive"] = trail.isActive;
    item["createdAtMs"] = trail.createdAtMs;
  }
  String output;
  serializeJson(doc, output);
  return output;
}

String StartStationApp::settingsJson() const {
  JsonDocument doc;
  doc["selectedRiderId"] = settings_.selectedRiderId;
  doc["selectedTrailId"] = settings_.selectedTrailId;
  String output;
  serializeJson(doc, output);
  return output;
}

bool StartStationApp::addRider(const String& displayName, String& error, RiderRecord* addedRider) {
  String name = displayName;
  name.trim();
  Serial.printf("rider add request name=%s\n", name.c_str());
  if (name.length() == 0) { error = "Rider name is required"; Serial.printf("rider add failed: %s\n", error.c_str()); return false; }
  RiderRecord newRider{makeEntityId("r"), name, true, millis()};
  riders_.push_back(newRider);
  if (settings_.selectedRiderId.length() == 0) settings_.selectedRiderId = newRider.id;
  if (!saveRiders()) { error = "Failed to save riders"; return false; }
  if (addedRider != nullptr) *addedRider = newRider;
  if (!saveSettings()) { error = "Failed to save settings"; Serial.printf("rider add failed: %s\n", error.c_str()); return false; }
  return true;
}

bool StartStationApp::deactivateRider(const String& riderId, String& error) {
  for (RiderRecord& rider : riders_) {
    if (rider.id == riderId) {
      rider.isActive = false;
      if (settings_.selectedRiderId == riderId) settings_.selectedRiderId = selectRider().id;
      saveRiders(); saveSettings(); return true;
    }
  }
  error = "rider not found";
  return false;
}

bool StartStationApp::addTrail(const String& displayName, String& error, TrailRecord* addedTrail) {
  String name = displayName;
  name.trim();
  Serial.printf("trail add request name=%s\n", name.c_str());
  if (name.length() == 0) { error = "Trail name is required"; Serial.printf("trail add failed: %s\n", error.c_str()); return false; }
  const bool isFirstTrail = trails_.empty();
  TrailRecord newTrail{makeEntityId("t"), name, true, millis()};
  trails_.push_back(newTrail);
  if (isFirstTrail || settings_.selectedTrailId.length() == 0) settings_.selectedTrailId = newTrail.id;
  if (!saveTrails()) { error = "Failed to save trails"; return false; }
  if (addedTrail != nullptr) *addedTrail = newTrail;
  if (!saveSettings()) { error = "Failed to save settings"; Serial.printf("trail add failed: %s\n", error.c_str()); return false; }
  return true;
}

bool StartStationApp::deactivateTrail(const String& trailId, String& error) {
  for (TrailRecord& trail : trails_) {
    if (trail.id == trailId) {
      trail.isActive = false;
      if (settings_.selectedTrailId == trailId) settings_.selectedTrailId = selectTrail().id;
      saveTrails(); saveSettings(); return true;
    }
  }
  error = "trail not found";
  return false;
}

bool StartStationApp::updateSettings(const String& selectedRiderId, const String& selectedTrailId, String& error) {
  RiderRecord rider;
  TrailRecord trail;
  if (selectedRiderId.length() > 0 && !findActiveRider(selectedRiderId, rider)) { error = "selected rider is inactive or missing"; return false; }
  if (selectedTrailId.length() > 0 && !findActiveTrail(selectedTrailId, trail)) { error = "selected trail is inactive or missing"; return false; }
  if (selectedRiderId.length() > 0) settings_.selectedRiderId = selectedRiderId;
  if (selectedTrailId.length() > 0) settings_.selectedTrailId = selectedTrailId;
  saveSettings();
  return true;
}

bool StartStationApp::findActiveRider(const String& id, RiderRecord& rider) const {
  for (const RiderRecord& item : riders_) if (item.id == id && item.isActive) { rider = item; return true; }
  return false;
}

bool StartStationApp::findActiveTrail(const String& id, TrailRecord& trail) const {
  for (const TrailRecord& item : trails_) if (item.id == id && item.isActive) { trail = item; return true; }
  return false;
}

RiderRecord StartStationApp::selectRider() const {
  RiderRecord rider;
  if (findActiveRider(settings_.selectedRiderId, rider)) return rider;
  for (const RiderRecord& item : riders_) if (item.isActive) return item;
  return {"", "Test Rider", true, millis()};
}

TrailRecord StartStationApp::selectTrail() const {
  TrailRecord trail;
  if (findActiveTrail(settings_.selectedTrailId, trail)) return trail;
  for (const TrailRecord& item : trails_) if (item.isActive) return item;
  return {"t-default", "Default trail", true, millis()};
}

String StartStationApp::makeEntityId(const char* prefix) const {
  return String(prefix) + String(millis(), HEX);
}

String StartStationApp::escapeCsv(const String& value) const {
  String out = value;
  out.replace("\"", "\"\"");
  return String("\"") + out + "\"";
}

bool StartStationApp::appendRunCsv(const RunRecord& run) const {
  const bool exists = LittleFS.exists("/runs.csv");
  File file = LittleFS.open("/runs.csv", "a");
  if (!file) {
    Serial.println("runs.csv append FAIL open");
    return false;
  }
  if (!exists || file.size() == 0) {
    file.write(0xEF); file.write(0xBB); file.write(0xBF);
    file.println("RunNumber;RunId;RunTime;Rider;Trail;StartMs;FinishMs;ResultMs;Result;Status;Source;TimingSource");
  }
  file.print(run.runNumber); file.print(';');
  file.print(escapeCsv(run.runId)); file.print(';');
  file.print(escapeCsv(run.startedAtText.length() > 0 ? run.startedAtText : String("TIME NOT SYNCED"))); file.print(';');
  file.print(escapeCsv(run.riderName)); file.print(';');
  file.print(escapeCsv(run.trailName)); file.print(';');
  file.print(run.startTimestampMs); file.print(';');
  file.print(run.finishTimestampMs); file.print(';');
  file.print(run.resultMs); file.print(';');
  file.print(run.resultFormatted); file.print(';');
  file.print(run.status); file.print(';');
  file.print(run.finishSource); file.print(';');
  file.println(run.timingSource);
  file.close();
  Serial.println("runs.csv append OK");
  return true;
}

String StartStationApp::runsCsv() const {
  if (!LittleFS.exists("/runs.csv")) return String("\xEF\xBB\xBFRunNumber;RunId;RunTime;Rider;Trail;StartMs;FinishMs;ResultMs;Result;Status;Source;TimingSource\n");
  File file = LittleFS.open("/runs.csv", "r");
  String content = file.readString();
  file.close();
  return content;
}

bool StartStationApp::syncBrowserTime(uint64_t epochMs, int timezoneOffsetMinutes, const String& isoLocal, String& error) {
  if (epochMs == 0) {
    error = "epochMs is required";
    return false;
  }
  wallClockSynced_ = true;
  wallClockEpochMsAtSync_ = epochMs;
  localMillisAtSync_ = millis();
  timezoneOffsetMinutes_ = timezoneOffsetMinutes;
  lastTimeSyncText_ = isoLocal.length() > 0 ? isoLocal : formatEpochLocal(epochMs);
  timeSource_ = "BROWSER";
  Serial.printf("TIME SYNC received epochMs=%llu timezoneOffsetMinutes=%d isoLocal=%s\n", static_cast<unsigned long long>(epochMs), timezoneOffsetMinutes_, lastTimeSyncText_.c_str());
  Serial.println("wallClockSynced=true");
  Serial.printf("currentTimeText=%s\n", currentTimeText().c_str());
  return true;
}

uint64_t StartStationApp::currentEpochMs() const {
  if (!wallClockSynced_) return 0;
  return wallClockEpochMsAtSync_ + static_cast<uint32_t>(millis() - localMillisAtSync_);
}

String StartStationApp::currentTimeText() const {
  if (!wallClockSynced_) return "TIME NOT SYNCED";
  return formatEpochLocal(currentEpochMs());
}

String StartStationApp::formatEpochLocal(uint64_t epochMs) const {
  const int64_t localSeconds = static_cast<int64_t>(epochMs / 1000ULL) - (static_cast<int64_t>(timezoneOffsetMinutes_) * 60LL);
  time_t raw = static_cast<time_t>(localSeconds);
  struct tm tmValue;
  gmtime_r(&raw, &tmValue);
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
  return String(buffer);
}

String StartStationApp::batteryText(const BatteryStatus& status) const {
  if (status.available && status.percent >= 0) return String("BAT:") + String(status.percent) + "%";
  return "BAT:USB";
}
