#pragma once

#include <Arduino.h>

enum class InputEventType {
  StartButtonPressed,
  FinishButtonPressed
};

struct InputEvent {
  InputEventType type;
  uint32_t localMillis;
  uint32_t rawGpio;
};
