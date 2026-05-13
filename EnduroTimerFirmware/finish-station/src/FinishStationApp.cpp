#include "FinishStationApp.h"

#include <SPI.h>
#include <esp_system.h>

#include "RadioProtocol.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint8_t MaxFinishAttempts = 5;
static constexpr uint32_t StatusIntervalMs = 1000UL;
static constexpr uint32_t DisplayRefreshMs = 200UL;
RTC_DATA_ATTR static uint32_t finishBootCounter = 0;
#if ENABLE_LORA
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
#endif

void FinishStationApp::begin() {
  finishBootCounter += 1;

  Serial.println("Firmware: EnduroTimer FinishStation");
  Serial.println("Board/role: Heltec WiFi LoRa 32 V3 / FinishStation");
  Serial.printf("Boot counter: %lu\n", static_cast<unsigned long>(finishBootCounter));
  Serial.printf("Chip: %s rev %u cores=%u efuseMac=%llX\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getEfuseMac());
  Serial.println("Power save: disabled");

#if ENABLE_OLED
  Serial.println("[BOOT] OLED init...");
  oledReady_ = display_.begin();
  Serial.println(oledReady_ ? "[BOOT] OLED OK" : "[BOOT] OLED FAIL");
  if (oledReady_) {
    display_.showBootScreen("FINISH");
  }
#else
  Serial.println("[BOOT] OLED init skipped (ENABLE_OLED=0)");
  oledReady_ = false;
#endif

  Serial.println("[BOOT] Finish button init...");
  sensor_.begin();
  Serial.println("[BOOT] Finish button OK");
  buzzer_.begin();
  beginRadio();
  state_.begin();
  Serial.println("[BOOT] State Idle");
}

void FinishStationApp::loop() {
  const uint32_t now = clock_.nowMs();
#if ENABLE_LORA
  pollRadio();
#endif
  updateLed(now);

  uint32_t finishTimestampMs = 0;
  if (sensor_.update(now, finishTimestampMs)) {
    if (state_.state() == FinishRunState::WaitFinish) {
      state_.markFinishSent(finishTimestampMs);
      finishAttempts_ = 0;
      lastFinishSendMs_ = 0;
      sendFinish();
#if ENABLE_OLED
      if (!display_.testPatternOnly()) {
        display_.showLines({"FINISH SENT", "Run: " + state_.runId(), "Sent: " + String(finishAttempts_) + "/5"});
      }
#endif
    } else if (state_.state() == FinishRunState::Idle) {
      Serial.println("Finish ignored: state=Idle");
      Serial.println("button ignored: no active run");
      Serial.println("button ignored: state=Idle");
      showNoRunUntilMs_ = now + 1200UL;
    } else {
      Serial.printf("button ignored: state=%s\n", state_.stateText().c_str());
    }
  }

  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ < MaxFinishAttempts &&
      now - lastFinishSendMs_ >= 1000UL) {
    sendFinish();
  }

  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ >= MaxFinishAttempts &&
      now - lastFinishSendMs_ >= 1000UL) {
    Serial.println("FINISH_ACK timeout.");
    state_.fail();
  }

  if (now - lastStatusMs_ >= StatusIntervalMs) {
    sendStatus();
    lastStatusMs_ = now;
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

void FinishStationApp::beginRadio() {
#if ENABLE_LORA
  Serial.println("[BOOT] LoRa init...");
  Serial.printf("LoRa init... %.0f MHz\n", LORA_FREQUENCY_MHZ);
  SPI.begin(9, 11, 10, LORA_NSS);
  const int initState = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = initState == RADIOLIB_ERR_NONE;
  if (!radioReady_) {
    Serial.printf("LoRa FAIL (%d)\n", initState);
    Serial.println("[BOOT] LoRa FAIL");
    return;
  }

  radio.setBandwidth(125.0);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setOutputPower(14);
  Serial.println("LoRa OK");
  Serial.println("[BOOT] LoRa OK");
#else
  Serial.println("[BOOT] LoRa init skipped (ENABLE_LORA=0)");
  radioReady_ = false;
#endif
}

void FinishStationApp::updateLed(uint32_t nowMs) {
#ifdef LED_BUILTIN
  static bool configured = false;
  if (!configured) {
    pinMode(LED_BUILTIN, OUTPUT);
    configured = true;
  }
  uint32_t interval = 1000UL;
  if (state_.state() == FinishRunState::Boot) interval = 120UL;
  if (state_.state() == FinishRunState::WaitFinish || state_.state() == FinishRunState::FinishSent) interval = 180UL;
  if (state_.state() == FinishRunState::Error) interval = 100UL;
  if (nowMs - lastLedMs_ >= interval) {
    ledOn_ = !ledOn_;
    digitalWrite(LED_BUILTIN, ledOn_ ? HIGH : LOW);
    lastLedMs_ = nowMs;
  }
#else
  (void)nowMs;
#endif
}

void FinishStationApp::pollRadio() {
#if ENABLE_LORA
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
#else
  (void)this;
#endif
}

bool FinishStationApp::sendRadio(const RadioMessage& message) {
#if ENABLE_LORA
  if (!radioReady_) return false;

  String payload;
  RadioProtocol::serialize(message, payload);
  const int result = radio.transmit(payload);
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[FinishStation] LoRa TX failed: %d\n", result);
    return false;
  }
  return true;
#else
  (void)message;
  return false;
#endif
}

void FinishStationApp::sendStatus() {
  RadioMessage message;
  message.type = RadioMessageType::Status;
  message.messageId = RadioProtocol::makeMessageId("finish-status");
  message.stationId = "finish";
  message.state = state_.stateText();
  message.source = "FinishStation";
  message.beamClear = true;
  message.buttonReady = true;
  message.timestampMs = clock_.nowMs();
  message.uptimeMs = message.timestampMs;
  message.heartbeat = heartbeatCounter_ + 1;
  if (state_.runId().length() > 0) message.runId = state_.runId();
  if (sendRadio(message)) {
    heartbeatCounter_ += 1;
    Serial.printf("[FinishStation] STATUS sent: %s heartbeat=%lu\n", message.state.c_str(), static_cast<unsigned long>(heartbeatCounter_));
  }
}

void FinishStationApp::sendFinish() {
  RadioMessage message;
  message.type = RadioMessageType::Finish;
  message.messageId = RadioProtocol::makeMessageId("finish");
  message.runId = state_.runId();
  message.finishTimestampMs = state_.finishTimestampMs();
  message.source = "BUTTON_STUB";
  finishAttempts_ += 1;
  lastFinishSendMs_ = clock_.nowMs();
  if (sendRadio(message)) {
    Serial.printf("[FinishStation] FINISH sent: run=%s attempt=%u/%u\n", state_.runId().c_str(),
                  finishAttempts_, MaxFinishAttempts);
  } else {
    Serial.printf("[FinishStation] FINISH send failed: run=%s attempt=%u/%u\n", state_.runId().c_str(),
                  finishAttempts_, MaxFinishAttempts);
  }
}

void FinishStationApp::handleRadioMessage(const RadioMessage& message) {
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
    showAckOkUntilMs_ = millis() + 1500UL;
#if ENABLE_OLED
    if (!display_.testPatternOnly()) {
      display_.showLines({"FINISH ACK", "IDLE"});
    }
#endif
  }
}

void FinishStationApp::updateDisplay() {
  const String runShort = state_.runId().length() > 0 ? state_.runId().substring(max(0, static_cast<int>(state_.runId().length()) - 6)) : "-";

  if (showAckOkUntilMs_ > millis()) {
    display_.showLines({"FINISH ACK", "IDLE"});
    return;
  }

  if (showNoRunUntilMs_ > millis()) {
    display_.showLines({"NO RUN", "Press start", "on StartStation"});
    return;
  }

  if (state_.state() == FinishRunState::Error) {
    display_.showLines({"ERROR", String("LoRa: ") + (radioReady_ ? "OK" : "OFF"), "ACK TIMEOUT", "Run: " + runShort, "Sent: " + String(finishAttempts_) + "/5"});
    return;
  }

  if (state_.state() == FinishRunState::WaitFinish) {
    display_.showLines({"WAIT FINISH", "Run: " + runShort, "Btn: finish"});
    return;
  }

  if (state_.state() == FinishRunState::FinishSent) {
    display_.showLines({"FINISH SENT", "Run: " + runShort, "Sent: " + String(finishAttempts_) + "/5"});
    return;
  }

  display_.showLines({
    "ENDURO TIMER",
    "FINISH",
    String("LoRa: ") + (radioReady_ ? "OK" : "OFF"),
    "IDLE",
    "HB: " + String(heartbeatCounter_),
  });
}

void FinishStationApp::logHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs_ < 1000UL) return;

  String heartbeatState = state_.stateText();
  heartbeatState.toUpperCase();
  Serial.printf("FINISH alive state=%s uptime=%lu heap=%lu minHeap=%lu\n", heartbeatState.c_str(),
                static_cast<unsigned long>(nowMs), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  lastHeartbeatMs_ = nowMs;
}
