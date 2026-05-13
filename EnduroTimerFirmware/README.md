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

Default built-in OLED pins are configured for Heltec WiFi LoRa 32 V3 and can be overridden from `platformio.ini` build flags:

- `OLED_SDA = 17`
- `OLED_SCL = 18`
- `OLED_RST = 21`
- `OLED_VEXT = 36` in the current diagnostic configuration. Set it to `-1` only when the VEXT control pin is unknown and firmware must not touch VEXT.
- `OLED_VEXT_ON_LEVEL = 0` by default, matching the Heltec active-low VEXT enable line.
- `OLED_SCAN_ONLY = 1` in the current diagnostic configuration; this performs an I2C-only OLED smoke test that skips display initialization and drawing.
- OLED probing checks only I2C addresses `0x3C` and `0x3D`.

When the selected ESP32 board package provides Heltec-style pin definitions such as `SDA_OLED`, `SCL_OLED`, `RST_OLED`, or `Vext`, the OLED helper uses those as fallback defaults before falling back to the values above. If `OLED_VEXT >= 0`, the firmware logs the VEXT pin and level, enables that GPIO before OLED probing, waits for the rail to settle, and logs a readback value. Keep `OLED_VEXT=-1` if the VEXT control pin is unknown.

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

The default PlatformIO build flags intentionally keep the Heltec startup path minimal for first-pass diagnostics:

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
-D OLED_SCAN_ONLY=1
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

After the OLED scan-only serial heartbeat is confirmed, disable `OLED_SCAN_ONLY` and then enable modules one at a time in this order: `ENABLE_WIFI`, `ENABLE_WEB`, then `ENABLE_LORA`. Each enabled initializer logs an `init...` line followed by `OK` or `FAIL`; failures are reported to Serial and the firmware keeps running so the heartbeat can continue.

## Troubleshooting

### Wi-Fi is not visible

- Check StartStation Serial logs.
- Confirm that the board was flashed with `start_station`, not `finish_station`.
- Confirm that `WiFi.softAP` returned `true` in the Serial log.
- Check USB power and try another cable or port.

### OLED is blank or boot stops at OLED init

- If the Serial log stops after `OLED init...`, first build with `ENABLE_OLED=0` and confirm the one-second `APP alive ms=...` heartbeat continues.
- Next build with `ENABLE_OLED=1` and `OLED_SCAN_ONLY=1`; this runs only `Wire.begin(...)` plus an I2C scan and does not call `display.begin()` or draw to the OLED.
- Every OLED boot attempt logs either `OLED VEXT enable: pin=... level=...` or `OLED VEXT skipped: OLED_VEXT < 0` immediately after `OLED init begin`.
- A healthy built-in display should normally return `OLED I2C 0x3C result=0` or `OLED I2C 0x3D result=0` between `I2C scan start` and `I2C scan done`.
- If the scan prints `No OLED I2C device found at 0x3C or 0x3D`, verify `OLED_SDA`, `OLED_SCL`, `OLED_RST`, `OLED_VEXT`, and `OLED_VEXT_ON_LEVEL` for the exact Heltec WiFi LoRa 32 V3 board revision.
- Keep `OLED_VEXT=-1` unless you are sure which GPIO controls VEXT on your board; do not call `pinMode` on an unknown VEXT pin.
- If your board requires VEXT power control, set `OLED_VEXT` and `OLED_VEXT_ON_LEVEL` explicitly in `platformio.ini` and retest with `OLED_SCAN_ONLY=1` before enabling full OLED initialization.
- Confirm that U8g2 is installed by PlatformIO.
- The firmware returns `OLED FAIL` and continues to Wi-Fi, LoRa, state initialization, and heartbeat logging when OLED probing fails. In scan-only mode, it returns `OLED OK` only when `0x3C` or `0x3D` responds and still skips display drawing.

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
