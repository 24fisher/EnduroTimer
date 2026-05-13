#pragma once

#include <Arduino.h>
#include <vector>

class OledDisplay {
public:
  bool begin();
  bool available() const { return initialized_; }
  void showLines(const std::vector<String>& lines);
  void showBoot(const String& role);
  void showCountdown(const String& text);

private:
  bool initialized_ = false;
};
