#include "RadioProtocol.h"

#include <ArduinoJson.h>

namespace {
String compactStationId(const String& stationId) {
  if (stationId == "start" || stationId == "s") return "s";
  if (stationId == "finish" || stationId == "f") return "f";
  return stationId;
}

String fullStationId(const String& stationId) {
  if (stationId == "s") return "start";
  if (stationId == "f") return "finish";
  return stationId;
}

String compactState(const String& state) {
  if (state == "Ready" || state == "R") return "R";
  if (state == "Countdown" || state == "C") return "C";
  if (state == "Riding" || state == "G") return "G";
  if (state == "Idle" || state == "I") return "I";
  if (state == "FinishSent" || state == "F") return "F";
  if (state == "AckTimeout" || state == "A") return "A";
  if (state == "Error" || state == "E") return "E";
  return state;
}

String fullState(const String& state) {
  if (state == "R") return "Ready";
  if (state == "C") return "Countdown";
  if (state == "G") return "Riding";
  if (state == "I") return "Idle";
  if (state == "F") return "FinishSent";
  if (state == "A") return "AckTimeout";
  if (state == "E") return "Error";
  return state;
}
}  // namespace

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
    case RadioMessageType::RunStartAck: return "RUN_START_ACK";
    case RadioMessageType::Finish: return "FINISH";
    case RadioMessageType::FinishAck: return "FINISH_ACK";
    case RadioMessageType::Status: return "STATUS";
    case RadioMessageType::Hello: return "HELLO";
    case RadioMessageType::HelloAck: return "HELLO_ACK";
    case RadioMessageType::StartStatus: return "START_STATUS";
    case RadioMessageType::SyncRequest: return "SYNC_REQUEST";
    case RadioMessageType::SyncPing: return "SYNC_PING";
    case RadioMessageType::SyncPong: return "SYNC_PONG";
    case RadioMessageType::SyncApply: return "SYNC_APPLY";
    case RadioMessageType::SyncAck: return "SYNC_ACK";
    default: return "UNKNOWN";
  }
}

RadioMessageType RadioProtocol::typeFromString(const String& type) {
  if (type == "PING") return RadioMessageType::Ping;
  if (type == "PONG") return RadioMessageType::Pong;
  if (type == "RUN_START") return RadioMessageType::RunStart;
  if (type == "RUN_START_ACK") return RadioMessageType::RunStartAck;
  if (type == "FINISH") return RadioMessageType::Finish;
  if (type == "FINISH_ACK") return RadioMessageType::FinishAck;
  if (type == "STATUS") return RadioMessageType::Status;
  if (type == "HELLO") return RadioMessageType::Hello;
  if (type == "HELLO_ACK") return RadioMessageType::HelloAck;
  if (type == "START_STATUS") return RadioMessageType::StartStatus;
  if (type == "SYNC_REQUEST") return RadioMessageType::SyncRequest;
  if (type == "SYNC_PING") return RadioMessageType::SyncPing;
  if (type == "SYNC_PONG") return RadioMessageType::SyncPong;
  if (type == "SYNC_APPLY") return RadioMessageType::SyncApply;
  if (type == "SYNC_ACK") return RadioMessageType::SyncAck;
  return RadioMessageType::Unknown;
}

bool RadioProtocol::serializeCompactStatus(const RadioMessage& message, String& output) {
  JsonDocument doc;
  doc["t"] = "S";
  doc["sid"] = compactStationId(message.stationId);
  if (message.version.length() > 0) doc["ver"] = message.version;
  if (message.bootId.length() > 0) doc["bid"] = message.bootId;
  if (message.state.length() > 0) doc["st"] = compactState(message.state);
  if (message.raceClockSynced) doc["rcs"] = true;
  if (message.raceClockNowMs > 0) doc["rcn"] = message.raceClockNowMs;
  if (message.syncAccuracyMs > 0) doc["sacc"] = message.syncAccuracyMs;
  if (message.offsetToMasterMs != 0) doc["off"] = message.offsetToMasterMs;
  if (message.hasBatteryVoltage) { doc["bv"] = message.batteryVoltage; doc["bp"] = message.batteryPercent; }
  if (message.heartbeat > 0) doc["hb"] = message.heartbeat;
  if (message.uptimeMs > 0) doc["up"] = message.uptimeMs / 1000UL;
  if (message.hasStartRssi) doc["sr"] = message.startRssi;
  if (message.hasStartSnr) doc["ss"] = static_cast<int>(message.startSnr);
  if (message.startLastSeenAgoMs > 0) doc["sa"] = message.startLastSeenAgoMs / 1000UL;
  if (message.hasFinishRssi) doc["fr"] = message.finishRssi;
  if (message.hasFinishSnr) doc["fs"] = static_cast<int>(message.finishSnr);

  output = "";
  return serializeJson(doc, output) > 0;
}

bool RadioProtocol::serializeEmergencyStatus(const RadioMessage& message, String& output) {
  JsonDocument doc;
  doc["t"] = "S";
  doc["sid"] = compactStationId(message.stationId);
  if (message.version.length() > 0) doc["ver"] = message.version;
  if (message.bootId.length() > 0) doc["bid"] = message.bootId;
  if (message.heartbeat > 0) doc["hb"] = message.heartbeat;

  output = "";
  return serializeJson(doc, output) > 0;
}

bool RadioProtocol::serialize(const RadioMessage& message, String& output) {
  if (message.type == RadioMessageType::Status) {
    return serializeCompactStatus(message, output);
  }

  JsonDocument doc;
  doc["type"] = typeToString(message.type);
  doc["messageId"] = message.messageId;

  if (message.stationId.length() > 0) doc["stationId"] = message.stationId;
  if (message.runId.length() > 0) doc["runId"] = message.runId;
  if (message.riderName.length() > 0) doc["riderName"] = message.riderName;
  if (message.trailName.length() > 0) doc["trailName"] = message.trailName;
  if (message.state.length() > 0) doc["state"] = message.state;
  if (message.version.length() > 0) doc["version"] = message.version;
  if (message.bootId.length() > 0) doc["bootId"] = message.bootId;
  if (message.role.length() > 0) doc["role"] = message.role;
  if (message.source.length() > 0) doc["source"] = message.source;
  if (message.timestampMs > 0) doc["timestampMs"] = message.timestampMs;
  if (message.uptimeMs > 0) doc["uptimeMs"] = message.uptimeMs;
  if (message.heartbeat > 0) doc["heartbeat"] = message.heartbeat;
  if (message.startTimestampMs > 0) doc["startTimestampMs"] = message.startTimestampMs;
  if (message.raceStartTimeMs > 0) doc["raceStartTimeMs"] = message.raceStartTimeMs;
  if (message.finishRaceTimeMs > 0) doc["finishRaceTimeMs"] = message.finishRaceTimeMs;
  if (message.timingSource.length() > 0) doc["timingSource"] = message.timingSource;
  if (message.syncId.length() > 0) doc["syncId"] = message.syncId;
  if (message.t1StartRaceMs > 0) doc["t1StartRaceMs"] = message.t1StartRaceMs;
  if (message.t2FinishLocalMs > 0) doc["t2FinishLocalMs"] = message.t2FinishLocalMs;
  if (message.t3FinishLocalMs > 0) doc["t3FinishLocalMs"] = message.t3FinishLocalMs;
  if (message.offsetToMasterMs != 0) doc["offsetToMasterMs"] = message.offsetToMasterMs;
  if (message.roundTripMs > 0) doc["roundTripMs"] = message.roundTripMs;
  if (message.networkDelayMs > 0) doc["networkDelayMs"] = message.networkDelayMs;
  if (message.syncAccuracyMs > 0) doc["syncAccuracyMs"] = message.syncAccuracyMs;
  if (message.raceClockSynced) doc["raceClockSynced"] = true;
  if (message.raceClockNowMs > 0) doc["raceClockNowMs"] = message.raceClockNowMs;
  if (message.finishTimestampMs > 0) doc["finishTimestampMs"] = message.finishTimestampMs;
  if (message.elapsedMs > 0) doc["elapsedMs"] = message.elapsedMs;
  if (message.resultMs > 0) doc["resultMs"] = message.resultMs;
  if (message.resultFormatted.length() > 0) doc["resultFormatted"] = message.resultFormatted;
  if (message.hasBatteryVoltage) { doc["batteryVoltage"] = message.batteryVoltage; doc["batteryPercent"] = message.batteryPercent; }

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
  const bool compactStatus = compactType == "S";
  output.type = compactStatus ? RadioMessageType::Status : typeFromString(doc["type"].as<String>());
  output.messageId = doc["messageId"] | "";
  if (output.messageId.length() == 0) output.messageId = doc["mid"] | "";
  output.stationId = doc["stationId"] | "";
  if (output.stationId.length() == 0) output.stationId = fullStationId(doc["sid"] | "");
  output.runId = doc["runId"] | "";
  if (output.runId.length() == 0) output.runId = doc["activeRunId"] | "";
  if (output.runId.length() == 0) output.runId = doc["rid"] | "";
  output.riderName = doc["riderName"] | "";
  if (output.riderName.length() == 0) output.riderName = doc["rn"] | "";
  output.trailName = doc["trailName"] | "";
  if (output.trailName.length() == 0) output.trailName = doc["tn"] | "";
  output.state = doc["state"] | "";
  if (output.state.length() == 0) output.state = fullState(doc["st"] | "");
  output.source = doc["source"] | "";
  output.version = doc["version"] | "";
  if (output.version.length() == 0) output.version = doc["ver"] | "";
  output.bootId = doc["bootId"] | "";
  if (output.bootId.length() == 0) output.bootId = doc["bid"] | "";
  output.role = doc["role"] | "";
  output.beamClear = doc["beamClear"] | true;
  output.buttonReady = doc["buttonReady"] | false;
  output.timestampMs = doc["timestampMs"] | 0;
  if (output.timestampMs == 0) output.timestampMs = doc["ts"] | 0;
  output.uptimeMs = doc["uptimeMs"] | 0;
  if (output.uptimeMs == 0 && !doc["up"].isNull()) {
    const uint32_t compactUptimeSeconds = doc["up"].as<uint32_t>();
    output.uptimeMs = compactStatus ? compactUptimeSeconds * 1000UL : compactUptimeSeconds;
  }
  output.heartbeat = doc["heartbeat"] | 0;
  if (output.heartbeat == 0) output.heartbeat = doc["hb"] | 0;
  output.startTimestampMs = doc["startTimestampMs"] | 0;
  output.raceStartTimeMs = doc["raceStartTimeMs"] | 0;
  output.finishRaceTimeMs = doc["finishRaceTimeMs"] | 0;
  output.timingSource = doc["timingSource"] | "";
  output.syncId = doc["syncId"] | "";
  output.t1StartRaceMs = doc["t1StartRaceMs"] | 0;
  output.t2FinishLocalMs = doc["t2FinishLocalMs"] | 0;
  output.t3FinishLocalMs = doc["t3FinishLocalMs"] | 0;
  output.offsetToMasterMs = doc["offsetToMasterMs"] | 0;
  if (output.offsetToMasterMs == 0) output.offsetToMasterMs = doc["off"] | 0;
  output.roundTripMs = doc["roundTripMs"] | 0;
  output.networkDelayMs = doc["networkDelayMs"] | 0;
  output.syncAccuracyMs = doc["syncAccuracyMs"] | 0;
  if (output.syncAccuracyMs == 0) output.syncAccuracyMs = doc["sacc"] | 0;
  output.raceClockSynced = doc["raceClockSynced"] | false;
  if (!output.raceClockSynced) output.raceClockSynced = doc["rcs"] | false;
  output.raceClockNowMs = doc["raceClockNowMs"] | 0;
  if (output.raceClockNowMs == 0) output.raceClockNowMs = doc["rcn"] | 0;
  output.localRunStartReceivedMillis = doc["localRunStartReceivedMillis"] | 0;
  output.finishLocalElapsedMs = doc["finishLocalElapsedMs"] | 0;
  output.remoteStartTimestampMs = doc["remoteStartTimestampMs"] | 0;
  if (output.startTimestampMs == 0) output.startTimestampMs = doc["sts"] | 0;
  output.finishTimestampMs = doc["finishTimestampMs"] | 0;
  if (output.finishTimestampMs == 0) output.finishTimestampMs = doc["fts"] | 0;
  output.elapsedMs = doc["elapsedMs"] | 0;
  if (output.elapsedMs == 0) output.elapsedMs = doc["el"] | 0;
  output.resultMs = doc["resultMs"] | 0;
  if (output.resultMs == 0) output.resultMs = doc["res"] | 0;
  output.resultFormatted = doc["resultFormatted"] | "";
  if (!doc["startRssi"].isNull()) { output.hasStartRssi = true; output.startRssi = doc["startRssi"].as<int>(); }
  if (!doc["sr"].isNull()) { output.hasStartRssi = true; output.startRssi = doc["sr"].as<int>(); }
  if (!doc["startSnr"].isNull()) { output.hasStartSnr = true; output.startSnr = doc["startSnr"].as<float>(); }
  if (!doc["ss"].isNull()) { output.hasStartSnr = true; output.startSnr = doc["ss"].as<float>(); }
  output.startLastSeenAgoMs = doc["startLastSeenAgoMs"] | 0;
  if (output.startLastSeenAgoMs == 0) output.startLastSeenAgoMs = (doc["sla"] | 0);
  if (output.startLastSeenAgoMs == 0 && !doc["sa"].isNull()) output.startLastSeenAgoMs = doc["sa"].as<uint32_t>() * 1000UL;
  output.startLinkActive = doc["startLinkActive"] | false;
  if (!output.startLinkActive) output.startLinkActive = doc["sl"] | false;
  if (!output.startLinkActive && compactStatus && (output.hasStartRssi || output.hasStartSnr)) output.startLinkActive = true;
  output.startPacketCount = doc["startPacketCount"] | 0;
  if (output.startPacketCount == 0) output.startPacketCount = doc["sp"] | 0;
  if (!doc["finishRssi"].isNull()) { output.hasFinishRssi = true; output.finishRssi = doc["finishRssi"].as<int>(); }
  if (!doc["fr"].isNull()) { output.hasFinishRssi = true; output.finishRssi = doc["fr"].as<int>(); }
  if (!doc["finishSnr"].isNull()) { output.hasFinishSnr = true; output.finishSnr = doc["finishSnr"].as<float>(); }
  if (!doc["fs"].isNull()) { output.hasFinishSnr = true; output.finishSnr = doc["fs"].as<float>(); }

  if (!doc["batteryVoltage"].isNull()) {
    output.hasBatteryVoltage = true;
    output.batteryVoltage = doc["batteryVoltage"].as<float>();
    output.batteryPercent = doc["batteryPercent"] | -1;
  }
  if (!doc["bv"].isNull()) {
    output.hasBatteryVoltage = true;
    output.batteryVoltage = doc["bv"].as<float>();
    output.batteryPercent = doc["bp"] | output.batteryPercent;
  }

  if (output.messageId.length() == 0 && output.type != RadioMessageType::Status) {
    if (error != nullptr) *error = "missing messageId";
    return false;
  }

  return true;
}
