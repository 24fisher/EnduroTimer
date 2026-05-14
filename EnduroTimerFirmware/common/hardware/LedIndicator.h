#pragma once

#include <Arduino.h>

#ifndef STATUS_LED_PIN
#ifdef LED_BUILTIN
#define STATUS_LED_PIN LED_BUILTIN
#else
#define STATUS_LED_PIN -1
#endif
#endif

#ifndef STATUS_LED_ACTIVE_LEVEL
#define STATUS_LED_ACTIVE_LEVEL 1
#endif

enum class LedMode {
  Off,
  ReadySlowBlink,
  CountdownFastBlink,
  RidingSolid,
  FinishFlash,
  ErrorFastBlink,
  NoSignalBlink,
  AckTimeoutBlink
};

class LedIndicator {
public:
  void begin();
  void update();
  void setMode(LedMode mode);
  void flash(uint8_t count, uint16_t onMs, uint16_t offMs);

  bool enabled() const { return enabled_; }
  LedMode mode() const { return mode_; }

private:
  void writeLed(bool on);
  void applyPattern(uint32_t nowMs, uint16_t onMs, uint16_t offMs);
  static const char* modeName(LedMode mode);

  bool enabled_ = false;
  bool ledOn_ = false;
  bool flashActive_ = false;
  bool flashPhaseOn_ = false;
  uint8_t flashRemaining_ = 0;
  uint16_t flashOnMs_ = 0;
  uint16_t flashOffMs_ = 0;
  uint32_t lastToggleMs_ = 0;
  LedMode mode_ = LedMode::Off;
};
