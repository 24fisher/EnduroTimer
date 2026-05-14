#pragma once

#include <Arduino.h>

static constexpr uint32_t LINK_TIMEOUT_MS = 15000UL;

struct LinkStatus {
  bool hasEverReceived = false;
  uint32_t lastPacketMs = 0;
  int lastRssi = 0;
  float lastSnr = 0.0F;
  String lastPacketType = "";
  String lastStationId = "";
  uint32_t packetCount = 0;
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

inline void updateLinkStatus(LinkStatus& link, const String& stationId, const String& packetType, int packetRssi, float packetSnr) {
  link.hasEverReceived = true;
  link.lastPacketMs = millis();
  link.lastRssi = packetRssi;
  link.lastSnr = packetSnr;
  link.lastPacketType = packetType;
  link.lastStationId = stationId;
  link.packetCount += 1;
}
