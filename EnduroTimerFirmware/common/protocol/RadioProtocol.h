#pragma once

#include <Arduino.h>
#include "RadioMessage.h"

static constexpr uint16_t MAX_LORA_PAYLOAD_WARN = 180;
static constexpr uint16_t MAX_LORA_PAYLOAD_HARD = 240;

class RadioProtocol {
public:
  static String makeMessageId(const char* prefix);
  static String typeToString(RadioMessageType type);
  static RadioMessageType typeFromString(const String& type);
  static String normalizeStationId(const String& value);
  static String compactStationCode(const String& value);
  static bool isForStation(const RadioMessage& message, const String& localStation);
  static bool isFromFinish(const RadioMessage& message);
  static bool isFromStart(const RadioMessage& message);
  static bool isFromRepeater(const RadioMessage& message);
  static bool serialize(const RadioMessage& message, String& output);
  static bool serializeCompactStatus(const RadioMessage& message, String& output);
  static bool serializeEmergencyStatus(const RadioMessage& message, String& output);
  static bool deserialize(const String& input, RadioMessage& output, String* error = nullptr);
};
