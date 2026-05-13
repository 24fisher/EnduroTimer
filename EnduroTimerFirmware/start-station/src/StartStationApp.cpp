#include "StartStationApp.h"

#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFi.h>

#include "RadioProtocol.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

void StartStationApp::begin() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[StartStation] boot");

  display_.begin();
  display_.showBoot("START");
  buzzer_.begin();
  beginRadio();
  state_.begin();
}

void StartStationApp::loop() {
  pollRadio();

  RunRecord runToStart;
  if (state_.updateCountdown(clock_.nowMs(), runToStart)) {
    buzzer_.beep("GO");
    sendRunStart(runToStart);
  }

  state_.tickAutoReady(clock_.nowMs());

  if (clock_.nowMs() - lastDisplayMs_ >= 500UL) {
    updateDisplay();
    lastDisplayMs_ = clock_.nowMs();
  }
}

bool StartStationApp::requestStartRun(String& error) {
  Serial.println("[StartStation] start requested");
  return state_.startCountdown(error);
}

void StartStationApp::resetSystem() {
  Serial.println("[StartStation] system reset requested");
  state_.resetActiveRun();
}

String StartStationApp::statusJson() const {
  JsonDocument doc;
  const bool finishOnline = lastFinishSeenMs_ > 0 && millis() - lastFinishSeenMs_ <= 10000UL;
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();

  doc["device"] = "StartStation";
  doc["state"] = state_.stateText();
  doc["finishStationOnline"] = finishOnline;
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
  Serial.println("[StartStation] initializing LoRa SX1262");
  SPI.begin(9, 11, 10, LORA_NSS);
  const int state = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = state == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("[StartStation] LoRa init failed: %d\n", state);
    return;
  }

  radio.setBandwidth(125.0);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setOutputPower(14);
  Serial.println("[StartStation] LoRa initialized");
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
  if (message.type == RadioMessageType::Status || message.type == RadioMessageType::Pong) {
    lastFinishSeenMs_ = millis();
    if (message.state.length() > 0) finishState_ = message.state;
    Serial.printf("[StartStation] STATUS/PONG received: state=%s rssi=%.1f snr=%.1f\n",
                  finishState_.c_str(), lastRssi_, lastSnr_);
    return;
  }

  if (message.type == RadioMessageType::Finish) {
    Serial.printf("[StartStation] FINISH received: run=%s finish=%lu source=%s\n", message.runId.c_str(),
                  static_cast<unsigned long>(message.finishTimestampMs), message.source.c_str());
    RunRecord completed;
    if (state_.completeRun(message.runId, message.finishTimestampMs, message.source, completed)) {
      Serial.printf("[StartStation] result calculated: %s\n", completed.resultFormatted.c_str());
      buzzer_.beep("FINISH");
    }
    sendFinishAck(message.runId);
  }
}

void StartStationApp::updateDisplay() {
  const bool finishOnline = lastFinishSeenMs_ > 0 && millis() - lastFinishSeenMs_ <= 10000UL;
  const RunRecord& current = state_.currentRun();
  const RunRecord& last = state_.lastRun();
  const String runShort = current.runId.length() > 0 ? current.runId.substring(max(0, static_cast<int>(current.runId.length()) - 6)) : "-";
  const String lastResult = last.resultFormatted.length() > 0 ? last.resultFormatted : "-";

  display_.showLines({
    "START",
    "AP " + WiFi.softAPIP().toString(),
    String("FIN ") + (finishOnline ? "OK" : "OFF") + " LoRa " + (radioReady_ ? "OK" : "ERR"),
    "State: " + state_.stateText(),
    state_.state() == StartRunState::Countdown ? "COUNT " + state_.countdownText(millis()) : "Run: " + runShort,
    "Last: " + lastResult,
  });
}
