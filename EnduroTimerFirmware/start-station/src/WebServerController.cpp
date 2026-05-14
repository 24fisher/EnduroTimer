#include "WebServerController.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "StartStationApp.h"

namespace {
const char FallbackIndex[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>EnduroTimer StartStation</title></head>
<body style="font-family:system-ui,sans-serif;background:#0d1117;color:#f0f6fc;padding:24px">
  <h1>EnduroTimer StartStation</h1>
  <p>LittleFS web files not found</p>
  <p>Use: <code>pio run -e start_station -t uploadfs</code></p>
</body>
</html>
)HTML";
}

WebServerController::WebServerController(StartStationApp& app) : app_(app), server_(80) {}

bool WebServerController::begin() {
  Serial.println("[BOOT] WiFi AP init...");
  Serial.println("WiFi AP starting...");
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  apStarted_ = WiFi.softAP("EnduroTimer", "endurotimer");
  if (apStarted_) {
    Serial.println("WiFi AP OK");
    Serial.println("SSID: EnduroTimer");
    Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("WiFi AP: MAC %s\n", WiFi.softAPmacAddress().c_str());
    Serial.printf("[BOOT] WiFi AP OK ssid=EnduroTimer ip=%s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("WiFi AP FAIL");
    Serial.println("[BOOT] WiFi AP FAIL");
  }

  Serial.println("[BOOT] LittleFS init...");
  fsMounted_ = LittleFS.begin(true);
  Serial.println(fsMounted_ ? "[BOOT] LittleFS OK" : "[BOOT] LittleFS FAIL");

  Serial.println("[BOOT] WebServer init...");
  server_.on("/", HTTP_GET, [this]() {
    if (!serveStaticFile("/index.html")) sendFallbackIndex();
  });

  server_.on("/api/status", HTTP_GET, [this]() { sendJson(200, app_.statusJson()); });

  server_.on("/api/time/sync", HTTP_POST, [this]() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["message"] = "RTC is not installed yet; millis-based clock is used.";
    doc["uptimeMs"] = millis();
    String output;
    serializeJson(doc, output);
    sendJson(200, output);
  });

  server_.on("/api/runs/start", HTTP_POST, [this]() {
#if ENABLE_WEB_START
    String error;
    if (!app_.requestStartRun(error)) {
      sendError(409, error.length() > 0 ? error : String("Run already active"));
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["state"] = "Countdown";
    String output;
    serializeJson(doc, output);
    sendJson(200, output);
#else
    sendError(403, "Start is only available from hardware button");
#endif
  });

  server_.on("/api/system/reset", HTTP_POST, [this]() {
    app_.resetSystem();
    JsonDocument doc;
    doc["ok"] = true;
    doc["state"] = "Ready";
    String output;
    serializeJson(doc, output);
    sendJson(200, output);
  });

  server_.on("/api/runs", HTTP_GET, [this]() { sendJson(200, app_.runsJson()); });
  server_.on("/api/export/runs.csv", HTTP_GET, [this]() { sendCsv(app_.runsCsv()); });
  server_.on("/api/riders", HTTP_GET, [this]() { sendJson(200, app_.ridersJson()); });
  server_.on("/api/trails", HTTP_GET, [this]() { sendJson(200, app_.trailsJson()); });
  server_.on("/api/settings", HTTP_GET, [this]() { sendJson(200, app_.settingsJson()); });

  server_.on("/api/riders/add", HTTP_POST, [this]() {
    JsonDocument doc;
    deserializeJson(doc, server_.arg("plain"));
    String error;
    if (!app_.addRider(doc["displayName"] | "", error)) { sendError(400, error); return; }
    sendJson(200, String("{\"ok\":true}"));
  });

  server_.on("/api/riders/deactivate", HTTP_POST, [this]() {
    JsonDocument doc;
    deserializeJson(doc, server_.arg("plain"));
    String error;
    if (!app_.deactivateRider(doc["riderId"] | "", error)) { sendError(404, error); return; }
    sendJson(200, String("{\"ok\":true}"));
  });

  server_.on("/api/trails/add", HTTP_POST, [this]() {
    JsonDocument doc;
    deserializeJson(doc, server_.arg("plain"));
    String error;
    if (!app_.addTrail(doc["displayName"] | "", error)) { sendError(400, error); return; }
    sendJson(200, String("{\"ok\":true}"));
  });

  server_.on("/api/trails/deactivate", HTTP_POST, [this]() {
    JsonDocument doc;
    deserializeJson(doc, server_.arg("plain"));
    String error;
    if (!app_.deactivateTrail(doc["trailId"] | "", error)) { sendError(404, error); return; }
    sendJson(200, String("{\"ok\":true}"));
  });

  server_.on("/api/settings", HTTP_POST, [this]() {
    JsonDocument doc;
    deserializeJson(doc, server_.arg("plain"));
    String error;
    if (!app_.updateSettings(doc["selectedRiderId"] | "", doc["selectedTrailId"] | "", error)) { sendError(400, error); return; }
    sendJson(200, String("{\"ok\":true}"));
  });

  server_.onNotFound([this]() {
    String path = server_.uri();
    if (path == "/") path = "/index.html";
    if (serveStaticFile(path)) return;
    if (path == "/index.html") {
      sendFallbackIndex();
      return;
    }
    sendError(404, "Not found");
  });

  server_.begin();
  webStarted_ = apStarted_;
  Serial.println(webStarted_ ? "[BOOT] WebServer OK" : "[BOOT] WebServer FAIL");
  return webStarted_;
}

void WebServerController::loop() {
  server_.handleClient();
}

void WebServerController::sendJson(int code, const String& body) {
  server_.sendHeader("Access-Control-Allow-Origin", "*");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

void WebServerController::sendCsv(const String& body) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Content-Disposition", "attachment; filename=\"enduro_runs.csv\"");
  server_.send(200, "text/csv; charset=utf-8", body);
}

void WebServerController::sendError(int code, const String& message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  String output;
  serializeJson(doc, output);
  sendJson(code, output);
}

void WebServerController::sendFallbackIndex() {
  server_.send_P(200, "text/html", FallbackIndex);
}

bool WebServerController::serveStaticFile(const String& path) {
  if (!fsMounted_ || !LittleFS.exists(path)) return false;

  String contentType = "text/plain";
  if (path.endsWith(".html")) contentType = "text/html";
  if (path.endsWith(".css")) contentType = "text/css";
  if (path.endsWith(".js")) contentType = "application/javascript";

  File file = LittleFS.open(path, "r");
  server_.streamFile(file, contentType);
  file.close();
  return true;
}
