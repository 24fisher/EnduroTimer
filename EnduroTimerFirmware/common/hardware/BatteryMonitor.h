#pragma once

#include <Arduino.h>

// Heltec WiFi LoRa 32 V3 battery measurement circuit:
// GPIO1 reads VBAT through the onboard divider; GPIO37 enables that divider.
// Heltec's V3.2 hardware update requires ADC_Ctrl (GPIO37) to be pulled high.
static constexpr int BATTERY_MONITOR_ADC_PIN = 1;
static constexpr int BATTERY_MONITOR_CTRL_PIN = 37;
static constexpr uint8_t BATTERY_MONITOR_CTRL_ACTIVE_LEVEL = HIGH;
static constexpr float BATTERY_MONITOR_DIVIDER_RATIO = 4.9F;
static constexpr uint32_t BATTERY_MONITOR_SAMPLE_INTERVAL_MS = 3000UL;
static constexpr uint8_t BATTERY_MONITOR_FILTER_SAMPLES = 4;
static constexpr uint32_t BATTERY_MONITOR_MIN_VALID_MV = 3000UL;
static constexpr uint32_t BATTERY_MONITOR_MAX_VALID_MV = 4400UL;

class BatteryMonitor {
public:
  void begin();
  void update(uint32_t nowMs);
  uint32_t voltageMv() const { return filteredVoltageMv_; }
  int percent() const { return valid_ ? percent_ : -1; }
  bool isChargingKnown() const { return false; }
  bool isValid() const { return valid_; }

private:
  static int percentForVoltage(uint32_t voltageMv);
  uint32_t readVoltageMv() const;

  uint32_t samples_[BATTERY_MONITOR_FILTER_SAMPLES] = {};
  uint8_t sampleCount_ = 0;
  uint8_t sampleCursor_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t filteredVoltageMv_ = 0;
  int percent_ = -1;
  bool valid_ = false;
};
