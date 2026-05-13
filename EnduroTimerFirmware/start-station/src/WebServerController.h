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
  bool webStarted() const { return webStarted_; }

private:
  void sendJson(int code, const String& body);
  void sendError(int code, const String& message);
  bool serveStaticFile(const String& path);
  void sendFallbackIndex();

  StartStationApp& app_;
  WebServer server_;
  bool apStarted_ = false;
  bool fsMounted_ = false;
  bool webStarted_ = false;
};
