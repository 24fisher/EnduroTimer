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

  if (message.type == RadioMessageType::Status) {
    doc["t"] = "S";
    doc["mid"] = message.messageId;
    doc["sid"] = message.stationId;
    doc["st"] = message.state;
    if (message.uptimeMs > 0) doc["up"] = message.uptimeMs;
    if (message.timestampMs > 0) doc["ts"] = message.timestampMs;
    if (message.heartbeat > 0) doc["hb"] = message.heartbeat;
    if (message.runId.length() > 0) doc["rid"] = message.runId;
    if (message.riderName.length() > 0) doc["rn"] = message.riderName;
    if (message.elapsedMs > 0) doc["el"] = message.elapsedMs;
    if (message.startLinkActive || message.startPacketCount > 0 || message.hasStartRssi || message.hasStartSnr) {
      doc["sl"] = message.startLinkActive;
      doc["sp"] = message.startPacketCount;
      doc["sla"] = message.startLastSeenAgoMs;
      if (message.hasStartRssi) doc["sr"] = message.startRssi;
      if (message.hasStartSnr) doc["ss"] = message.startSnr;
    }
    if (message.hasBatteryVoltage) doc["bv"] = message.batteryVoltage;
    output = "";
    return serializeJson(doc, output) > 0;
  }

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
  const String compactType = doc["t"] | "";
  output.type = compactType == "S" ? RadioMessageType::Status : typeFromString(doc["type"].as<String>());
  output.messageId = doc["messageId"] | "";
  if (output.messageId.length() == 0) output.messageId = doc["mid"] | "";
  output.stationId = doc["stationId"] | "";
  if (output.stationId.length() == 0) output.stationId = doc["sid"] | "";
  output.runId = doc["runId"] | "";
  if (output.runId.length() == 0) output.runId = doc["activeRunId"] | "";
  if (output.runId.length() == 0) output.runId = doc["rid"] | "";
  output.riderName = doc["riderName"] | "";
  if (output.riderName.length() == 0) output.riderName = doc["rn"] | "";
  output.trailName = doc["trailName"] | "";
  if (output.trailName.length() == 0) output.trailName = doc["tn"] | "";
  output.state = doc["state"] | "";
  if (output.state.length() == 0) output.state = doc["st"] | "";
  output.source = doc["source"] | "";
  output.beamClear = doc["beamClear"] | true;
  output.buttonReady = doc["buttonReady"] | false;
  output.timestampMs = doc["timestampMs"] | 0;
  if (output.timestampMs == 0) output.timestampMs = doc["ts"] | 0;
  output.uptimeMs = doc["uptimeMs"] | 0;
  if (output.uptimeMs == 0) output.uptimeMs = doc["up"] | 0;
  output.heartbeat = doc["heartbeat"] | 0;
  if (output.heartbeat == 0) output.heartbeat = doc["hb"] | 0;
  output.startTimestampMs = doc["startTimestampMs"] | 0;
  if (output.startTimestampMs == 0) output.startTimestampMs = doc["sts"] | 0;
  output.finishTimestampMs = doc["finishTimestampMs"] | 0;
  if (output.finishTimestampMs == 0) output.finishTimestampMs = doc["fts"] | 0;
  output.elapsedMs = doc["elapsedMs"] | 0;
  if (output.elapsedMs == 0) output.elapsedMs = doc["el"] | 0;
  if (!doc["startRssi"].isNull()) { output.hasStartRssi = true; output.startRssi = doc["startRssi"].as<int>(); }
  if (!doc["sr"].isNull()) { output.hasStartRssi = true; output.startRssi = doc["sr"].as<int>(); }
  if (!doc["startSnr"].isNull()) { output.hasStartSnr = true; output.startSnr = doc["startSnr"].as<float>(); }
  if (!doc["ss"].isNull()) { output.hasStartSnr = true; output.startSnr = doc["ss"].as<float>(); }
  output.startLastSeenAgoMs = doc["startLastSeenAgoMs"] | 0;
  if (output.startLastSeenAgoMs == 0) output.startLastSeenAgoMs = doc["sla"] | 0;
  output.startLinkActive = doc["startLinkActive"] | false;
  if (!output.startLinkActive) output.startLinkActive = doc["sl"] | false;
  output.startPacketCount = doc["startPacketCount"] | 0;
  if (output.startPacketCount == 0) output.startPacketCount = doc["sp"] | 0;

  if (!doc["batteryVoltage"].isNull()) {
    output.hasBatteryVoltage = true;
    output.batteryVoltage = doc["batteryVoltage"].as<float>();
  }
  if (!doc["bv"].isNull()) {
    output.hasBatteryVoltage = true;
    output.batteryVoltage = doc["bv"].as<float>();
  }

  if (output.messageId.length() == 0) {
    if (error != nullptr) *error = "missing messageId";
    return false;
  }

  return true;
}
