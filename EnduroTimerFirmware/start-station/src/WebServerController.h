#pragma once

#include <Arduino.h>
#include <WebServer.h>

class StartStationApp;

class WebServerController {
public:
  explicit WebServerController(StartStationApp& app);
  bool begin();
  void loop();
  bool apStarted() const { return apStarted_; }

private:
  void sendJson(int code, const String& body);
  void sendError(int code, const String& message);
  bool serveStaticFile(const String& path);

  StartStationApp& app_;
  WebServer server_;
  bool apStarted_ = false;
  bool webStarted_ = false;
};
