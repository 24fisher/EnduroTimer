#pragma once

#include <Arduino.h>

#ifndef BATTERY_ADC_ENABLED
#define BATTERY_ADC_ENABLED 0
#endif
#ifndef BATTERY_VOLTAGE_DIVIDER_RATIO
#define BATTERY_VOLTAGE_DIVIDER_RATIO 1.0F
#endif
#ifndef BATTERY_MIN_VOLTAGE
#define BATTERY_MIN_VOLTAGE 3.3F
#endif
#ifndef BATTERY_MAX_VOLTAGE
#define BATTERY_MAX_VOLTAGE 4.2F
#endif

struct BatteryStatus {
  bool available = false;
  bool usbPoweredOrUnknown = true;
  float voltage = 0.0F;
  int percent = -1;
};

class BatteryService {
public:
  void begin();
  BatteryStatus read() const;
};
