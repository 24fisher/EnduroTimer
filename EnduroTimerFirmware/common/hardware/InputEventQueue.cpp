#include "InputEventQueue.h"

bool InputEventQueue::begin(uint8_t capacity) {
  if (queue_ != nullptr) return true;
  queue_ = xQueueCreate(capacity, sizeof(InputEvent));
  droppedInputEvents_ = 0;
  return queue_ != nullptr;
}

bool InputEventQueue::push(const InputEvent& event) {
  if (queue_ != nullptr && xQueueSend(queue_, &event, 0) == pdTRUE) return true;
  droppedInputEvents_ += 1;
  return false;
}

bool InputEventQueue::pop(InputEvent& event) {
  return queue_ != nullptr && xQueueReceive(queue_, &event, 0) == pdTRUE;
}

uint32_t InputEventQueue::depth() const {
  return queue_ != nullptr ? static_cast<uint32_t>(uxQueueMessagesWaiting(queue_)) : 0;
}
