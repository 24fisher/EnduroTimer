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

Default built-in OLED pins are configured for Heltec WiFi LoRa 32 V3:

- `OLED_SDA = 17`
- `OLED_SCL = 18`
- `OLED_RST = 21`
- `VEXT_PIN = 36`
- OLED I2C address is handled by U8g2's SSD1306 driver.

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
-D ENABLE_OLED=0
-D ENABLE_LORA=0
-D ENABLE_WIFI=0
-D ENABLE_WEB=0
```

With those flags, reset the StartStation board and confirm the serial monitor shows the application entry banner and one-second heartbeat before enabling hardware modules:

```text
ENDURO TIMER START STATION APP ENTRY
Build: ...
Serial OK
APP alive ms=...
```

After the serial heartbeat is confirmed, enable modules one at a time in this order: `ENABLE_OLED`, `ENABLE_WIFI`, `ENABLE_WEB`, then `ENABLE_LORA`. Each enabled initializer logs an `init...` line followed by `OK` or `FAIL`; failures are reported to Serial and the firmware keeps running so the heartbeat can continue.

## Troubleshooting

### Wi-Fi is not visible

- Check StartStation Serial logs.
- Confirm that the board was flashed with `start_station`, not `finish_station`.
- Confirm that `WiFi.softAP` returned `true` in the Serial log.
- Check USB power and try another cable or port.

### OLED is blank

- Check Serial logs for `OLED: init failed`.
- Confirm that the firmware is using the Heltec WiFi LoRa 32 V3 OLED pins.
- Confirm that U8g2 is installed by PlatformIO.
- The firmware continues to run even if OLED initialization fails.

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
