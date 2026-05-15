#pragma once

#include <Arduino.h>
#include <vector>

class OledDisplay {
public:
  bool begin();
  bool available() const { return initialized_; }
  bool isAvailable() const { return available(); }
  bool testPatternOnly() const;

  void update();
  void showLines(const std::vector<String>& lines);
  void showBoot(const String& role);
  void showBootScreen(const String& role);
  void showStatus(const String& line1, const String& line2 = String(), const String& line3 = String(), const String& line4 = String());
  void showCountdown(const String& text, const String& role = String("START"));
  void showFinishLineCrossed();
  void showResult(const String& result, const String& detail = String());

private:
  bool initialized_ = false;
  uint8_t address_ = 0;
  std::vector<String> lastLines_;
  String lastFrameKey_;
  uint32_t lastRenderMs_ = 0;
};
