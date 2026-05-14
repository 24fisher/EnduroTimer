#pragma once

#include <Arduino.h>
#include "RadioMessage.h"

static constexpr uint16_t MAX_LORA_PAYLOAD_WARN = 200;
static constexpr uint16_t MAX_LORA_PAYLOAD_HARD = 240;

class RadioProtocol {
public:
  static String makeMessageId(const char* prefix);
  static String typeToString(RadioMessageType type);
  static RadioMessageType typeFromString(const String& type);
  static bool serialize(const RadioMessage& message, String& output);
  static bool serializeCompactStatus(const RadioMessage& message, String& output);
  static bool serializeEmergencyStatus(const RadioMessage& message, String& output);
  static bool deserialize(const String& input, RadioMessage& output, String* error = nullptr);
};
