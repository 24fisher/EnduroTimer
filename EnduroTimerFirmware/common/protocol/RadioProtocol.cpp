#include "RadioProtocol.h"

#include <ArduinoJson.h>

String RadioProtocol::makeMessageId(const char* prefix) {
  static uint32_t sequence = 0;
  sequence += 1;
  return String(prefix) + "-" + String(millis(), HEX) + "-" + String(sequence, HEX);
}

String RadioProtocol::typeToString(RadioMessageType type) {
  switch (type) {
    case RadioMessageType::Ping: return "PING";
    case RadioMessageType::Pong: return "PONG";
    case RadioMessageType::RunStart: return "RUN_START";
    case RadioMessageType::Finish: return "FINISH";
    case RadioMessageType::FinishAck: return "FINISH_ACK";
    case RadioMessageType::Status: return "STATUS";
    case RadioMessageType::StartStatus: return "START_STATUS";
    default: return "UNKNOWN";
  }
}

RadioMessageType RadioProtocol::typeFromString(const String& type) {
  if (type == "PING") return RadioMessageType::Ping;
  if (type == "PONG") return RadioMessageType::Pong;
  if (type == "RUN_START") return RadioMessageType::RunStart;
  if (type == "FINISH") return RadioMessageType::Finish;
  if (type == "FINISH_ACK") return RadioMessageType::FinishAck;
  if (type == "STATUS") return RadioMessageType::Status;
  if (type == "START_STATUS") return RadioMessageType::StartStatus;
  return RadioMessageType::Unknown;
}

bool RadioProtocol::serialize(const RadioMessage& message, String& output) {
  JsonDocument doc;
  doc["type"] = typeToString(message.type);
  doc["messageId"] = message.messageId;

  if (message.stationId.length() > 0) doc["stationId"] = message.stationId;
  if (message.runId.length() > 0) doc["runId"] = message.runId;
  if (message.riderName.length() > 0) doc["riderName"] = message.riderName;
  if (message.trailName.length() > 0) doc["trailName"] = message.trailName;
  if (message.state.length() > 0) doc["state"] = message.state;
  if (message.source.length() > 0) doc["source"] = message.source;
  if (message.timestampMs > 0) doc["timestampMs"] = message.timestampMs;
  if (message.uptimeMs > 0) doc["uptimeMs"] = message.uptimeMs;
  if (message.heartbeat > 0) doc["heartbeat"] = message.heartbeat;
  if (message.startTimestampMs > 0) doc["startTimestampMs"] = message.startTimestampMs;
  if (message.finishTimestampMs > 0) doc["finishTimestampMs"] = message.finishTimestampMs;
  if (message.elapsedMs > 0) doc["elapsedMs"] = message.elapsedMs;

  if (message.type == RadioMessageType::Status || message.type == RadioMessageType::StartStatus) {
    doc["activeRunId"] = message.runId;
    doc["riderName"] = message.riderName;
    doc["elapsedMs"] = message.elapsedMs;
    doc["beamClear"] = message.beamClear;
    doc["buttonReady"] = message.buttonReady;
    if (message.hasStartRssi) doc["startRssi"] = message.startRssi; else doc["startRssi"] = nullptr;
    if (message.hasStartSnr) doc["startSnr"] = message.startSnr; else doc["startSnr"] = nullptr;
    doc["startLinkActive"] = message.startLinkActive;
    if (message.hasStartRssi || message.hasStartSnr) doc["startLastSeenAgoMs"] = message.startLastSeenAgoMs; else doc["startLastSeenAgoMs"] = nullptr;
    doc["startPacketCount"] = message.startPacketCount;
    if (message.hasBatteryVoltage) {
      doc["batteryVoltage"] = message.batteryVoltage;
    } else {
      doc["batteryVoltage"] = nullptr;
    }
  }

  output = "";
  return serializeJson(doc, output) > 0;
}

bool RadioProtocol::deserialize(const String& input, RadioMessage& output, String* error) {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, input);
  if (jsonError) {
    if (error != nullptr) *error = jsonError.c_str();
    return false;
  }

  output = RadioMessage{};
  output.type = typeFromString(doc["type"].as<String>());
  output.messageId = doc["messageId"] | "";
  output.stationId = doc["stationId"] | "";
  output.runId = doc["runId"] | "";
  if (output.runId.length() == 0) output.runId = doc["activeRunId"] | "";
  output.riderName = doc["riderName"] | "";
  output.trailName = doc["trailName"] | "";
  output.state = doc["state"] | "";
  output.source = doc["source"] | "";
  output.beamClear = doc["beamClear"] | true;
  output.buttonReady = doc["buttonReady"] | false;
  output.timestampMs = doc["timestampMs"] | 0;
  output.uptimeMs = doc["uptimeMs"] | 0;
  output.heartbeat = doc["heartbeat"] | 0;
  output.startTimestampMs = doc["startTimestampMs"] | 0;
  output.finishTimestampMs = doc["finishTimestampMs"] | 0;
  output.elapsedMs = doc["elapsedMs"] | 0;
  if (!doc["startRssi"].isNull()) { output.hasStartRssi = true; output.startRssi = doc["startRssi"].as<int>(); }
  if (!doc["startSnr"].isNull()) { output.hasStartSnr = true; output.startSnr = doc["startSnr"].as<float>(); }
  output.startLastSeenAgoMs = doc["startLastSeenAgoMs"] | 0;
  output.startLinkActive = doc["startLinkActive"] | false;
  output.startPacketCount = doc["startPacketCount"] | 0;

  if (!doc["batteryVoltage"].isNull()) {
    output.hasBatteryVoltage = true;
    output.batteryVoltage = doc["batteryVoltage"].as<float>();
  }

  if (output.messageId.length() == 0) {
    if (error != nullptr) *error = "missing messageId";
    return false;
  }

  return true;
}
