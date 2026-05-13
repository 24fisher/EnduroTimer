#include <Arduino.h>

#include "StartStationApp.h"
#include "WebServerController.h"

StartStationApp app;
WebServerController web(app);

void setup() {
  app.begin();
  web.begin();
}

void loop() {
  app.loop();
  web.loop();
  delay(2);
}
