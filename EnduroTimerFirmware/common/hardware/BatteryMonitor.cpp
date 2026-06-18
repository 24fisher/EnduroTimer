#include "BatteryMonitor.h"

namespace {
struct BatteryCurvePoint {
  uint32_t voltageMv;
  int percent;
};

static constexpr BatteryCurvePoint BATTERY_CURVE[] = {
    {3400, 0}, {3500, 5}, {3600, 15}, {3700, 30}, {3800, 45},
    {3900, 60}, {4000, 75}, {4100, 90}, {4200, 100},
};
}

void BatteryMonitor::begin() {
  pinMode(BATTERY_MONITOR_CTRL_PIN, OUTPUT);
  digitalWrite(BATTERY_MONITOR_CTRL_PIN, !BATTERY_MONITOR_CTRL_ACTIVE_LEVEL);
  pinMode(BATTERY_MONITOR_ADC_PIN, INPUT);
  analogSetPinAttenuation(BATTERY_MONITOR_ADC_PIN, ADC_11db);
  lastSampleMs_ = 0;
}

void BatteryMonitor::update(uint32_t nowMs) {
  if (lastSampleMs_ > 0 && nowMs - lastSampleMs_ < BATTERY_MONITOR_SAMPLE_INTERVAL_MS) return;
  lastSampleMs_ = nowMs;

  const uint32_t measuredMv = readVoltageMv();
  if (measuredMv < BATTERY_MONITOR_MIN_VALID_MV || measuredMv > BATTERY_MONITOR_MAX_VALID_MV) {
    valid_ = false;
    filteredVoltageMv_ = 0;
    percent_ = -1;
    sampleCount_ = 0;
    sampleCursor_ = 0;
    return;
  }

  samples_[sampleCursor_] = measuredMv;
  sampleCursor_ = (sampleCursor_ + 1) % BATTERY_MONITOR_FILTER_SAMPLES;
  if (sampleCount_ < BATTERY_MONITOR_FILTER_SAMPLES) sampleCount_ += 1;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < sampleCount_; ++i) sum += samples_[i];
  filteredVoltageMv_ = sum / sampleCount_;
  percent_ = percentForVoltage(filteredVoltageMv_);
  valid_ = true;
}

uint32_t BatteryMonitor::readVoltageMv() const {
  digitalWrite(BATTERY_MONITOR_CTRL_PIN, BATTERY_MONITOR_CTRL_ACTIVE_LEVEL);
  delayMicroseconds(250);
  const uint32_t adcMv = analogReadMilliVolts(BATTERY_MONITOR_ADC_PIN);
  digitalWrite(BATTERY_MONITOR_CTRL_PIN, !BATTERY_MONITOR_CTRL_ACTIVE_LEVEL);
  return static_cast<uint32_t>(static_cast<float>(adcMv) * BATTERY_MONITOR_DIVIDER_RATIO + 0.5F);
}

int BatteryMonitor::percentForVoltage(uint32_t voltageMv) {
  if (voltageMv <= BATTERY_CURVE[0].voltageMv) return BATTERY_CURVE[0].percent;
  const size_t pointCount = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);
  if (voltageMv >= BATTERY_CURVE[pointCount - 1].voltageMv) return BATTERY_CURVE[pointCount - 1].percent;

  for (size_t i = 1; i < pointCount; ++i) {
    if (voltageMv <= BATTERY_CURVE[i].voltageMv) {
      const BatteryCurvePoint& low = BATTERY_CURVE[i - 1];
      const BatteryCurvePoint& high = BATTERY_CURVE[i];
      const uint32_t offsetMv = voltageMv - low.voltageMv;
      const uint32_t spanMv = high.voltageMv - low.voltageMv;
      return low.percent + static_cast<int>((offsetMv * (high.percent - low.percent) + spanMv / 2) / spanMv);
    }
  }
  return 0;
}
