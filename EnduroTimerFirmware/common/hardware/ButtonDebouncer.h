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
    lastRawPressedMs_ = 0;
    lastDebouncedPressedMs_ = 0;
    lastPressLatencyMs_ = 0;
    maxPressLatencyMs_ = 0;
    shortPressedEvent_ = false;
    pressedEventArmed_ = !isPressedReading(stableState_);
  }

  void begin(uint8_t pin, uint32_t debounceMs) { begin(pin, true, debounceMs); }

  void update() { update(millis()); }

  bool update(uint32_t nowMs) {
    const int reading = digitalRead(pin_);
    if (reading != lastReading_) {
      if (isPressedReading(reading)) {
        lastRawPressedMs_ = nowMs;
        Serial.printf("BUTTON raw pressed pin=%u at ms=%lu\n", pin_, static_cast<unsigned long>(nowMs));
      } else {
        Serial.printf("BUTTON raw released pin=%u at ms=%lu\n", pin_, static_cast<unsigned long>(nowMs));
      }
      lastReading_ = reading;
      lastChangeMs_ = nowMs;
    }

    if (nowMs - lastChangeMs_ < debounceMs_ || reading == stableState_) return false;

    const int previousStableState = stableState_;
    stableState_ = reading;
    if (isPressedReading(stableState_)) {
      if (pressedEventArmed_ && !isPressedReading(previousStableState)) {
        shortPressedEvent_ = true;
        pressedEventArmed_ = false;
        lastDebouncedPressedMs_ = nowMs;
        lastPressLatencyMs_ = lastRawPressedMs_ > 0 ? nowMs - lastRawPressedMs_ : 0;
        if (lastPressLatencyMs_ > maxPressLatencyMs_) maxPressLatencyMs_ = lastPressLatencyMs_;
        Serial.printf("BUTTON short press pin=%u latencyMs=%lu\n", pin_, static_cast<unsigned long>(lastPressLatencyMs_));
        if (lastPressLatencyMs_ > 200UL) {
          Serial.printf("WARN button debounce latency too high latencyMs=%lu\n", static_cast<unsigned long>(lastPressLatencyMs_));
        }
        return true;
      }
    } else {
      pressedEventArmed_ = true;
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
  uint32_t lastRawPressedMs() const { return lastRawPressedMs_; }
  uint32_t lastDebouncedPressedMs() const { return lastDebouncedPressedMs_; }
  uint32_t lastPressLatencyMs() const { return lastPressLatencyMs_; }
  uint32_t maxPressLatencyMs() const { return maxPressLatencyMs_; }

private:
  bool isPressedReading(int reading) const { return activeLow_ ? reading == LOW : reading == HIGH; }

  uint8_t pin_ = 0;
  bool activeLow_ = true;
  uint32_t debounceMs_ = 50UL;
  int stableState_ = HIGH;
  int lastReading_ = HIGH;
  uint32_t lastChangeMs_ = 0;
  uint32_t lastRawPressedMs_ = 0;
  uint32_t lastDebouncedPressedMs_ = 0;
  uint32_t lastPressLatencyMs_ = 0;
  uint32_t maxPressLatencyMs_ = 0;
  bool shortPressedEvent_ = false;
  bool pressedEventArmed_ = true;
};
