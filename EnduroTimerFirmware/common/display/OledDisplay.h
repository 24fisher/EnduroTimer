#pragma once

#include <Arduino.h>
#include <vector>

class OledDisplay {
public:
  void begin();
  void showLines(const std::vector<String>& lines);
  void showBoot(const String& role);

private:
  bool initialized_ = false;
};
