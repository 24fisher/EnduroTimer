#include "FinishStationApp.h"

#include <SPI.h>

#include "RadioProtocol.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint8_t MaxFinishAttempts = 5;
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

void FinishStationApp::begin() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[FinishStation] boot");

  display_.begin();
  display_.showBoot("FINISH");
  buzzer_.begin();
  beginRadio();
  state_.begin();
}

void FinishStationApp::loop() {
  pollRadio();

  uint32_t finishTimestampMs = 0;
  if (sensor_.update(clock_.nowMs(), finishTimestampMs)) {
    Serial.printf("[FinishStation] simulated finish fired: %lu\n", static_cast<unsigned long>(finishTimestampMs));
    state_.markFinishSent(finishTimestampMs);
    finishAttempts_ = 0;
    lastFinishSendMs_ = 0;
    sendFinish();
  }

  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ < MaxFinishAttempts &&
      clock_.nowMs() - lastFinishSendMs_ >= 1000UL) {
    sendFinish();
  }

  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ >= MaxFinishAttempts) {
    Serial.println("[FinishStation] FINISH attempts exceeded");
    state_.fail();
  }

  if (clock_.nowMs() - lastStatusMs_ >= 3000UL) {
    sendStatus();
    lastStatusMs_ = clock_.nowMs();
  }

  if (clock_.nowMs() - lastDisplayMs_ >= 500UL) {
    updateDisplay();
    lastDisplayMs_ = clock_.nowMs();
  }
}

void FinishStationApp::beginRadio() {
  Serial.println("[FinishStation] initializing LoRa SX1262");
  SPI.begin(9, 11, 10, LORA_NSS);
  const int initState = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = initState == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("[FinishStation] LoRa init failed: %d\n", initState);
    return;
  }

  radio.setBandwidth(125.0);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setOutputPower(14);
  Serial.println("[FinishStation] LoRa initialized");
}

void FinishStationApp::pollRadio() {
  if (!radioReady_) return;

  String payload;
  const int rxState = radio.receive(payload, 0);
  if (rxState == RADIOLIB_ERR_NONE) {
    RadioMessage message;
    String error;
    if (!RadioProtocol::deserialize(payload, message, &error)) {
      Serial.printf("[FinishStation] invalid packet: %s payload=%s\n", error.c_str(), payload.c_str());
      return;
    }
    lastPacket_ = RadioProtocol::typeToString(message.type);
    handleRadioMessage(message);
  }
}

bool FinishStationApp::sendRadio(const RadioMessage& message) {
  if (!radioReady_) return false;

  String payload;
  RadioProtocol::serialize(message, payload);
  const int result = radio.transmit(payload);
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[FinishStation] LoRa TX failed: %d\n", result);
    return false;
  }
  return true;
}

void FinishStationApp::sendStatus() {
  RadioMessage message;
  message.type = RadioMessageType::Status;
  message.messageId = RadioProtocol::makeMessageId("finish-status");
  message.stationId = "finish";
  message.state = state_.stateText();
  message.beamClear = true;
  message.timestampMs = clock_.nowMs();
  if (sendRadio(message)) {
    Serial.printf("[FinishStation] STATUS sent: %s\n", message.state.c_str());
  }
}

void FinishStationApp::sendFinish() {
  RadioMessage message;
  message.type = RadioMessageType::Finish;
  message.messageId = RadioProtocol::makeMessageId("finish");
  message.runId = state_.runId();
  message.finishTimestampMs = state_.finishTimestampMs();
  message.source = "SIMULATED_SENSOR_20S";
  if (sendRadio(message)) {
    finishAttempts_ += 1;
    lastFinishSendMs_ = clock_.nowMs();
    Serial.printf("[FinishStation] FINISH sent: run=%s attempt=%u/%u\n", state_.runId().c_str(),
                  finishAttempts_, MaxFinishAttempts);
  }
}

void FinishStationApp::handleRadioMessage(const RadioMessage& message) {
  if (message.type == RadioMessageType::Ping) {
    RadioMessage pong;
    pong.type = RadioMessageType::Pong;
    pong.messageId = RadioProtocol::makeMessageId("pong");
    pong.stationId = "finish";
    pong.timestampMs = clock_.nowMs();
    sendRadio(pong);
    return;
  }

  if (message.type == RadioMessageType::RunStart) {
    Serial.printf("[FinishStation] RUN_START received: run=%s start=%lu\n", message.runId.c_str(),
                  static_cast<unsigned long>(message.startTimestampMs));
    state_.startRun(message.runId, message.startTimestampMs);
    sensor_.arm(message.runId, message.startTimestampMs);
    finishAttempts_ = 0;
    buzzer_.beep("RUN_START");
    return;
  }

  if (message.type == RadioMessageType::FinishAck && message.runId == state_.runId()) {
    Serial.printf("[FinishStation] FINISH_ACK received: run=%s\n", message.runId.c_str());
    sensor_.reset();
    state_.ackFinish();
    finishAttempts_ = 0;
  }
}

void FinishStationApp::updateDisplay() {
  const String runShort = state_.runId().length() > 0 ? state_.runId().substring(max(0, static_cast<int>(state_.runId().length()) - 6)) : "-";
  const uint32_t simSeconds = min<uint32_t>(20, sensor_.elapsedMs(clock_.nowMs()) / 1000UL);

  display_.showLines({
    "FINISH",
    String("LoRa ") + (radioReady_ ? "OK" : "ERR"),
    "State: " + state_.stateText(),
    "Run: " + runShort,
    "Sim: " + String(simSeconds) + "/20s",
    "Sent: " + String(finishAttempts_) + "/5 " + lastPacket_,
  });
}
