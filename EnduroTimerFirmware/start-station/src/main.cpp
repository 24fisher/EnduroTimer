#include <Arduino.h>

#include "StartStationApp.h"
#include "WebServerController.h"

#include <WiFi.h>

StartStationApp app;
WebServerController web(app);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER START STATION BOOT");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");

  app.begin();
  web.begin();
  app.setWifiStatus(web.apStarted(), WiFi.softAPIP(), WiFi.softAPmacAddress());
}

void loop() {
  app.loop();
  web.loop();
  delay(2);
}
