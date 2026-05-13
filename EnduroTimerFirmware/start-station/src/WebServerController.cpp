#include "WebServerController.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "StartStationApp.h"

WebServerController::WebServerController(StartStationApp& app) : app_(app), server_(80) {}

void WebServerController::begin() {
  WiFi.mode(WIFI_AP);
  const bool apStarted = WiFi.softAP("EnduroTimer", "endurotimer");
  Serial.printf("[StartStation] Wi-Fi AP %s IP=%s\n", apStarted ? "started" : "failed", WiFi.softAPIP().toString().c_str());

  if (!LittleFS.begin(true)) {
    Serial.println("[StartStation] LittleFS mount failed");
  }

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
    String error;
    if (!app_.requestStartRun(error)) {
      sendError(409, error);
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["state"] = "Countdown";
    String output;
    serializeJson(doc, output);
    sendJson(202, output);
  });

  server_.on("/api/system/reset", HTTP_POST, [this]() {
    app_.resetSystem();
    JsonDocument doc;
    doc["ok"] = true;
    String output;
    serializeJson(doc, output);
    sendJson(200, output);
  });

  server_.on("/api/runs", HTTP_GET, [this]() { sendJson(200, app_.runsJson()); });

  server_.onNotFound([this]() {
    String path = server_.uri();
    if (path == "/") path = "/index.html";
    if (!serveStaticFile(path)) sendError(404, "Not found");
  });

  server_.begin();
  Serial.println("[StartStation] Web server started");
}

void WebServerController::loop() {
  server_.handleClient();
}

void WebServerController::sendJson(int code, const String& body) {
  server_.sendHeader("Access-Control-Allow-Origin", "*");
  server_.send(code, "application/json", body);
}

void WebServerController::sendError(int code, const String& message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  String output;
  serializeJson(doc, output);
  sendJson(code, output);
}

bool WebServerController::serveStaticFile(const String& path) {
  if (!LittleFS.exists(path)) return false;

  String contentType = "text/plain";
  if (path.endsWith(".html")) contentType = "text/html";
  if (path.endsWith(".css")) contentType = "text/css";
  if (path.endsWith(".js")) contentType = "application/javascript";

  File file = LittleFS.open(path, "r");
  server_.streamFile(file, contentType);
  file.close();
  return true;
}
