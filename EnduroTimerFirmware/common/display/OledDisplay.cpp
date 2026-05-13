#include "OledDisplay.h"

#include <U8g2lib.h>
#include <Wire.h>

#ifndef OLED_SDA
#define OLED_SDA 17
#endif
#ifndef OLED_SCL
#define OLED_SCL 18
#endif
#ifndef OLED_RST
#define OLED_RST 21
#endif
#ifndef VEXT_PIN
#define VEXT_PIN 36
#endif

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

bool OledDisplay::begin() {
  Serial.printf("OLED: init... SDA=%d SCL=%d RST=%d VEXT=%d\n", OLED_SDA, OLED_SCL, OLED_RST, VEXT_PIN);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(50);

  Wire.begin(OLED_SDA, OLED_SCL);
  initialized_ = display.begin();
  if (!initialized_) {
    Serial.println("OLED: init failed");
    return false;
  }

  display.setFont(u8g2_font_6x10_tf);
  display.clearBuffer();
  display.drawStr(0, 12, "ENDURO TIMER");
  display.drawStr(0, 28, "OLED OK");
  display.sendBuffer();
  Serial.println("OLED: OK");
  return true;
}

void OledDisplay::showLines(const std::vector<String>& lines) {
  if (!initialized_) return;

  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  int y = 10;
  for (const String& line : lines) {
    display.drawStr(0, y, line.c_str());
    y += 10;
    if (y > 64) break;
  }
  display.sendBuffer();
}

void OledDisplay::showBoot(const String& role) {
  showLines({"ENDURO TIMER", role, "BOOTING..."});
}

void OledDisplay::showCountdown(const String& text) {
  if (!initialized_) return;

  display.clearBuffer();
  display.setFont(u8g2_font_logisoso46_tf);
  const int16_t width = display.getStrWidth(text.c_str());
  int16_t x = (128 - width) / 2;
  if (x < 0) x = 0;
  display.drawStr(x, 56, text.c_str());
  display.sendBuffer();
}
