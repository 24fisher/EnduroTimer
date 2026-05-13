#include <Arduino.h>

#include "FinishStationApp.h"

FinishStationApp app;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER FINISH STATION BOOT");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");

  app.begin();
}

void loop() {
  app.loop();
  delay(2);
}
