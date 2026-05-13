#include <Arduino.h>

#include "StartStationApp.h"
#include "WebServerController.h"

#include <WiFi.h>

StartStationApp app;
WebServerController web(app);

void setup() {
  app.begin();
  web.begin();
  app.setWifiStatus(web.apStarted(), WiFi.softAPIP(), WiFi.softAPmacAddress());
}

void loop() {
  app.loop();
  web.loop();
  delay(2);
}
