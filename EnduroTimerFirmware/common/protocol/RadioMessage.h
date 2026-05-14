#pragma once

#include <Arduino.h>

enum class RadioMessageType {
  Unknown,
  Ping,
  Pong,
  RunStart,
  RunStartAck,
  Finish,
  FinishAck,
  Status,
  Hello,
  HelloAck,
  StartStatus,
  SyncRequest,
  SyncPing,
  SyncPong,
  SyncApply,
  SyncAck
};

struct RadioMessage {
  RadioMessageType type = RadioMessageType::Unknown;
  String messageId;
  String stationId;
  String runId;
  uint32_t runNumber = 0;
  String riderName;
  String trailName;
  String state;
  String version;
  String bootId;
  String role;
  String source;
  bool beamClear = true;
  bool buttonReady = false;
  bool hasBatteryVoltage = false;
  float batteryVoltage = 0.0F;
  int batteryPercent = -1;
  uint32_t timestampMs = 0;
  uint32_t uptimeMs = 0;
  uint32_t heartbeat = 0;
  uint32_t startTimestampMs = 0;
  uint32_t raceStartTimeMs = 0;
  uint32_t finishRaceTimeMs = 0;
  String timingSource;
  String syncId;
  uint32_t t1StartRaceMs = 0;
  uint32_t t2FinishLocalMs = 0;
  uint32_t t3FinishLocalMs = 0;
  int32_t offsetToMasterMs = 0;
  uint32_t roundTripMs = 0;
  uint32_t networkDelayMs = 0;
  uint32_t syncAccuracyMs = 0;
  bool raceClockSynced = false;
  uint32_t raceClockNowMs = 0;
  uint32_t localRunStartReceivedMillis = 0;
  uint32_t finishLocalElapsedMs = 0;
  uint32_t remoteStartTimestampMs = 0;
  uint32_t finishTimestampMs = 0;
  uint32_t elapsedMs = 0;
  uint32_t resultMs = 0;
  String resultFormatted;
  bool hasStartRssi = false;
  int startRssi = 0;
  bool hasStartSnr = false;
  float startSnr = 0.0F;
  uint32_t startLastSeenAgoMs = 0;
  bool startLinkActive = false;
  uint32_t startPacketCount = 0;
  bool hasFinishRssi = false;
  int finishRssi = 0;
  bool hasFinishSnr = false;
  float finishSnr = 0.0F;
};
