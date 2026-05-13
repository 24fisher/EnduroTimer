#include <Arduino.h>

#include "FinishStationApp.h"

FinishStationApp app;

void setup() {
  app.begin();
}

void loop() {
  app.loop();
  delay(2);
}
