#include "OledDisplay.h"

#include <U8g2lib.h>
#include <Wire.h>

#include <memory>

// Heltec WiFi LoRa 32 V3 built-in OLED usually uses SDA GPIO17, SCL GPIO18,
// RST GPIO21, address 0x3C. VextCtrl is often GPIO36 and usually active LOW,
// but revisions/clones may differ.
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

#ifndef OLED_SCAN_ONLY
#define OLED_SCAN_ONLY 1
#endif

#ifndef OLED_DRAW_TEST_ONLY
#define OLED_DRAW_TEST_ONLY 0
#endif

#ifndef OLED_VEXT_ON_LEVEL
#define OLED_VEXT_ON_LEVEL 0
#endif

#ifndef OLED_FULL_I2C_SCAN
#define OLED_FULL_I2C_SCAN 0
#endif

#ifndef OLED_TRY_BOTH_VEXT_LEVELS
#define OLED_TRY_BOTH_VEXT_LEVELS 0
#endif

static constexpr uint32_t OledI2cClockHz = 100000UL;
static constexpr uint16_t OledI2cTimeoutMs = 50;
static const uint8_t OLED_ADDR_CANDIDATES[] = {
  0x3C,
  0x3D
};

using OledDisplayDriver = U8G2_SSD1306_128X64_NONAME_F_HW_I2C;
static std::unique_ptr<OledDisplayDriver> display;

static OledDisplayDriver& ensureDisplayDriver() {
  if (!display) {
    Serial.println("OLED display driver create begin");
    display.reset(new OledDisplayDriver(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA));
    Serial.println("OLED display driver create returned");
  }
  return *display;
}

static void enableOledVext(int level) {
#if OLED_VEXT >= 0
  Serial.printf("OLED VEXT enable: pin=%d level=%d\n", OLED_VEXT, level);
  pinMode(OLED_VEXT, OUTPUT);
  digitalWrite(OLED_VEXT, level);
  delay(300);
  Serial.printf("OLED VEXT enabled, readback=%d\n", digitalRead(OLED_VEXT));
#else
  (void)level;
  Serial.println("OLED VEXT skipped: OLED_VEXT < 0");
#endif
}

static void resetOled() {
#if OLED_RST >= 0
  Serial.printf("OLED reset: pin=%d\n", OLED_RST);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(150);
  Serial.printf("OLED reset done, readback=%d\n", digitalRead(OLED_RST));
#else
  Serial.println("OLED reset skipped: OLED_RST < 0");
#endif
}

static void beginOledWire() {
  Serial.printf("I2C begin SDA=%d SCL=%d\n", OLED_SDA, OLED_SCL);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(OledI2cClockHz);
  Wire.setTimeOut(OledI2cTimeoutMs);
}

static void runFullI2cScan() {
#if OLED_FULL_I2C_SCAN
  Serial.println("Full I2C scan start: 0x08..0x77");
  Wire.setTimeOut(OledI2cTimeoutMs);
  bool anyFound = false;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    if (result == 0) {
      anyFound = true;
      Serial.printf("I2C device found at 0x%02X\n", address);
    }
    delay(1);
  }
  if (!anyFound) {
    Serial.println("No I2C devices found");
  }
  Serial.println("Full I2C scan done");
#endif
}

static bool checkOledI2cAddresses(uint8_t& foundAddress) {
  Serial.println("I2C OLED candidate scan start");

  bool found = false;
  for (uint8_t i = 0; i < sizeof(OLED_ADDR_CANDIDATES); ++i) {
    const uint8_t address = OLED_ADDR_CANDIDATES[i];
    Serial.printf("Checking OLED I2C addr 0x%02X...\n", address);
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    Serial.printf("OLED I2C 0x%02X result=%u\n", address, result);
    if (result == 0 && !found) {
      foundAddress = address;
      found = true;
    }
  }

  if (found) {
    Serial.printf("OLED found at 0x%02X\n", foundAddress);
  } else {
    Serial.println("No OLED I2C device found at 0x3C or 0x3D");
  }
  Serial.println("I2C OLED candidate scan done");
  return found;
}

static bool runOledProbeForVextLevel(int level, uint8_t& foundAddress) {
  Serial.printf("OLED VEXT test level=%d\n", level);
  enableOledVext(level);
  resetOled();
  beginOledWire();

  const bool oledAddressFound = checkOledI2cAddresses(foundAddress);
  runFullI2cScan();
  return oledAddressFound;
}

bool OledDisplay::begin() {
  initialized_ = false;
  address_ = 0;
  vextLevel_ = OLED_VEXT_ON_LEVEL;
  Serial.printf(
      "OLED init... SDA=%d SCL=%d RST=%d VEXT=%d VEXT_ON_LEVEL=%d SCAN_ONLY=%d DRAW_TEST_ONLY=%d FULL_I2C_SCAN=%d TRY_BOTH_VEXT_LEVELS=%d\n",
      OLED_SDA,
      OLED_SCL,
      OLED_RST,
      OLED_VEXT,
      OLED_VEXT_ON_LEVEL,
      OLED_SCAN_ONLY,
      OLED_DRAW_TEST_ONLY,
      OLED_FULL_I2C_SCAN,
      OLED_TRY_BOTH_VEXT_LEVELS);
  Serial.println("OLED init begin");

  uint8_t foundAddress = 0;
  bool oledAddressFound = runOledProbeForVextLevel(OLED_VEXT_ON_LEVEL, foundAddress);

#if OLED_TRY_BOTH_VEXT_LEVELS && OLED_VEXT >= 0
  if (!oledAddressFound) {
    const int alternateLevel = OLED_VEXT_ON_LEVEL ? 0 : 1;
    oledAddressFound = runOledProbeForVextLevel(alternateLevel, foundAddress);
    if (oledAddressFound) {
      vextLevel_ = alternateLevel;
      Serial.printf("OLED works with VEXT level=%d\n", alternateLevel);
    }
  } else {
    Serial.printf("OLED works with VEXT level=%d\n", OLED_VEXT_ON_LEVEL);
  }
#endif

  if (!oledAddressFound || foundAddress == 0) {
    Serial.println("OLED display init skipped: no address");
    Serial.println("OLED not found at 0x3C or 0x3D");
    Serial.println("OLED FAIL");
    return false;
  }

  address_ = foundAddress;

#if OLED_SCAN_ONLY
  Serial.println("OLED scan-only mode, display init skipped");
  Serial.println("OLED OK");
  return true;
#elif OLED_DRAW_TEST_ONLY
  Serial.println("OLED draw-test-only mode, display init skipped");
  Serial.println("OLED OK");
  return true;
#else
  Serial.println("OLED display init begin");
  yield();

  OledDisplayDriver& driver = ensureDisplayDriver();
  driver.setI2CAddress(address_ << 1);
  initialized_ = driver.begin();
  yield();
  Serial.printf("OLED display init returned result=%d\n", initialized_ ? 1 : 0);

  if (!initialized_) {
    Serial.println("Serial OLED display init failed");
    Serial.println("OLED FAIL");
    return false;
  }

  driver.setFont(u8g2_font_6x10_tf);
  driver.clearBuffer();
  driver.drawStr(0, 12, "ENDURO TIMER");
  driver.drawStr(0, 28, "START STATION");
  driver.drawStr(0, 44, "READY");
  driver.sendBuffer();
  Serial.println("OLED splash drawn");
  Serial.println("OLED OK");
  return true;
#endif
}

void OledDisplay::showLines(const std::vector<String>& lines) {
  if (!initialized_ || !display) return;

  OledDisplayDriver& driver = *display;
  driver.clearBuffer();
  driver.setFont(u8g2_font_6x10_tf);
  int y = 10;
  for (const String& line : lines) {
    driver.drawStr(0, y, line.c_str());
    y += 10;
    if (y > 64) break;
  }
  driver.sendBuffer();
}

void OledDisplay::showBoot(const String& role) {
  showLines({"ENDURO TIMER", role, "BOOTING..."});
}

void OledDisplay::showCountdown(const String& text) {
  if (!initialized_ || !display) return;

  OledDisplayDriver& driver = *display;
  driver.clearBuffer();
  driver.setFont(u8g2_font_logisoso46_tf);
  const int16_t width = driver.getStrWidth(text.c_str());
  int16_t x = (128 - width) / 2;
  if (x < 0) x = 0;
  driver.drawStr(x, 56, text.c_str());
  driver.sendBuffer();
}
