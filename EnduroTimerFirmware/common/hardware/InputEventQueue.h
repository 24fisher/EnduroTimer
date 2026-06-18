#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "InputEvent.h"

class InputEventQueue {
public:
  bool begin(uint8_t capacity = 16);
  bool push(const InputEvent& event);
  bool pop(InputEvent& event);
  uint32_t depth() const;
  uint32_t droppedInputEvents() const { return droppedInputEvents_; }

private:
  QueueHandle_t queue_ = nullptr;
  volatile uint32_t droppedInputEvents_ = 0;
};
