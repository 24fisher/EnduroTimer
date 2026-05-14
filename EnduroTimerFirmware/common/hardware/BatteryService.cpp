#include "BatteryService.h"

void BatteryService::begin() {
#if BATTERY_ADC_ENABLED
  #ifdef BATTERY_ADC_PIN
  pinMode(BATTERY_ADC_PIN, INPUT);
  #endif
#endif
}

BatteryStatus BatteryService::read() const {
  BatteryStatus status;
#if BATTERY_ADC_ENABLED
  #ifdef BATTERY_ADC_PIN
  const uint32_t measuredMv = analogReadMilliVolts(BATTERY_ADC_PIN);
  status.voltage = (static_cast<float>(measuredMv) / 1000.0F) * BATTERY_VOLTAGE_DIVIDER_RATIO;
  status.available = status.voltage > 0.1F;
  status.usbPoweredOrUnknown = !status.available;
  const float span = BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE;
  if (status.available && span > 0.01F) {
    int percent = static_cast<int>(((status.voltage - BATTERY_MIN_VOLTAGE) * 100.0F / span) + 0.5F);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    status.percent = percent;
  }
  #endif
#endif
  return status;
}
