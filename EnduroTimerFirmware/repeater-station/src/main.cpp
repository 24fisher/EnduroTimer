#include <Arduino.h>
#include <rom/rtc.h>

#include "RepeaterStationApp.h"

RepeaterStationApp app;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER REPEATER APP ENTRY");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");
  Serial.printf("[BOOT] Reset reason CPU0=%d CPU1=%d\n", rtc_get_reset_reason(0), rtc_get_reset_reason(1));
  app.begin();
}

void loop() {
  app.loop();
  yield();
}
