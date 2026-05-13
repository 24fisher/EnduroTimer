# EnduroTimer Firmware Prototype

This folder contains the hardware-oriented EnduroTimer firmware for two **Heltec WiFi LoRa 32 V3 / ESP32-S3** boards.

The goal of this iteration is a smoke test for:

- a Wi-Fi Access Point and Web UI on the upper/start Heltec;
- LoRa 868 MHz communication between two Heltec boards;
- OLED and Serial boot diagnostics on both boards;
- a physical-button start on the StartStation;
- a physical-button finish simulation on the FinishStation.

This is intentionally **not** the final C# backend architecture. Missing hardware is represented by stubs so the two bare Heltec boards can be tested immediately.

## Project layout

```text
EnduroTimerFirmware/
├─ platformio.ini
├─ common/
│  ├─ display/       # Built-in OLED helper
│  ├─ hardware/      # Hardware stubs such as BuzzerStub
│  ├─ protocol/      # Shared LoRa JSON message model/protocol
│  └─ time/          # millis-based ClockService and formatting helpers
├─ start-station/
│  ├─ src/           # StartStation firmware and Web API
│  └─ data/          # LittleFS Web UI files
└─ finish-station/
   └─ src/           # FinishStation firmware and finish button stub
```

## Hardware target

- Heltec WiFi LoRa 32 V3
- ESP32-S3
- SX1262 LoRa radio
- Built-in 0.96 inch OLED
- LoRa frequency: **868 MHz**

The firmware uses Arduino framework for ESP32, RadioLib for SX1262, LittleFS for the StartStation Web UI, and U8g2 for the OLED.

Default built-in OLED pins are configured for Heltec WiFi LoRa 32 V3 and can be overridden from `platformio.ini` build flags. Heltec WiFi LoRa 32 V3 built-in OLED usually uses SDA GPIO17, SCL GPIO18, RST GPIO21, address 0x3C. VextCtrl is often GPIO36 and usually active LOW, but revisions/clones may differ.

| Setting | Default Heltec WiFi LoRa 32 V3 value | Notes |
| --- | --- | --- |
| `OLED_SDA` | `17` | I2C SDA for the built-in OLED. |
| `OLED_SCL` | `18` | I2C SCL for the built-in OLED. |
| `OLED_RST` | `21` | Set to `-1` to skip the OLED reset pulse during diagnostics. |
| `OLED_VEXT` | `36` | Set to `-1` only when firmware must not touch a VEXT control pin. |
| `OLED_VEXT_ON_LEVEL` | `0` | Usually active LOW on Heltec V3. |
| `OLED_SCAN_ONLY` | `0` | Diagnostic mode: probe I2C only, skip display initialization/drawing. |
| `OLED_DRAW_TEST_ONLY` | `0` | Temporary bypass: after a successful scan, skip display initialization/drawing and return success so the heartbeat can prove the main loop is alive. |
| OLED address | `0x3C` | The firmware also probes `0x3D`. |

When the selected ESP32 board package provides Heltec-style pin definitions such as `SDA_OLED`, `SCL_OLED`, `RST_OLED`, or `Vext`, the OLED helper uses those as fallback defaults before falling back to the values above. If `OLED_VEXT >= 0`, the firmware logs the VEXT pin and level, enables that GPIO before OLED probing, and waits for the rail to settle. Keep `OLED_VEXT=-1` if the VEXT control pin is unknown.

Default button pins use the board boot/user button as a temporary bare-board input:

- `START_BUTTON_PIN = 0`
- `FINISH_BUTTON_PIN = 0`

If your exact board revision exposes a different user/PRG button GPIO, override these values in `platformio.ini` build flags.

## Firmware roles

### StartStation

The StartStation firmware:

- prints boot diagnostics to Serial at 115200 baud;
- shows an OLED boot greeting and service screen;
- starts a Wi-Fi AP:
  - SSID: `EnduroTimer`
  - password: `endurotimer`
- serves the Web UI from LittleFS at `http://192.168.4.1`;
- exposes JSON API endpoints:
  - `GET /api/status`
  - `POST /api/time/sync`
  - `POST /api/runs/start`
  - `POST /api/system/reset`
  - `GET /api/runs`
- displays AP IP, LoRa, finish station online/offline, state, run, countdown, and result information on the OLED;
- starts a run from the physical button or from `POST /api/runs/start`;
- sends `RUN_START` to the FinishStation over LoRa;
- receives periodic `STATUS` heartbeats from the FinishStation and marks it offline if no heartbeat arrives for more than 6 seconds;
- receives `FINISH`, calculates the result, stores recent runs in RAM, and replies with `FINISH_ACK`.

### FinishStation

The FinishStation firmware:

- prints boot diagnostics to Serial at 115200 baud;
- shows an OLED boot greeting and service screen;
- does not start Wi-Fi or a web server;
- communicates only over LoRa;
- sends `STATUS` every 2 seconds;
- receives `RUN_START` and switches to `WAIT_FINISH`;
- waits for the physical finish button;
- sends `FINISH` with `source = BUTTON_STUB`;
- repeats `FINISH` once per second up to 5 attempts until `FINISH_ACK` is received;
- returns to `IDLE` after `FINISH_ACK`.

## Build and upload

Install PlatformIO first, then run commands from this directory or pass `-d EnduroTimerFirmware` from the repository root.

### StartStation

Build:

```bash
pio run -e start_station
```

Upload firmware:

```bash
pio run -e start_station -t upload
```

Upload Web UI filesystem:

```bash
pio run -e start_station -t uploadfs
```

### FinishStation

Build:

```bash
pio run -e finish_station
```

Upload firmware:

```bash
pio run -e finish_station -t upload
```

## Hardware smoke test with two bare Heltec boards

1. Build and flash StartStation:
   ```bash
   pio run -e start_station -t upload
   pio run -e start_station -t uploadfs
   ```
2. Build and flash FinishStation:
   ```bash
   pio run -e finish_station -t upload
   ```
3. Open the serial monitor for each board one at a time at 115200 baud.
4. Check that each OLED shows the boot greeting and then the service screen.
5. Connect to Wi-Fi:
   - SSID: `EnduroTimer`
   - password: `endurotimer`
   - URL: `http://192.168.4.1`
6. Confirm that the Web UI reports the FinishStation as online.
7. Press the physical button on the upper Heltec or press **Start test run** in the Web UI.
8. Wait for `RUN_START` / `WAIT_FINISH` on the lower OLED.
9. Press the physical button on the lower Heltec.
10. Confirm that the result appears on the upper OLED and in the Web UI.

## Diagnostic boot mode

The default PlatformIO build flags intentionally keep LoRa, Wi-Fi, and the Web UI disabled while OLED diagnostics run through U8g2:

```ini
-D ENABLE_OLED=1
-D ENABLE_LORA=0
-D ENABLE_WIFI=0
-D ENABLE_WEB=0
-D OLED_SDA=17
-D OLED_SCL=18
-D OLED_RST=21
-D OLED_VEXT=36
-D OLED_VEXT_ON_LEVEL=0
-D OLED_SCAN_ONLY=0
-D OLED_DRAW_TEST_ONLY=0
-D ARDUINO_USB_CDC_ON_BOOT=0
-D ARDUINO_USB_MODE=0
```

With those flags, reset the StartStation board and confirm the serial monitor shows the application entry banner, the explicit VEXT enable or skip log line, the OLED I2C address checks, and the one-second heartbeat before enabling more hardware modules:

```text
ENDURO TIMER START STATION APP ENTRY
Build: ...
Serial OK
APP alive ms=...
```

After the U8g2 OLED path and serial heartbeat are confirmed, enable modules one at a time in this order: `ENABLE_WIFI`, `ENABLE_WEB`, then `ENABLE_LORA`. Each enabled initializer logs an `init...` line followed by `OK` or `FAIL`; failures are reported to Serial and the firmware keeps running so the heartbeat can continue.

## Troubleshooting

### Wi-Fi is not visible

- Check StartStation Serial logs.
- Confirm that the board was flashed with `start_station`, not `finish_station`.
- Confirm that `WiFi.softAP` returned `true` in the Serial log.
- Check USB power and try another cable or port.

### Heltec V3 OLED diagnostics

The built-in OLED now uses **U8g2** with the `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` driver. For Heltec WiFi LoRa 32 V3 / ESP32-S3 the expected built-in OLED wiring is:

- `OLED_SDA=17`
- `OLED_SCL=18`
- `OLED_RST=21`
- `OLED_VEXT=36`
- `OLED_VEXT_ON_LEVEL=0` (VEXT active LOW)
- OLED I2C address `0x3C` (the firmware also checks `0x3D`)

The OLED boot path is intentionally serial-first and step-by-step. A healthy StartStation OLED test should show these serial checkpoints before the normal heartbeat continues:

```text
OLED init begin
OLED VEXT enable...
OLED reset...
I2C begin...
Checking OLED I2C addr 0x3C...
OLED found at 0x3C
U8g2 create begin
U8g2 create returned
U8g2 begin...
U8g2 begin returned
U8g2 splash draw begin
U8g2 splash draw returned
OLED OK
APP alive ms=...
```

If the display or driver blocks, use only the bypass flags and the last printed serial line to isolate the step:

1. Set `ENABLE_OLED=0` to avoid touching the OLED path at all and confirm the heartbeat continues.
2. Set `ENABLE_OLED=1` and `OLED_SCAN_ONLY=1` to enable VEXT, reset the panel, start I2C, and check only `0x3C`/`0x3D` without creating U8g2.
3. Set `OLED_SCAN_ONLY=0` and `OLED_DRAW_TEST_ONLY=1` to create the U8g2 object but skip `begin()` and `sendBuffer()`.
4. Set `OLED_SCAN_ONLY=0` and `OLED_DRAW_TEST_ONLY=0` for the full U8g2 initialization and splash draw.

The OLED path never restarts the ESP, aborts, or enters a retry loop. If the OLED is not found, initialization returns `false`, logs `OLED not found, U8g2 init skipped`, and the main loop heartbeat continues.

Serial result codes from `Wire.endTransmission()` are the key signal:

- `result=0` means an I2C device acknowledged the address.
- `result=2` means NACK on the address.
- `result=5` means timeout or another bus-level error.

Confirm that PlatformIO installs `olikraus/U8g2` from `platformio.ini` and that no older OLED-only driver is used by `OledDisplay`.

### LoRa is offline

- Both boards must use 868 MHz.
- Both boards should have antennas connected.
- The two boards must run different firmware roles: one `start_station`, one `finish_station`.
- Check Serial logs for `LoRa: init failed` and radio TX errors.

### Button does not work

- Check `START_BUTTON_PIN` and `FINISH_BUTTON_PIN`.
- Watch Serial for `START BUTTON pressed` or `FINISH BUTTON pressed`.
- Some board revisions may require changing the GPIO in `platformio.ini`.
- Buttons are configured as `INPUT_PULLUP`; pressed state is `LOW`.


### Serial monitor is empty after flashing

- Keep the serial monitor open at `115200` baud and press the board **Reset/EN** button to replay the boot log.
- Confirm that the expected PlatformIO environment was flashed: `start_station` for the StartStation board or `finish_station` for the FinishStation board.
- Confirm that `monitor_speed = 115200` is set in `platformio.ini`.
- Confirm that `Serial.begin(115200)` is the first action in each firmware `setup()` before OLED, LoRa, Wi-Fi, WebServer, or other services are initialized.
- Check the USB cable, selected COM port, and whether the cable supports data rather than charge-only power.

### PlatformIO warns that `data_dir` is ignored

- `data_dir` must be configured in the top-level `[platformio]` section, not inside an `[env:*]` section.
- The StartStation Web UI filesystem uses `data_dir = start-station/data` and `board_build.filesystem = littlefs`.
- After flashing StartStation firmware, upload the Web UI filesystem:
  ```bash
  pio run -e start_station -t uploadfs
  ```

## Current stubs and intentional limitations

- Finish sensor: physical finish button stub only, no E3JK GPIO implementation yet.
- Buzzer: `BuzzerStub`, serial log only, no GPIO writes.
- Encoder: not implemented in this iteration.
- RFID: not implemented in this iteration.
- RTC: not implemented yet; `ClockService` currently uses `millis()`.
- Finish timestamp is approximated as `startTimestampMs + (millis() - localRunStartReceivedMillis)`, which is sufficient for this two-board smoke test without RTC.
- Runs are stored in RAM only.
- PDF, Excel, SQLite, Entity Framework, and the full rider/trail/group queue logic are intentionally out of scope for this hardware smoke test.

## Notes for future iterations

The current file structure leaves room for adding:

- DS3231 RTC synchronization on both stations;
- real E3JK finish sensor GPIO implementation;
- real buzzer output;
- encoder and local menus;
- RFID rider selection;
- persistent LittleFS files such as `riders.json`, `trails.json`, `settings.json`, `group_queue.json`, and `runs.jsonl` or `runs.csv`.
