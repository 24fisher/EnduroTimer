#pragma once

#include <Arduino.h>
#include <WebServer.h>

class StartStationApp;

class WebServerController {
public:
  explicit WebServerController(StartStationApp& app);
  void begin();
  void loop();

private:
  void sendJson(int code, const String& body);
  void sendError(int code, const String& message);
  bool serveStaticFile(const String& path);

  StartStationApp& app_;
  WebServer server_;
};
