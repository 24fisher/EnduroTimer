#include <Arduino.h>
#include <rom/rtc.h>

#include "StartStationApp.h"

#if ENABLE_WIFI && ENABLE_WEB
#include "WebServerController.h"
#endif

#if ENABLE_WIFI
#include <WiFi.h>
#endif

StartStationApp app;
#if ENABLE_WIFI && ENABLE_WEB
WebServerController web(app);
#endif

#if ENABLE_WIFI
static bool beginWifiApOnly() {
  Serial.println("WiFi AP init...");
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  const bool apStarted = WiFi.softAP("EnduroTimer", "endurotimer");
  if (apStarted) {
    Serial.printf("WiFi AP OK, IP=%s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("WiFi AP: SSID EnduroTimer");
    Serial.printf("WiFi AP: MAC %s\n", WiFi.softAPmacAddress().c_str());
  } else {
    Serial.println("WiFi AP FAIL");
  }
  return apStarted;
}
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("ENDURO TIMER START STATION APP ENTRY");
  Serial.println("Build: " __DATE__ " " __TIME__);
  Serial.println("Serial OK");
  Serial.println("================================");
  Serial.printf("[BOOT] Reset reason CPU0=%d CPU1=%d\n", rtc_get_reset_reason(0), rtc_get_reset_reason(1));

  app.begin();

#if ENABLE_WIFI && ENABLE_WEB
  web.begin();
  app.setWifiStatus(web.apStarted(), WiFi.softAPIP(), WiFi.softAPmacAddress());
  app.setWebStatus(web.webStarted());
#elif ENABLE_WIFI
  const bool apStarted = beginWifiApOnly();
  app.setWifiStatus(apStarted, WiFi.softAPIP(), WiFi.softAPmacAddress());
  app.setWebStatus(false);
#else
  Serial.println("[BOOT] WiFi AP init skipped (ENABLE_WIFI=0)");
  app.setWebStatus(false);
#endif

#if ENABLE_LORA
  app.beginRadio();
#else
  Serial.println("[BOOT] LoRa init skipped (ENABLE_LORA=0)");
#endif
  Serial.println("[BOOT] State Ready");
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
#if ENABLE_WIFI && ENABLE_WEB
  web.loop();
#endif
  app.loopDisplayTask();
  yield();
}
