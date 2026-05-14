#include "FinishStationApp.h"

#include <SPI.h>
#include <esp_system.h>

#include "RadioProtocol.h"
#include "DisplayText.h"

#ifndef LORA_FREQUENCY_MHZ
#define LORA_FREQUENCY_MHZ 868.0
#endif

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint8_t FINISH_MAX_RETRY_ATTEMPTS = 15;
static constexpr uint32_t FINISH_RETRY_INTERVAL_MS = 1000UL;
static constexpr uint32_t StatusIntervalMs = 1000UL;
static constexpr uint32_t DisplayRefreshMs = 200UL;
static constexpr uint32_t StartSignalTimeoutMs = 10000UL;
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
    display_.showBootScreen("FINISH TERMINAL");
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
  if (startLastSeenMs_ > 0 && now - startLastSeenMs_ > StartSignalTimeoutMs) {
    hasStartSignal_ = false;
  }
  updateLed(now);

  uint32_t finishTimestampMs = 0;
  if (sensor_.update(now, finishTimestampMs)) {
    if (state_.canFinish()) {
      state_.markFinishSent(finishTimestampMs);
      finishAttempts_ = 0;
      manualResendCount_ = 0;
      lastFinishSendMs_ = 0;
      finishLineCrossedUntilMs_ = now + 2000UL;
      Serial.printf("Finish accepted runId=%s\n", state_.runId().c_str());
#if ENABLE_OLED
      if (!display_.testPatternOnly()) {
        display_.showFinishLineCrossed();
      }
#endif
      sendFinish();
    } else if (state_.state() == FinishRunState::Idle) {
      Serial.println("FINISH button ignored: no active run");
      showNoRunUntilMs_ = now + 1500UL;
#if ENABLE_OLED
      if (!display_.testPatternOnly()) display_.showLines({"FINISH TERMINAL", "NO ACTIVE RUN"});
#endif
    } else if (state_.state() == FinishRunState::FinishSent || state_.state() == FinishRunState::Error) {
      resendFinishFromButton(now);
    } else {
      Serial.printf("button ignored: state=%s\n", state_.stateText().c_str());
    }
  }


  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ < FINISH_MAX_RETRY_ATTEMPTS &&
      now - lastFinishSendMs_ >= FINISH_RETRY_INTERVAL_MS) {
    sendFinish();
  }

  if (state_.state() == FinishRunState::FinishSent && finishAttempts_ >= FINISH_MAX_RETRY_ATTEMPTS &&
      now - lastFinishSendMs_ >= FINISH_RETRY_INTERVAL_MS) {
    Serial.printf("FINISH_ACK timeout: sent %u/%u.\n", finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS);
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
    Serial.printf("LoRa FAIL code=%d\n", initState);
    Serial.println("[BOOT] LoRa FAIL");
#if ENABLE_OLED
    if (!display_.testPatternOnly()) display_.showLines({"FINISH TERMINAL", "LoRa FAIL", "code=" + String(initState)});
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
    Serial.printf("LORA RX type=%s rssi=%d snr=%.1f raw=%s\n", lastPacket_.c_str(), lastRssi_, static_cast<double>(lastSnr_), lastLoRaRaw_.c_str());
    if (message.type == RadioMessageType::Unknown) {
      Serial.printf("LORA unknown type=%s raw=%s\n", lastPacket_.c_str(), payload.c_str());
    }
    if (message.stationId.length() == 0) {
      Serial.printf("LORA missing stationId raw=%s\n", payload.c_str());
    }
    if (message.stationId == "start") {
      startRssi_ = lastRssi_;
      startSnr_ = lastSnr_;
      startLastSeenMs_ = millis();
      hasStartSignal_ = true;
    }
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
  if (state_.riderName().length() > 0) message.riderName = state_.riderName();
  if (state_.trailName().length() > 0) message.trailName = state_.trailName();
  message.elapsedMs = state_.elapsedMs(clock_.nowMs());
  if (hasStartSignal_ && startLastSeenMs_ > 0 && clock_.nowMs() - startLastSeenMs_ <= StartSignalTimeoutMs) {
    message.hasStartRssi = true;
    message.startRssi = startRssi_;
    message.hasStartSnr = true;
    message.startSnr = startSnr_;
    message.startLastSeenAgoMs = clock_.nowMs() - startLastSeenMs_;
  }
  if (sendRadio(message)) {
    heartbeatCounter_ += 1;
    if (heartbeatCounter_ % 5 == 0) {
      Serial.printf("STATUS sent heartbeat=%lu state=%s\n", static_cast<unsigned long>(heartbeatCounter_), message.state.c_str());
    }
  } else {
    Serial.println("STATUS send failed code=-1");
  }
}

void FinishStationApp::sendFinish() {
  RadioMessage message;
  message.type = RadioMessageType::Finish;
  message.messageId = RadioProtocol::makeMessageId("finish");
  message.stationId = "finish";
  message.runId = state_.runId();
  message.finishTimestampMs = state_.finishTimestampMs();
  message.source = "BUTTON_STUB";
  finishAttempts_ += 1;
  lastFinishSendMs_ = clock_.nowMs();
  Serial.printf("FINISH sent attempt=%u/%u\n", finishAttempts_, FINISH_MAX_RETRY_ATTEMPTS);
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
  const bool resendAfterTimeout = state_.state() == FinishRunState::Error;
  if (resendAfterTimeout) {
    finishAttempts_ = 0;
    state_.markFinishSent(state_.finishTimestampMs());
    Serial.println("FINISH button short press: resend after ACK timeout");
  } else {
    manualResendCount_ += 1;
    Serial.printf("FINISH button short press: manual resend attempt=%u\n", finishAttempts_ + 1);
  }
  finishLineCrossedUntilMs_ = 0;
  sendFinish();
#if ENABLE_OLED
  if (!display_.testPatternOnly()) {
    display_.showLines({"FINISH TERMINAL", resendAfterTimeout ? "RESEND FINISH" : "FINISH RESENT", "Sent: " + String(finishAttempts_) + "/" + String(FINISH_MAX_RETRY_ATTEMPTS)});
  }
#endif
}

void FinishStationApp::handleRadioMessage(const RadioMessage& message) {
  if (message.type == RadioMessageType::Status && message.stationId == "start") {
    Serial.printf("START STATUS received heartbeat=%lu rssi=%d snr=%.1f\n", static_cast<unsigned long>(message.heartbeat), startRssi_, static_cast<double>(startSnr_));
    return;
  }

  if (message.type == RadioMessageType::RunStart) {
    Serial.printf("RUN_START received runId=%s start=%lu\n", message.runId.c_str(),
                  static_cast<unsigned long>(message.startTimestampMs));
    const uint32_t localStartMs = millis();
    state_.startRun(message.runId, message.riderName, message.trailName, message.startTimestampMs, localStartMs);
    Serial.println("Finish state -> Riding");
    Serial.printf("Finish timer started localMs=%lu\n", static_cast<unsigned long>(localStartMs));
    sensor_.arm(message.runId, message.startTimestampMs);
    finishAttempts_ = 0;
    manualResendCount_ = 0;
    buzzer_.beep("RUN_START");
    return;
  }

  if (message.type == RadioMessageType::FinishAck && message.runId == state_.runId()) {
    Serial.printf("[FinishStation] FINISH_ACK received: run=%s\n", message.runId.c_str());
    sensor_.reset();
    state_.ackFinish();
    finishAttempts_ = 0;
    manualResendCount_ = 0;
    showAckOkUntilMs_ = millis() + 1500UL;
#if ENABLE_OLED
    if (!display_.testPatternOnly()) {
      display_.showLines({"FINISH TERMINAL", "ACK OK", "IDLE"});
    }
#endif
  }
}

void FinishStationApp::updateDisplay() {
  const String runShort = state_.runId().length() > 0 ? state_.runId().substring(max(0, static_cast<int>(state_.runId().length()) - 6)) : "-";
  const bool startOnline = hasStartSignal_ && startLastSeenMs_ > 0 && millis() - startLastSeenMs_ <= StartSignalTimeoutMs;
  const String startSignal = startOnline ? String("START:") + String(startRssi_) + "dBm" : String("START:NO SIG");

  if (finishLineCrossedUntilMs_ > millis()) {
    display_.showFinishLineCrossed();
    return;
  }

  if (showAckOkUntilMs_ > millis()) {
    display_.showLines({"FINISH TERMINAL", "ACK OK", "IDLE"});
    return;
  }

  if (showNoRunUntilMs_ > millis()) {
    display_.showLines({"FINISH TERMINAL", "NO ACTIVE RUN", "Press START"});
    return;
  }

  if (state_.state() == FinishRunState::Error) {
    display_.showLines({"FINISH TERMINAL", "ACK TIMEOUT", "PRESS BTN RESEND", "Sent: " + String(finishAttempts_) + "/" + String(FINISH_MAX_RETRY_ATTEMPTS)});
    return;
  }

  if (state_.state() == FinishRunState::WaitFinish) {
    String anim = String("RIDING ");
    for (uint8_t i = 0; i < ridingAnimationFrame(); ++i) anim += " ";
    anim += ">";
    display_.showLines({"FINISH TERMINAL", anim, "Rider: " + toDisplayText(state_.riderName(), 16), "Time: " + formatDurationMs(state_.elapsedMs(millis())).substring(0, 5), startSignal});
    return;
  }

  if (state_.state() == FinishRunState::FinishSent) {
    display_.showLines({"FINISH TERMINAL", "FINISH SENT", "Sent: " + String(finishAttempts_) + "/" + String(FINISH_MAX_RETRY_ATTEMPTS), startSignal});
    return;
  }

  display_.showLines({
    "FINISH TERMINAL",
    String("LoRa: ") + (radioReady_ ? "OK" : "NO SIGNAL"),
    "State: IDLE",
    startSignal,
  });
}

uint8_t FinishStationApp::ridingAnimationFrame() const {
  return static_cast<uint8_t>((millis() / 250UL) % 10UL);
}

void FinishStationApp::logHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs_ < 1000UL) return;

  String heartbeatState = state_.stateText();
  heartbeatState.toUpperCase();
  Serial.printf("FINISH alive state=%s uptime=%lu heap=%lu minHeap=%lu buttonRaw=%d buttonPressed=%s\n", heartbeatState.c_str(),
                static_cast<unsigned long>(nowMs), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), sensor_.buttonRaw(), sensor_.buttonPressed() ? "true" : "false");
  lastHeartbeatMs_ = nowMs;
}
