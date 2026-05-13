#include <Arduino.h>

#include "FinishStationApp.h"

FinishStationApp app;

static void blinkDiagnosticLed(uint32_t nowMs) {
#ifdef LED_BUILTIN
  static bool configured = false;
  static bool ledOn = false;
  static uint32_t lastBlinkMs = 0;
  if (!configured) {
    pinMode(LED_BUILTIN, OUTPUT);
    configured = true;
  }
  if (nowMs - lastBlinkMs >= 1000UL) {
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
    lastBlinkMs = nowMs;
  }
#else
  (void)nowMs;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER FINISH STATION APP ENTRY");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");

  app.begin();
}

void loop() {
  static uint32_t lastLog = 0;
  const uint32_t now = millis();
  blinkDiagnosticLed(now);

  if (now - lastLog > 1000UL) {
    lastLog = now;
    Serial.print("APP alive ms=");
    Serial.println(now);
  }

  app.loop();
  delay(2);
}
