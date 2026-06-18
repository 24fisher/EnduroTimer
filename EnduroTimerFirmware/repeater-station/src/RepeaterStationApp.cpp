#include "RepeaterStationApp.h"

#include <ArduinoJson.h>
#include <SPI.h>

#include "BuildConfig.h"
#include "RadioProtocol.h"

static constexpr int LORA_NSS = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_RST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr uint32_t REPEATER_SEEN_TTL_MS = 60000UL;
static constexpr uint32_t DISPLAY_REFRESH_MS = 500UL;
static constexpr uint32_t STATUS_RELAY_MIN_INTERVAL_MS = 2000UL;

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
namespace {
volatile bool radioPacketReceived = false;
uint32_t lastStatusRelayStartMs = 0;
uint32_t lastStatusRelayFinishMs = 0;
void IRAM_ATTR onRadioPacketReceived() { radioPacketReceived = true; }
}

void RepeaterStationApp::begin() {
  Serial.println("Firmware: EnduroTimer RepeaterStation");
  Serial.println("Version: v" FIRMWARE_VERSION);
  Serial.println("LoRa RX mode: non-blocking");
  Serial.printf("LoRa config freq=%.1f bw=%.1f sf=%d cr=%d txPower=%d\n", static_cast<double>(LORA_FREQUENCY_MHZ), static_cast<double>(LORA_BANDWIDTH_KHZ), LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_TX_POWER_DBM);
#if ENABLE_OLED
  oledReady_ = display_.begin();
  Serial.println(oledReady_ ? "OLED OK" : "OLED FAIL");
  if (oledReady_) display_.showBootScreen(String("REPEATER v") + FIRMWARE_VERSION);
#endif
  battery_.begin();
  beginRadio();
}

void RepeaterStationApp::beginRadio() {
#if ENABLE_LORA
  Serial.println("[BOOT] LoRa init...");
  SPI.begin(9, 11, 10, LORA_NSS);
  const int state = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady_ = state == RADIOLIB_ERR_NONE;
  if (!radioReady_) { Serial.printf("LoRa FAIL code=%d\n", state); return; }
  radio.setBandwidth(LORA_BANDWIDTH_KHZ);
  radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
  radio.setCodingRate(LORA_CODING_RATE);
  radio.setOutputPower(LORA_TX_POWER_DBM);
  radio.setPacketReceivedAction(onRadioPacketReceived);
  radioPacketReceived = false;
  const int rx = radio.startReceive();
  radioReady_ = rx == RADIOLIB_ERR_NONE;
  Serial.println(radioReady_ ? "LoRa OK" : "LoRa startReceive FAIL");
#endif
}

void RepeaterStationApp::loop() {
  const uint32_t now = millis();
  loopMonitor_.tick(now);
  battery_.update(now);
  const uint32_t pollStart = millis();
  pollRadioNonBlocking();
  loopMonitor_.recordBlock("RadioPoll", millis() - pollStart, LORA_MAX_POLL_DURATION_WARN_MS);
  processRelayQueue();
#if ENABLE_OLED
  display_.update();
  if (oledReady_ && now - lastDisplayMs_ >= DISPLAY_REFRESH_MS) { updateDisplay(); lastDisplayMs_ = now; }
#endif
  logCounters(now);
}

void RepeaterStationApp::pollRadioNonBlocking() {
#if ENABLE_LORA
  if (!radioReady_) return;
  const uint32_t start = millis();
  if (!radioPacketReceived) { radioPollLastDurationMs_ = millis() - start; return; }
  radioPacketReceived = false;
  String payload;
  const int state = radio.readData(payload);
  restoreRadioReceiveMode();
  radioPollLastDurationMs_ = millis() - start;
  if (radioPollLastDurationMs_ > radioPollMaxDurationMs_) radioPollMaxDurationMs_ = radioPollLastDurationMs_;
  if (state == RADIOLIB_ERR_NONE) handlePacket(payload, static_cast<int>(radio.getRSSI()), radio.getSNR());
  else if (state != RADIOLIB_ERR_RX_TIMEOUT) Serial.printf("LORA readData failed code=%d\n", state);
#endif
}

void RepeaterStationApp::restoreRadioReceiveMode() {
#if ENABLE_LORA
  if (!radioReady_) return;
  radioPacketReceived = false;
  const int rx = radio.startReceive();
  if (rx != RADIOLIB_ERR_NONE) Serial.printf("LoRa startReceive restore failed code=%d\n", rx);
#endif
}

bool RepeaterStationApp::relayable(RadioMessageType type) const {
  if (type == RadioMessageType::RunStart || type == RadioMessageType::RunStartAck ||
      type == RadioMessageType::Finish || type == RadioMessageType::FinishAck) {
    return true;
  }
#if REPEATER_RELAY_STATUS_CFG
  if (type == RadioMessageType::Status) return true;
#endif
#if REPEATER_RELAY_HELLO_CFG
  if (type == RadioMessageType::Hello || type == RadioMessageType::HelloAck) return true;
#endif
  return false;
}

uint8_t RepeaterStationApp::priorityFor(RadioMessageType type) const {
  return (type == RadioMessageType::RunStart || type == RadioMessageType::RunStartAck || type == RadioMessageType::Finish || type == RadioMessageType::FinishAck) ? 1 : 2;
}

bool RepeaterStationApp::seenRecently(const String& mid, uint32_t nowMs) {
  for (const auto& item : seen_) if (item.mid == mid && nowMs - item.seenMs <= REPEATER_SEEN_TTL_MS) return true;
  return false;
}

void RepeaterStationApp::remember(const String& mid, uint32_t nowMs) {
  SeenMessage seen;
  seen.mid = mid;
  seen.seenMs = nowMs;
  seen_[seenCursor_] = seen;
  seenCursor_ = (seenCursor_ + 1) % seen_.size();
}

void RepeaterStationApp::handlePacket(const String& payload, int rssi, float snr) {
  (void)snr;
  rxCount_ += 1;
  RadioMessage msg;
  String error;
  if (!RadioProtocol::deserialize(payload, msg, &error)) { Serial.printf("RX parse failed error=%s\n", error.c_str()); return; }
  const String type = RadioProtocol::typeToString(msg.type);
  Serial.printf("REPEATER RX type=%s src=%s dst=%s hop=%u rssi=%d\n",
                type.c_str(), msg.src.c_str(), msg.dst.c_str(), msg.hop, rssi);
  if (msg.type == RadioMessageType::RunStart) lastCriticalType_ = "RS";
  else if (msg.type == RadioMessageType::RunStartAck) lastCriticalType_ = "RSA";
  else if (msg.type == RadioMessageType::Finish) lastCriticalType_ = "F";
  else if (msg.type == RadioMessageType::FinishAck) lastCriticalType_ = "FA";
  if (msg.src == "s") { startSeen_ = true; startRssi_ = rssi; }
  if (msg.src == "f") { finishSeen_ = true; finishRssi_ = rssi; }
  if (msg.messageId.length() == 0) { Serial.println("RX warning: missing mid, not relayed"); return; }
  const uint32_t now = millis();
  if (seenRecently(msg.messageId, now)) {
    duplicateCount_ += 1;
    Serial.printf("REPEATER DROP duplicate mid=%s\n", msg.messageId.c_str());
    return;
  }
  remember(msg.messageId, now);
  if (msg.hop >= msg.maxHops) {
    hopLimitDrops_ += 1;
    Serial.printf("REPEATER DROP hop limit mid=%s\n", msg.messageId.c_str());
    return;
  }
  if (msg.src == "r" || msg.dst == "r" || !relayable(msg.type)) return;
  if (msg.type == RadioMessageType::Status) {
    uint32_t& last = msg.src == "s" ? lastStatusRelayStartMs : lastStatusRelayFinishMs;
    if (now - last < STATUS_RELAY_MIN_INTERVAL_MS) return;
    last = now;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;
  doc["hop"] = msg.hop + 1;
  doc["via"] = "r";
  String out;
  serializeJson(doc, out);
  enqueueRelay(out, msg, priorityFor(msg.type), msg.hop + 1);
}

bool RepeaterStationApp::enqueueRelay(const String& payload, const RadioMessage& message, uint8_t priority, uint8_t hop) {
  int freeSlot = -1;
  int lowSlot = -1;
  for (size_t i = 0; i < queue_.size(); ++i) {
    if (queue_[i].payload.length() == 0) { freeSlot = i; break; }
    if (queue_[i].priority > priority) lowSlot = i;
  }
  const int slot = freeSlot >= 0 ? freeSlot : lowSlot;
  if (slot < 0) { queueDrops_ += 1; Serial.printf("relay queue drop mid=%s\n", message.messageId.c_str()); return false; }
  RelayPacket packet;
  packet.payload = payload;
  packet.mid = message.messageId;
  packet.type = RadioProtocol::typeToString(message.type);
  packet.src = message.src;
  packet.dst = message.dst;
  packet.priority = priority;
  packet.hop = hop;
  packet.enqueueMs = millis();
  queue_[slot] = packet;
  Serial.printf("queued relay mid=%s\n", message.messageId.c_str());
  return true;
}

void RepeaterStationApp::processRelayQueue() {
#if ENABLE_LORA
  if (!radioReady_) return;
  bool criticalPending = false;
  for (const auto& packet : queue_) {
    if (packet.payload.length() > 0 && packet.priority == 1) {
      criticalPending = true;
      break;
    }
  }
  int best = -1;
  for (size_t i = 0; i < queue_.size(); ++i) {
    if (queue_[i].payload.length() == 0) continue;
    if (criticalPending && queue_[i].priority != 1) continue;
    if (queue_[i].priority != 1 && lastCriticalRelayMs_ > 0 &&
        millis() - lastCriticalRelayMs_ < LORA_POST_PRIORITY_QUIET_MS) continue;
    if (best < 0 || queue_[i].priority < queue_[best].priority) best = i;
  }
  if (best < 0) return;
  RelayPacket pkt = queue_[best];
  queue_[best] = RelayPacket{};
  const int tx = radio.transmit(pkt.payload);
  restoreRadioReceiveMode();
  if (tx == RADIOLIB_ERR_NONE) {
    txCount_ += 1;
    if (pkt.priority == 1) lastCriticalRelayMs_ = millis();
    Serial.printf("REPEATER TX type=%s src=%s dst=%s hop=%u\n",
                  pkt.type.c_str(), pkt.src.c_str(), pkt.dst.c_str(), pkt.hop);
  }
  else { queueDrops_ += 1; Serial.printf("relay TX failed mid=%s code=%d\n", pkt.mid.c_str(), tx); }
#endif
}

String RepeaterStationApp::rssiText(const char* label, bool seen, int rssi) const {
  return String(label) + (seen ? String(rssi) : String("--"));
}

void RepeaterStationApp::updateDisplay() {
#if ENABLE_OLED
  const uint32_t dropCount = duplicateCount_ + hopLimitDrops_ + queueDrops_;
  String batteryLine = "BAT: --%";
  if (battery_.isValid()) {
    const uint32_t mv = battery_.voltageMv();
    char voltageText[8];
    snprintf(voltageText, sizeof(voltageText), "%lu.%02luV",
             static_cast<unsigned long>(mv / 1000UL),
             static_cast<unsigned long>((mv % 1000UL) / 10UL));
    batteryLine = String("BAT: ") + String(battery_.percent()) + "% " + voltageText;
  }
  display_.showLines({String("REPEATER v") + FIRMWARE_VERSION,
                      "MODE: RELAY",
                      rssiText("S:", startSeen_, startRssi_) + " " + rssiText("F:", finishSeen_, finishRssi_),
                      String("RX:") + rxCount_ + " TX:" + txCount_ + " D:" + dropCount,
                      String("LAST: ") + lastCriticalType_,
                      batteryLine});
#endif
}

void RepeaterStationApp::logCounters(uint32_t nowMs) {
  if (nowMs - lastCountersMs_ < 5000UL) return;
  lastCountersMs_ = nowMs;
  Serial.printf("RX count=%lu TX count=%lu DUP count=%lu queue drops=%lu hop drops=%lu start RSSI=%s finish RSSI=%s loop gap max=%lu radio poll max=%lu\n",
                static_cast<unsigned long>(rxCount_), static_cast<unsigned long>(txCount_), static_cast<unsigned long>(duplicateCount_), static_cast<unsigned long>(queueDrops_), static_cast<unsigned long>(hopLimitDrops_),
                startSeen_ ? String(startRssi_).c_str() : "NO SIGNAL", finishSeen_ ? String(finishRssi_).c_str() : "NO SIGNAL",
                static_cast<unsigned long>(loopMonitor_.maxLoopGapMs()), static_cast<unsigned long>(radioPollMaxDurationMs_));
}

