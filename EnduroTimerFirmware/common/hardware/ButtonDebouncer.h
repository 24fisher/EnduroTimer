#pragma once

#include <Arduino.h>

class ButtonDebouncer {
public:
  void begin(uint8_t pin, uint32_t debounceMs = 50UL) {
    pin_ = pin;
    debounceMs_ = debounceMs;
    pinMode(pin_, INPUT_PULLUP);
    stableState_ = digitalRead(pin_);
    lastReading_ = stableState_;
    lastChangeMs_ = millis();
  }

  bool update(uint32_t nowMs) {
    const int reading = digitalRead(pin_);
    if (reading != lastReading_) {
      lastReading_ = reading;
      lastChangeMs_ = nowMs;
    }

    if (nowMs - lastChangeMs_ < debounceMs_ || reading == stableState_) return false;

    const int previousStableState = stableState_;
    stableState_ = reading;
    return previousStableState == HIGH && stableState_ == LOW;
  }

  bool pressed() const { return stableState_ == LOW; }
  uint8_t pin() const { return pin_; }

private:
  uint8_t pin_ = 0;
  uint32_t debounceMs_ = 50UL;
  int stableState_ = HIGH;
  int lastReading_ = HIGH;
  uint32_t lastChangeMs_ = 0;
};
