#include "LedIndicator.h"

void LedIndicator::begin() {
#if STATUS_LED_PIN >= 0
  enabled_ = true;
  pinMode(STATUS_LED_PIN, OUTPUT);
  ledOn_ = true;
  writeLed(false);
  Serial.printf("LED indicator pin=%d activeLevel=%d\n", STATUS_LED_PIN, STATUS_LED_ACTIVE_LEVEL);
#else
  enabled_ = false;
  Serial.println("LED indicator disabled");
#endif
}

void LedIndicator::update() {
  if (!enabled_) return;

  const uint32_t nowMs = millis();
  if (flashActive_) {
    const uint16_t interval = flashPhaseOn_ ? flashOnMs_ : flashOffMs_;
    if (nowMs - lastToggleMs_ >= interval) {
      if (flashPhaseOn_) {
        flashPhaseOn_ = false;
        writeLed(false);
      } else if (flashRemaining_ > 0) {
        flashRemaining_ -= 1;
        if (flashRemaining_ == 0) {
          flashActive_ = false;
          lastToggleMs_ = nowMs;
          writeLed(false);
          return;
        }
        flashPhaseOn_ = true;
        writeLed(true);
      }
      lastToggleMs_ = nowMs;
    }
    return;
  }

  switch (mode_) {
    case LedMode::Off:
      writeLed(false);
      break;
    case LedMode::ReadySlowBlink:
      applyPattern(nowMs, 120U, 1880U);
      break;
    case LedMode::CountdownFastBlink:
      applyPattern(nowMs, 100U, 100U);
      break;
    case LedMode::RidingSolid:
      writeLed(true);
      break;
    case LedMode::FinishFlash:
      applyPattern(nowMs, 100U, 120U);
      break;
    case LedMode::ErrorFastBlink:
    case LedMode::AckTimeoutBlink:
      applyPattern(nowMs, 120U, 120U);
      break;
    case LedMode::NoSignalBlink:
      applyPattern(nowMs, 80U, 920U);
      break;
  }
}

void LedIndicator::setMode(LedMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  lastToggleMs_ = millis();
  Serial.printf("LED mode: %s\n", modeName(mode_));
}

void LedIndicator::flash(uint8_t count, uint16_t onMs, uint16_t offMs) {
  if (!enabled_ || count == 0) return;
  mode_ = LedMode::FinishFlash;
  flashActive_ = true;
  flashPhaseOn_ = true;
  flashRemaining_ = count;
  flashOnMs_ = onMs;
  flashOffMs_ = offMs;
  lastToggleMs_ = millis();
  Serial.printf("LED mode: %s\n", modeName(mode_));
  writeLed(true);
}

void LedIndicator::writeLed(bool on) {
  if (!enabled_ || ledOn_ == on) return;
  ledOn_ = on;
  const int activeLevel = STATUS_LED_ACTIVE_LEVEL ? HIGH : LOW;
  digitalWrite(STATUS_LED_PIN, on ? activeLevel : (activeLevel == HIGH ? LOW : HIGH));
}

void LedIndicator::applyPattern(uint32_t nowMs, uint16_t onMs, uint16_t offMs) {
  const uint16_t interval = ledOn_ ? onMs : offMs;
  if (nowMs - lastToggleMs_ >= interval) {
    writeLed(!ledOn_);
    lastToggleMs_ = nowMs;
  }
}

const char* LedIndicator::modeName(LedMode mode) {
  switch (mode) {
    case LedMode::Off: return "Off";
    case LedMode::ReadySlowBlink: return "ReadySlowBlink";
    case LedMode::CountdownFastBlink: return "CountdownFastBlink";
    case LedMode::RidingSolid: return "RidingSolid";
    case LedMode::FinishFlash: return "FinishFlash";
    case LedMode::ErrorFastBlink: return "ErrorFastBlink";
    case LedMode::NoSignalBlink: return "NoSignalBlink";
    case LedMode::AckTimeoutBlink: return "AckTimeoutBlink";
  }
  return "Unknown";
}
