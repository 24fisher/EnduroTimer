#pragma once

#include <Arduino.h>

static constexpr uint32_t FINISH_STATUS_INTERVAL_MS = 5000UL;
static constexpr uint32_t START_STATUS_READY_INTERVAL_MS = 15000UL;
static constexpr uint32_t START_STATUS_ACTIVE_INTERVAL_MS = 7000UL;
static constexpr uint32_t LINK_HEARTBEAT_INTERVAL_MS = FINISH_STATUS_INTERVAL_MS;
static constexpr uint32_t LINK_TIMEOUT_MS = 30000UL;
static constexpr uint32_t LINK_DISCOVERY_INTERVAL_MS = 5000UL;
static constexpr uint32_t LORA_POST_PRIORITY_QUIET_MS = 300UL;

struct LinkStatus {
  bool hasEverReceived = false;
  uint32_t lastPacketMs = 0;
  int lastRssi = 0;
  float lastSnr = 0.0F;
  String lastPacketType = "";
  String lastStationId = "";
  String remoteBootId = "";
  uint32_t packetCount = 0;
  uint32_t remoteRebootCount = 0;
  uint32_t lastBootIdChangeMs = 0;
  bool remoteRebootDetected = false;
};

inline bool isLinkActive(const LinkStatus& link) {
  return link.hasEverReceived && (millis() - link.lastPacketMs <= LINK_TIMEOUT_MS);
}

inline uint32_t linkAgeMs(const LinkStatus& link) {
  if (!link.hasEverReceived) return UINT32_MAX;
  return millis() - link.lastPacketMs;
}

inline String linkSignalText(const LinkStatus& link) {
  if (!isLinkActive(link)) return "NO SIGNAL";
  return String(link.lastRssi) + " dBm";
}

inline bool updateLinkStatus(LinkStatus& link, const String& stationId, const String& packetType, const String& bootId, int packetRssi, float packetSnr) {
  bool rebootDetected = false;
  link.hasEverReceived = true;
  link.lastPacketMs = millis();
  link.lastRssi = packetRssi;
  link.lastSnr = packetSnr;
  link.lastPacketType = packetType;
  link.lastStationId = stationId;
  link.packetCount += 1;
  link.remoteRebootDetected = false;
  if (bootId.length() > 0 && link.remoteBootId.length() > 0 && link.remoteBootId != bootId) {
    rebootDetected = true;
    link.remoteRebootDetected = true;
    link.remoteRebootCount += 1;
    link.lastBootIdChangeMs = millis();
  }
  if (bootId.length() > 0) link.remoteBootId = bootId;
  return rebootDetected;
}
