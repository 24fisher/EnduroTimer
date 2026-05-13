# EnduroTimer

EnduroTimer now contains two related prototypes:

1. **`EnduroTimerFirmware/`** — the current first hardware iteration for two Heltec WiFi LoRa 32 V3 / ESP32-S3 boards.
2. **`src/` and `tests/`** — the earlier .NET/Web UI logic prototype kept as a reference for future features.

The active target for this iteration is **PlatformIO + Arduino C++ firmware**, not the final C# backend.

## Current hardware iteration

See [`EnduroTimerFirmware/README.md`](EnduroTimerFirmware/README.md) for build, upload, and smoke-test instructions.

The firmware project provides two separate PlatformIO environments:

- `start_station` — upper/start station firmware:
  - Wi-Fi AP: `EnduroTimer` / `endurotimer`;
  - LittleFS Web UI at `http://192.168.4.1`;
  - JSON API for status, start, reset, and in-memory runs;
  - OLED status display;
  - LoRa `RUN_START` sender and `FINISH` receiver.
- `finish_station` — lower/finish station firmware:
  - LoRa-only operation;
  - OLED service display;
  - `STATUS` heartbeat every 3 seconds;
  - simulated finish sensor 20 seconds after `RUN_START`;
  - repeated `FINISH` until `FINISH_ACK`.

The first hardware smoke test requires only two bare Heltec WiFi LoRa 32 V3 boards. The E3JK finish sensor, buzzers, encoder, RFID, and RTC are intentionally stubbed or left for later iterations.

## Quick commands

Run these from `EnduroTimerFirmware/`.

Build StartStation:

```bash
pio run -e start_station
```

Upload StartStation firmware and Web UI filesystem:

```bash
pio run -e start_station -t upload
pio run -e start_station -t uploadfs
```

Build and upload FinishStation:

```bash
pio run -e finish_station
pio run -e finish_station -t upload
```

## Legacy .NET prototype

The previous .NET prototype remains in the repository as a logic and UI reference:

```text
EnduroTimer.sln
src/EnduroTimer.Core
src/EnduroTimer.Web
tests/EnduroTimer.Tests
```

Do not treat it as the target runtime for the current hardware iteration. It can still be useful when porting rider, trail, group queue, persistence, and reporting features in future firmware or companion-tool work.
