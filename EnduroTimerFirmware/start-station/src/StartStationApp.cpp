#include "StartStationApp.h"

#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_system.h>

#include "RadioProtocol.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint32_t FinishOfflineTimeoutMs = 6000UL;
static constexpr uint32_t DisplayRefreshMs = 200UL;
static constexpr uint32_t ButtonDebounceMs = 70UL;
RTC_DATA_ATTR static uint32_t startBootCounter = 0;
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

void StartStationApp::begin() {
  Serial.begin(115200);
  delay(300);
  startBootCounter += 1;

  Serial.println();
  Serial.println("START STATION BOOT");
  Serial.println("Firmware: EnduroTimer StartStation");
  Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
  Serial.println("Serial: OK");
  Serial.println("Board/role: Heltec WiFi LoRa 32 V3 / StartStation");
  Serial.printf("Boot counter: %lu\n", static_cast<unsigned long>(startBootCounter));
  Serial.printf("Chip: %s rev %u cores=%u efuseMac=%llX\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getEfuseMac());

  display_.begin();
  display_.showBoot("START STATION");
  delay(1200);

  buzzer_.begin();
  beginRadio();
  configureButton();
  state_.begin();
  Serial.printf("State: %s\n", state_.stateText().c_str());
}

void StartStationApp::loop() {
  const uint32_t now = clock_.nowMs();
  pollRadio();
  updateButton(now);
  updateLed(now);

  RunRecord runToStart;
  if (state_.updateCountdown(now, runToStart)) {
    buzzer_.beep("GO");
    sendRunStart(runToStart);
  }

  state_.tickAutoReady(now);

  if (now - lastDisplayMs_ >= DisplayRefreshMs) {
    updateDisplay();
    lastDisplayMs_ = now;
  }
}

bool StartStationApp::requestStartRun(String& error) {
  Serial.println("Start run requested");
  return state_.startCountdown(error);
}

void StartStationApp::resetSystem() {
  Serial.println("[StartStation] system reset requested");
  state_.resetActiveRun();
}

void StartStationApp::setWifiStatus(bool apStarted, const IPAddress& ip, const String& mac) {
  wifiApStarted_ = apStarted;
  wifiIp_ = ip;
  wifiMac_ = mac;
  if (!wifiApStarted_) {
    state_.setError();
    Serial.println("WiFi AP failed.");
  }
  updateDisplay();
}

String StartStationApp::statusJson() const {
  JsonDocument doc;
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();

  doc["device"] = "StartStation";
  doc["state"] = state_.stateText();
  doc["finishStationOnline"] = finishOnline();
  doc["finishState"] = finishState_;
  if (radioReady_) {
    doc["loraLastRssi"] = lastRssi_;
    doc["loraLastSnr"] = lastSnr_;
  } else {
    doc["loraLastRssi"] = nullptr;
    doc["loraLastSnr"] = nullptr;
  }
  if (current.runId.length() > 0) {
    doc["currentRunId"] = current.runId;
  } else {
    doc["currentRunId"] = nullptr;
  }
  doc["currentRiderName"] = current.riderName.length() > 0 ? current.riderName : "Test Rider";
  doc["countdownText"] = state_.countdownText(millis());
  doc["lastResultMs"] = last.resultMs > 0 ? last.resultMs : 0;
  doc["lastResultFormatted"] = last.resultFormatted;
  doc["lastFinishSource"] = last.finishSource;
  doc["uptimeMs"] = millis();

  String output;
  serializeJson(doc, output);
  return output;
}

String StartStationApp::runsJson() const {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (const RunRecord& run : state_.runs()) {
    JsonObject item = array.add<JsonObject>();
    item["runId"] = run.runId;
    item["riderName"] = run.riderName;
    item["startTimestampMs"] = run.startTimestampMs;
    item["finishTimestampMs"] = run.finishTimestampMs;
    item["resultMs"] = run.resultMs;
    item["resultFormatted"] = run.resultFormatted;
    item["status"] = run.status;
  }

  String output;
  serializeJson(doc, output);
  return output;
}

void StartStationApp::beginRadio() {
  Serial.printf("LoRa: init %.0f MHz...\n", LORA_FREQUENCY_MHZ);
  SPI.begin(9, 11, 10, LORA_NSS);
  const int state = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = state == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("LoRa: init failed (%d)\n", state);
    return;
  }

  radio.setBandwidth(125.0);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setOutputPower(14);
  Serial.println("LoRa: OK");
}

void StartStationApp::configureButton() {
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  buttonStableState_ = digitalRead(START_BUTTON_PIN);
  buttonLastReading_ = buttonStableState_;
  buttonLastChangeMs_ = millis();
  Serial.printf("Button: configured START_BUTTON_PIN=%d INPUT_PULLUP\n", START_BUTTON_PIN);
}

void StartStationApp::updateButton(uint32_t nowMs) {
  const int reading = digitalRead(START_BUTTON_PIN);
  if (reading != buttonLastReading_) {
    buttonLastReading_ = reading;
    buttonLastChangeMs_ = nowMs;
  }

  if (nowMs - buttonLastChangeMs_ < ButtonDebounceMs || reading == buttonStableState_) return;

  buttonStableState_ = reading;
  if (buttonStableState_ == LOW && !buttonPressConsumed_) {
    buttonPressConsumed_ = true;
    Serial.println("START BUTTON pressed");
    Serial.println("Start run requested from physical button");
    String error;
    if (!requestStartRun(error)) {
      Serial.printf("[StartStation] physical start ignored: %s\n", error.c_str());
    }
  } else if (buttonStableState_ == HIGH) {
    buttonPressConsumed_ = false;
  }
}

void StartStationApp::updateLed(uint32_t nowMs) {
#ifdef LED_BUILTIN
  static bool configured = false;
  if (!configured) {
    pinMode(LED_BUILTIN, OUTPUT);
    configured = true;
  }
  uint32_t interval = 1000UL;
  if (state_.state() == StartRunState::Boot) interval = 120UL;
  if (state_.state() == StartRunState::Countdown || state_.state() == StartRunState::Riding) interval = 180UL;
  if (state_.state() == StartRunState::Error) interval = 100UL;
  if (nowMs - lastLedMs_ >= interval) {
    ledOn_ = !ledOn_;
    digitalWrite(LED_BUILTIN, ledOn_ ? HIGH : LOW);
    lastLedMs_ = nowMs;
  }
#else
  (void)nowMs;
#endif
}

void StartStationApp::pollRadio() {
  if (!radioReady_) return;

  String payload;
  const int state = radio.receive(payload, 0);
  if (state == RADIOLIB_ERR_NONE) {
    lastRssi_ = radio.getRSSI();
    lastSnr_ = radio.getSNR();
    RadioMessage message;
    String error;
    if (!RadioProtocol::deserialize(payload, message, &error)) {
      Serial.printf("[StartStation] invalid packet: %s payload=%s\n", error.c_str(), payload.c_str());
      return;
    }
    handleRadioMessage(message);
  }
}

bool StartStationApp::sendRadio(const RadioMessage& message) {
  if (!radioReady_) return false;

  String payload;
  RadioProtocol::serialize(message, payload);
  const int result = radio.transmit(payload);
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[StartStation] LoRa TX failed: %d\n", result);
    return false;
  }
  return true;
}

void StartStationApp::sendRunStart(const RunRecord& run) {
  RadioMessage message;
  message.type = RadioMessageType::RunStart;
  message.messageId = RadioProtocol::makeMessageId("start");
  message.runId = run.runId;
  message.riderName = run.riderName;
  message.startTimestampMs = run.startTimestampMs;

  if (sendRadio(message)) {
    Serial.printf("[StartStation] RUN_START sent: %s start=%lu\n", run.runId.c_str(),
                  static_cast<unsigned long>(run.startTimestampMs));
  }
}

void StartStationApp::sendFinishAck(const String& runId) {
  RadioMessage message;
  message.type = RadioMessageType::FinishAck;
  message.messageId = RadioProtocol::makeMessageId("ack");
  message.runId = runId;
  if (sendRadio(message)) {
    Serial.printf("[StartStation] FINISH_ACK sent: %s\n", runId.c_str());
  }
}

void StartStationApp::handleRadioMessage(const RadioMessage& message) {
  if (message.type == RadioMessageType::Status) {
    lastFinishSeenMs_ = millis();
    if (message.state.length() > 0) finishState_ = message.state;
    Serial.printf("[StartStation] STATUS received: state=%s run=%s rssi=%.1f snr=%.1f\n",
                  finishState_.c_str(), message.runId.c_str(), lastRssi_, lastSnr_);
    return;
  }

  if (message.type == RadioMessageType::Finish) {
    Serial.printf("[StartStation] FINISH received: run=%s finish=%lu source=%s\n", message.runId.c_str(),
                  static_cast<unsigned long>(message.finishTimestampMs), message.source.c_str());
    RunRecord completed;
    if (state_.completeRun(message.runId, message.finishTimestampMs, message.source, completed)) {
      Serial.printf("[StartStation] result calculated: %s\n", completed.resultFormatted.c_str());
      buzzer_.beep("FINISH");
    } else {
      Serial.println("[StartStation] FINISH ignored: run/state mismatch");
    }
    sendFinishAck(message.runId);
  }
}

bool StartStationApp::finishOnline() const {
  return lastFinishSeenMs_ > 0 && millis() - lastFinishSeenMs_ <= FinishOfflineTimeoutMs;
}

void StartStationApp::updateDisplay() {
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();
  const String runShort = current.runId.length() > 0 ? current.runId.substring(max(0, static_cast<int>(current.runId.length()) - 6)) : "-";
  const String lastResult = last.resultFormatted.length() > 0 ? last.resultFormatted : "-";

  if (state_.state() == StartRunState::Countdown) {
    display_.showCountdown(state_.countdownText(millis()));
    return;
  }

  if (state_.state() == StartRunState::Finished) {
    display_.showLines({"FINISHED", lastResult, "Run: " + runShort, "Finish: " + last.finishSource});
    return;
  }

  display_.showLines({
    "START",
    wifiApStarted_ ? "AP: EnduroTimer" : "WIFI FAIL",
    "IP: " + (wifiApStarted_ ? wifiIp_.toString() : String("-")),
    String("LoRa: ") + (radioReady_ ? "OK" : "OFF"),
    String("Finish: ") + (finishOnline() ? "OK" : "OFF"),
    "State: " + state_.stateText(),
  });
}
