#pragma once

#include <Arduino.h>

class ButtonDebouncer {
public:
  void begin(uint8_t pin, bool activeLow = true, uint32_t debounceMs = 50UL) {
    pin_ = pin;
    activeLow_ = activeLow;
    debounceMs_ = debounceMs;
    pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT);
    stableState_ = digitalRead(pin_);
    lastReading_ = stableState_;
    lastChangeMs_ = millis();
    shortPressedEvent_ = false;
  }

  void begin(uint8_t pin, uint32_t debounceMs) { begin(pin, true, debounceMs); }

  void update() { update(millis()); }

  bool update(uint32_t nowMs) {
    const int reading = digitalRead(pin_);
    if (reading != lastReading_) {
      Serial.printf("button raw %s transition pin=%u raw=%d at ms=%lu\n", isPressedReading(reading) ? "pressed" : "released", pin_, reading, static_cast<unsigned long>(nowMs));
      lastReading_ = reading;
      lastChangeMs_ = nowMs;
    }

    if (nowMs - lastChangeMs_ < debounceMs_ || reading == stableState_) return false;

    const int previousStableState = stableState_;
    stableState_ = reading;
    if (!isPressedReading(previousStableState) && isPressedReading(stableState_)) {
      shortPressedEvent_ = true;
      Serial.printf("debounced short press pin=%u at ms=%lu\n", pin_, static_cast<unsigned long>(nowMs));
      return true;
    }
    return false;
  }

  bool wasShortPressed() {
    const bool event = shortPressedEvent_;
    shortPressedEvent_ = false;
    return event;
  }

  bool isPressed() const { return isPressedReading(stableState_); }
  bool pressed() const { return isPressed(); }
  int raw() const { return lastReading_; }
  int rawState() const { return raw(); }
  bool rawPressed() const { return isPressedReading(lastReading_); }
  uint8_t pin() const { return pin_; }

private:
  bool isPressedReading(int reading) const { return activeLow_ ? reading == LOW : reading == HIGH; }

  uint8_t pin_ = 0;
  bool activeLow_ = true;
  uint32_t debounceMs_ = 50UL;
  int stableState_ = HIGH;
  int lastReading_ = HIGH;
  uint32_t lastChangeMs_ = 0;
  bool shortPressedEvent_ = false;
};
