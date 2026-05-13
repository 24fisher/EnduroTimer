#pragma once

#include <Arduino.h>

enum class RadioMessageType {
  Unknown,
  Ping,
  Pong,
  RunStart,
  Finish,
  FinishAck,
  Status
};

struct RadioMessage {
  RadioMessageType type = RadioMessageType::Unknown;
  String messageId;
  String stationId;
  String runId;
  String riderName;
  String state;
  String source;
  bool beamClear = true;
  bool hasBatteryVoltage = false;
  float batteryVoltage = 0.0F;
  uint32_t timestampMs = 0;
  uint32_t startTimestampMs = 0;
  uint32_t finishTimestampMs = 0;
};
