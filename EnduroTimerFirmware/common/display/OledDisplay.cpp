#include "OledDisplay.h"

#include <U8g2lib.h>
#include <Wire.h>

#include <memory>

#ifndef ENABLE_OLED
#define ENABLE_OLED 1
#endif

// Heltec WiFi LoRa 32 V3 built-in OLED: SDA GPIO17, SCL GPIO18, RST GPIO21,
// VEXT GPIO36 active LOW, SSD1306 I2C address 0x3C. All values can be
// overridden from platformio.ini build flags.
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
#define OLED_VEXT 36
#endif
#endif

#ifndef OLED_VEXT_ON_LEVEL
#define OLED_VEXT_ON_LEVEL 0
#endif

#ifndef OLED_SCAN_ONLY
#define OLED_SCAN_ONLY 0
#endif

#ifndef OLED_DRAW_TEST_ONLY
#define OLED_DRAW_TEST_ONLY 0
#endif

#ifndef OLED_TEST_PATTERN_ONLY
#define OLED_TEST_PATTERN_ONLY 0
#endif

#ifndef OLED_INVERT_TEST
#define OLED_INVERT_TEST 0
#endif

#define SSD1306_NONAME 1
#define SSD1306_VCOMH0 2
#define SH1106 3
#define SSD1306_64_NONAME 4

#ifndef OLED_DRIVER_TYPE
#define OLED_DRIVER_TYPE SSD1306_NONAME
#endif

static constexpr uint32_t OledI2cClockHz = 100000UL;
static constexpr uint16_t OledI2cTimeoutMs = 50;
static const uint8_t OledAddressCandidates[] = {0x3C, 0x3D};

#if OLED_DRIVER_TYPE == SSD1306_VCOMH0
using OledDisplayDriver = U8G2_SSD1306_128X64_VCOMH0_F_SW_I2C;
static const char* OledDriverTypeName = "SSD1306_VCOMH0";
#elif OLED_DRIVER_TYPE == SH1106
using OledDisplayDriver = U8G2_SH1106_128X64_NONAME_F_SW_I2C;
static const char* OledDriverTypeName = "SH1106";
#elif OLED_DRIVER_TYPE == SSD1306_64_NONAME
// U8g2 has no separate 128x64 constructor named SSD1306_64_NONAME in the
// supported matrix, so keep the 128x64 NONAME constructor and expose the build
// flag as a diagnostic alias.
using OledDisplayDriver = U8G2_SSD1306_128X64_NONAME_F_SW_I2C;
static const char* OledDriverTypeName = "SSD1306_64_NONAME";
#else
using OledDisplayDriver = U8G2_SSD1306_128X64_NONAME_F_SW_I2C;
static const char* OledDriverTypeName = "SSD1306_NONAME";
#endif

static std::unique_ptr<OledDisplayDriver> u8g2;
static uint32_t lastInvertTestMs = 0;

static void enableOledVext() {
  Serial.println("OLED VEXT enable...");
#if OLED_VEXT >= 0
  Serial.printf("OLED VEXT pin=%d level=%d\n", OLED_VEXT, OLED_VEXT_ON_LEVEL);
  pinMode(OLED_VEXT, OUTPUT);
  digitalWrite(OLED_VEXT, OLED_VEXT_ON_LEVEL);
  delay(300);
#else
  Serial.println("OLED VEXT skipped (OLED_VEXT < 0)");
#endif
}

static void resetOled() {
  Serial.println("OLED reset...");
#if OLED_RST >= 0
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(150);
#else
  Serial.println("OLED reset skipped (OLED_RST < 0)");
#endif
}

static void beginOledWire() {
  Serial.println("I2C begin...");
  Serial.printf("I2C pins SDA=%d SCL=%d clock=%lu timeout=%u\n", OLED_SDA, OLED_SCL,
                static_cast<unsigned long>(OledI2cClockHz), OledI2cTimeoutMs);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(OledI2cClockHz);
  Wire.setTimeOut(OledI2cTimeoutMs);
}

static bool findOledAddress(uint8_t& foundAddress) {
  for (uint8_t i = 0; i < sizeof(OledAddressCandidates); ++i) {
    const uint8_t address = OledAddressCandidates[i];
    Serial.printf("Checking OLED I2C addr 0x%02X...\n", address);
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    Serial.printf("OLED I2C 0x%02X result=%u\n", address, result);
    if (result == 0) {
      foundAddress = address;
      Serial.printf("OLED found at 0x%02X\n", foundAddress);
      return true;
    }
  }

  Serial.println("OLED not found, U8g2 init skipped");
  return false;
}

static bool createU8g2(uint8_t address) {
  Serial.println("U8g2 create begin");
  Serial.printf("OLED driver type: %s\n", OledDriverTypeName);
  // U8g2 SW I2C argument order is rotation, clock, data, reset.
  u8g2.reset(new OledDisplayDriver(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST));
  Serial.println("U8g2 create returned");

  if (u8g2 == nullptr) {
    Serial.println("U8g2 create failed");
    return false;
  }

  u8g2->setI2CAddress(address << 1);
  Serial.printf("U8g2 I2C address set to 0x%02X\n", address << 1);
  return true;
}

static void prepareVisibleOled() {
  Serial.println("U8g2 setPowerSave 0");
  u8g2->setPowerSave(0);
  Serial.println("U8g2 setContrast 255");
  u8g2->setContrast(255);
}

static void drawTestPattern() {
  prepareVisibleOled();
  Serial.println("U8g2 render test begin");
  u8g2->clearBuffer();
  u8g2->drawFrame(0, 0, 128, 64);
  u8g2->drawBox(0, 0, 20, 20);
  u8g2->setFont(u8g2_font_6x12_tf);
  u8g2->drawStr(24, 12, "ENDURO");
  u8g2->drawStr(24, 28, "START");
  u8g2->drawStr(24, 44, "OLED TEST");
  u8g2->drawLine(0, 63, 127, 0);
  u8g2->drawLine(0, 32, 127, 32);
  u8g2->drawLine(64, 0, 64, 63);
  Serial.println("U8g2 sendBuffer begin");
  u8g2->sendBuffer();
  Serial.println("U8g2 sendBuffer returned");
  Serial.println("OLED test pattern displayed");
}

bool OledDisplay::begin() {
  initialized_ = false;
  address_ = 0;
  lastInvertTestMs = 0;

#if !ENABLE_OLED
  Serial.println("OLED init skipped (ENABLE_OLED=0)");
  return false;
#else
  Serial.println("OLED init begin");
  Serial.printf("OLED config SDA=%d SCL=%d RST=%d VEXT=%d VEXT_ON_LEVEL=%d SCAN_ONLY=%d DRAW_TEST_ONLY=%d TEST_PATTERN_ONLY=%d INVERT_TEST=%d\n",
                OLED_SDA, OLED_SCL, OLED_RST, OLED_VEXT, OLED_VEXT_ON_LEVEL, OLED_SCAN_ONLY,
                OLED_DRAW_TEST_ONLY, OLED_TEST_PATTERN_ONLY, OLED_INVERT_TEST);

  enableOledVext();
  resetOled();
  beginOledWire();

  uint8_t foundAddress = 0;
  if (!findOledAddress(foundAddress)) {
    return false;
  }
  address_ = foundAddress;

#if OLED_SCAN_ONLY
  Serial.println("OLED scan-only mode, U8g2 init skipped");
  Serial.println("OLED OK");
  return true;
#else
  if (!createU8g2(address_)) {
    return false;
  }

#if OLED_DRAW_TEST_ONLY
  Serial.println("OLED draw-test-only mode, U8g2 begin/sendBuffer skipped");
  Serial.println("OLED OK");
  return true;
#else
  Serial.println("U8g2 begin...");
  initialized_ = u8g2->begin();
  Serial.println("U8g2 begin returned");

  if (!initialized_) {
    Serial.println("U8g2 begin failed");
    return false;
  }

  drawTestPattern();
  Serial.println("OLED OK");
  return true;
#endif
#endif
#endif
}

void OledDisplay::update() {
  if (!initialized_ || u8g2 == nullptr) return;

#if OLED_INVERT_TEST
  const uint32_t now = millis();
  if (now - lastInvertTestMs >= 1000UL) {
    Serial.println("OLED invert/display power test tick");
    prepareVisibleOled();
    lastInvertTestMs = now;
  }
#endif
}

bool OledDisplay::testPatternOnly() const {
#if OLED_TEST_PATTERN_ONLY
  return true;
#else
  return false;
#endif
}

void OledDisplay::showLines(const std::vector<String>& lines) {
  if (!initialized_ || u8g2 == nullptr || testPatternOnly()) return;

  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_6x10_tf);

  int y = 10;
  for (const String& line : lines) {
    const String visibleLine = line.length() > 0 ? line : String("-");
    u8g2->drawStr(0, y, visibleLine.c_str());
    y += 10;
    if (y > 64) break;
  }

  u8g2->sendBuffer();
}

void OledDisplay::showBoot(const String& role) {
  showBootScreen(role);
}

void OledDisplay::showBootScreen(const String& role) {
  if (testPatternOnly()) return;
  showLines({"ENDURO TIMER", role.length() > 0 ? role : String("START"), "READY", "UP: " + String(millis() / 1000UL) + "s"});
}

void OledDisplay::showStatus(const String& line1, const String& line2, const String& line3, const String& line4) {
  if (testPatternOnly()) return;

  const String visibleLine1 = line1.length() > 0 ? line1 : String("START");
  const String visibleLine2 = line2.length() > 0 ? line2 : String("READY");
  const String visibleLine3 = line3.length() > 0 ? line3 : String("UP: ") + String(millis() / 1000UL) + "s";
  const String visibleLine4 = line4.length() > 0 ? line4 : String("OLED OK");
  showLines({visibleLine1, visibleLine2, visibleLine3, visibleLine4});
}

void OledDisplay::showCountdown(const String& text) {
  if (!initialized_ || u8g2 == nullptr || testPatternOnly()) return;

  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_logisoso46_tf);
  const int16_t width = u8g2->getStrWidth(text.c_str());
  int16_t x = (128 - width) / 2;
  if (x < 0) x = 0;
  u8g2->drawStr(x, 56, text.c_str());
  u8g2->sendBuffer();
}

void OledDisplay::showResult(const String& result, const String& detail) {
  if (testPatternOnly()) return;
  showLines({"RESULT", result.length() > 0 ? result : String("-"), detail.length() > 0 ? detail : String("-")});
}
