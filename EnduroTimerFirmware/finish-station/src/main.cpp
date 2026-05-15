#include <Arduino.h>
#include <rom/rtc.h>

#include "FinishStationApp.h"

FinishStationApp app;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER FINISH STATION APP ENTRY");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");
  Serial.printf("[BOOT] Reset reason CPU0=%d CPU1=%d\n", rtc_get_reset_reason(0), rtc_get_reset_reason(1));

  app.begin();
}

void loop() {
  static uint32_t lastLog = 0;
  const uint32_t now = millis();
  if (now - lastLog > 5000UL) {
    lastLog = now;
    Serial.print("APP alive ms=");
    Serial.println(now);
  }

  app.loop();
  yield();
}
