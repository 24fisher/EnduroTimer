#pragma once

#include <Arduino.h>
#include "RadioMessage.h"

class RadioProtocol {
public:
  static String makeMessageId(const char* prefix);
  static String typeToString(RadioMessageType type);
  static RadioMessageType typeFromString(const String& type);
  static bool serialize(const RadioMessage& message, String& output);
  static bool deserialize(const String& input, RadioMessage& output, String* error = nullptr);
};
