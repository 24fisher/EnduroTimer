# EnduroTimer Firmware Prototype

This folder contains the first hardware-oriented EnduroTimer iteration for two **Heltec WiFi LoRa 32 V3 / ESP32-S3** boards.

The goal of this iteration is a smoke test for:

- a Wi-Fi Access Point and Web UI on the upper/start Heltec;
- LoRa 868 MHz communication between two Heltec boards;
- a simulated finish event from the lower/finish Heltec 20 seconds after `RUN_START`.

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
   └─ src/           # FinishStation firmware and simulated finish sensor
```

## Hardware target

- Heltec WiFi LoRa 32 V3
- ESP32-S3
- SX1262 LoRa radio
- Built-in 0.96 inch OLED
- LoRa frequency: **868 MHz**

The firmware uses Arduino framework for ESP32, RadioLib for SX1262, LittleFS for the StartStation Web UI, and U8g2 for the OLED.

## Firmware roles

### StartStation

The StartStation firmware:

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
- displays AP, LoRa, finish station, state, run, countdown, and result information on the OLED;
- sends `RUN_START` to the FinishStation over LoRa;
- receives `FINISH`, calculates the result, stores recent runs in RAM, and replies with `FINISH_ACK`.

### FinishStation

The FinishStation firmware:

- does not start Wi-Fi or a web server;
- communicates only over LoRa;
- shows service status on the OLED;
- sends `STATUS` every 3 seconds;
- receives `RUN_START`;
- uses `FinishSensorStub` to simulate finish 20 seconds after `RUN_START`;
- sends `FINISH` with `source = SIMULATED_SENSOR_20S`;
- repeats `FINISH` once per second up to 5 attempts until `FINISH_ACK` is received.

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

## Test procedure

1. Flash the StartStation firmware to the first Heltec.
2. Upload the StartStation LittleFS filesystem.
3. Flash the FinishStation firmware to the second Heltec.
4. Power on both boards.
5. Connect a phone or computer to Wi-Fi:
   - SSID: `EnduroTimer`
   - password: `endurotimer`
6. Open `http://192.168.4.1`.
7. Wait until the Web UI reports the FinishStation as online.
8. Press **Start test run**.
9. The StartStation shows countdown `3`, `2`, `1`, `GO` and sends `RUN_START` over LoRa.
10. After 20 seconds the FinishStation simulates the finish sensor and sends `FINISH`.
11. The Web UI and StartStation OLED should show a result close to `00:20.000`.

## Current stubs and intentional limitations

- Finish sensor: `FinishSensorStub`, no GPIO reads.
- Buzzer: `BuzzerStub`, serial log only, no GPIO writes.
- Encoder: not implemented in this iteration.
- RFID: not implemented in this iteration.
- RTC: not implemented yet; `ClockService` currently uses `millis()`.
- Finish timestamp is calculated as `startTimestampMs + 20000`, so the smoke test result does not depend on clock drift between the two ESP32 boards.
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
