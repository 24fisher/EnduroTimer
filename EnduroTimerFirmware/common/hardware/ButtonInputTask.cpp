#include "ButtonInputTask.h"

bool ButtonInputTask::begin(uint8_t pin, InputEventType eventType, InputEventQueue& queue,
                            bool activeLow, uint32_t debounceMs, uint32_t periodMs,
                            UBaseType_t priority, BaseType_t core) {
  if (taskHandle_ != nullptr) return true;

  pin_ = pin;
  eventType_ = eventType;
  queue_ = &queue;
  activeLow_ = activeLow;
  debounceMs_ = debounceMs;
  periodTicks_ = pdMS_TO_TICKS(periodMs);
  if (periodTicks_ == 0) periodTicks_ = 1;

  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT);
  rawGpio_ = digitalRead(pin_);
  pressed_ = isPressedReading(rawGpio_);

  const BaseType_t result = xTaskCreatePinnedToCore(
      taskEntry, "ButtonInput", 3072, this, priority, &taskHandle_, core);
  if (result != pdPASS) {
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

void ButtonInputTask::taskEntry(void* context) {
  static_cast<ButtonInputTask*>(context)->run();
}

void ButtonInputTask::run() {
  int stableState = rawGpio_;
  int lastReading = rawGpio_;
  bool pressArmed = !isPressedReading(stableState);
  uint32_t rawPressedMs = 0;
  uint32_t lastChangeMs = millis();
  TickType_t lastWakeTime = xTaskGetTickCount();

  for (;;) {
    const uint32_t nowMs = millis();
    const int reading = digitalRead(pin_);
    rawGpio_ = reading;

    if (reading != lastReading) {
      lastReading = reading;
      lastChangeMs = nowMs;
      if (isPressedReading(reading)) rawPressedMs = nowMs;
    }

    if (reading != stableState && nowMs - lastChangeMs >= debounceMs_) {
      const int previousStableState = stableState;
      stableState = reading;
      pressed_ = isPressedReading(stableState);

      if (pressed_ && pressArmed && !isPressedReading(previousStableState)) {
        pressArmed = false;
        lastDebounceLatencyMs_ = rawPressedMs > 0 ? nowMs - rawPressedMs : 0;
        if (lastDebounceLatencyMs_ > maxDebounceLatencyMs_) {
          maxDebounceLatencyMs_ = lastDebounceLatencyMs_;
        }
        const InputEvent event{
            eventType_,
            rawPressedMs > 0 ? rawPressedMs : nowMs,
            static_cast<uint32_t>(reading)};
        queue_->push(event);
      } else if (!pressed_) {
        pressArmed = true;
      }
    }

    vTaskDelayUntil(&lastWakeTime, periodTicks_);
  }
}

bool ButtonInputTask::isPressedReading(int reading) const {
  return activeLow_ ? reading == LOW : reading == HIGH;
}
