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
      Serial.printf("button raw pressed transition pin=%u raw=%d pressed=%s at ms=%lu\n", pin_, reading, reading == LOW ? "true" : "false", static_cast<unsigned long>(nowMs));
      lastReading_ = reading;
      lastChangeMs_ = nowMs;
    }

    if (nowMs - lastChangeMs_ < debounceMs_ || reading == stableState_) return false;

    const int previousStableState = stableState_;
    stableState_ = reading;
    if (previousStableState == HIGH && stableState_ == LOW) {
      Serial.printf("debounced short press pin=%u at ms=%lu\n", pin_, static_cast<unsigned long>(nowMs));
      return true;
    }
    return false;
  }

  bool pressed() const { return stableState_ == LOW; }
  int rawState() const { return lastReading_; }
  bool rawPressed() const { return lastReading_ == LOW; }
  uint8_t pin() const { return pin_; }

private:
  uint8_t pin_ = 0;
  uint32_t debounceMs_ = 50UL;
  int stableState_ = HIGH;
  int lastReading_ = HIGH;
  uint32_t lastChangeMs_ = 0;
};
