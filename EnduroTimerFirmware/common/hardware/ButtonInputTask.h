#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "InputEventQueue.h"

class ButtonInputTask {
public:
  bool begin(uint8_t pin, InputEventType eventType, InputEventQueue& queue,
             bool activeLow = true, uint32_t debounceMs = 40UL,
             uint32_t periodMs = 2UL, UBaseType_t priority = 5,
             BaseType_t core = 1);

  int rawGpio() const { return rawGpio_; }
  bool isPressed() const { return pressed_; }
  uint32_t lastDebounceLatencyMs() const { return lastDebounceLatencyMs_; }
  uint32_t maxDebounceLatencyMs() const { return maxDebounceLatencyMs_; }

private:
  static void taskEntry(void* context);
  void run();
  bool isPressedReading(int reading) const;

  uint8_t pin_ = 0;
  InputEventType eventType_ = InputEventType::StartButtonPressed;
  InputEventQueue* queue_ = nullptr;
  bool activeLow_ = true;
  uint32_t debounceMs_ = 40UL;
  TickType_t periodTicks_ = 1;
  TaskHandle_t taskHandle_ = nullptr;
  volatile int rawGpio_ = HIGH;
  volatile bool pressed_ = false;
  volatile uint32_t lastDebounceLatencyMs_ = 0;
  volatile uint32_t maxDebounceLatencyMs_ = 0;
};
