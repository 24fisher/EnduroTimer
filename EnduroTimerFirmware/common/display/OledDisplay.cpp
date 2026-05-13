#include "OledDisplay.h"

#include <U8g2lib.h>
#include <Wire.h>

#ifndef OLED_SDA
#ifdef SDA_OLED
#define OLED_SDA SDA_OLED
#else
#define OLED_SDA 17
#endif
#endif

#ifndef OLED_SCL
#ifdef SCL_OLED
#define OLED_SCL SCL_OLED
#else
#define OLED_SCL 18
#endif
#endif

#ifndef OLED_RST
#ifdef RST_OLED
#define OLED_RST RST_OLED
#else
#define OLED_RST 21
#endif
#endif

#ifndef OLED_VEXT
#ifdef VEXT_PIN
#define OLED_VEXT VEXT_PIN
#elif defined(Vext)
#define OLED_VEXT Vext
#else
#define OLED_VEXT -1
#endif
#endif

#ifndef OLED_SCAN_ONLY
#define OLED_SCAN_ONLY 0
#endif

#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3C
#endif

static constexpr uint32_t OledI2cClockHz = 100000UL;
static constexpr uint16_t OledI2cTimeoutMs = 10;
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

static bool scanI2cBus(uint8_t requiredAddress) {
  bool foundAny = false;
  bool foundRequired = false;

  Serial.println("I2C scan start");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      foundAny = true;
      Serial.printf("Found I2C device: 0x%02X\n", address);
      if (address == requiredAddress) {
        foundRequired = true;
      }
    }
    delay(1);
  }

  if (!foundAny) {
    Serial.println("No I2C devices found");
  }
  Serial.println("I2C scan done");
  return foundRequired;
}

bool OledDisplay::begin() {
  initialized_ = false;
  Serial.printf("OLED init... SDA=%d SCL=%d RST=%d VEXT=%d SCAN_ONLY=%d\n", OLED_SDA, OLED_SCL, OLED_RST, OLED_VEXT, OLED_SCAN_ONLY);
  Serial.println("OLED init begin");

#if OLED_VEXT >= 0
  pinMode(OLED_VEXT, OUTPUT);
  digitalWrite(OLED_VEXT, LOW);
  delay(50);
#else
  Serial.println("OLED VEXT skipped (OLED_VEXT < 0)");
#endif

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(OledI2cClockHz);
  Wire.setTimeOut(OledI2cTimeoutMs);

  const bool oledAddressFound = scanI2cBus(OLED_I2C_ADDRESS);
  if (!oledAddressFound) {
    Serial.printf("OLED not found at 0x%02X\n", OLED_I2C_ADDRESS);
    Serial.println("OLED FAIL");
    return false;
  }

#if OLED_SCAN_ONLY
  Serial.println("OLED scan-only mode, display init skipped");
  Serial.println("OLED FAIL");
  return false;
#else
  initialized_ = display.begin();
  if (!initialized_) {
    Serial.println("OLED FAIL");
    return false;
  }

  display.setFont(u8g2_font_6x10_tf);
  display.clearBuffer();
  display.drawStr(0, 12, "ENDURO TIMER");
  display.drawStr(0, 28, "OLED OK");
  display.sendBuffer();
  Serial.println("OLED OK");
  return true;
#endif
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
