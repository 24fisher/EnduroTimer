#pragma once

#include <Arduino.h>

class BuzzerStub {
public:
  void begin() { Serial.println("[BuzzerStub] ready (no GPIO used)"); }
  void beep(const char* reason) { Serial.printf("[BuzzerStub] BEEP: %s\n", reason); }
};
