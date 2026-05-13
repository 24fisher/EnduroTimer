# EnduroTimer Firmware Smoke Test

This folder contains the PlatformIO firmware for a two-board **Heltec WiFi LoRa 32 V3 / ESP32-S3** smoke test.

The firmware keeps the normal split PlatformIO structure:

```text
EnduroTimerFirmware/
├─ platformio.ini
├─ common/              # Shared OLED, LoRa protocol, time, and hardware stubs
├─ start-station/
│  ├─ src/              # StartStation app, state machine, Web API
│  └─ data/             # LittleFS Web UI files
└─ finish-station/
   └─ src/              # FinishStation app, state machine, finish button stub
```

StartStation and FinishStation are intentionally separate applications. The smoke test does not connect a real E3JK sensor, buzzer GPIO, encoder, or RFID reader yet; those parts remain stubs.

## Known working OLED and serial configuration

The current Heltec V3 OLED configuration is fixed in `platformio.ini`:

- `OLED_SDA=17`
- `OLED_SCL=18`
- `OLED_RST=21`
- `OLED_VEXT=36`
- `OLED_VEXT_ON_LEVEL=0` (active LOW)
- OLED address: `0x3C`
- Driver: `SSD1306_NONAME`
- Library: U8g2
- `ARDUINO_USB_CDC_ON_BOOT=0`
- `ARDUINO_USB_MODE=0`

`OLED_TEST_PATTERN_ONLY=0` for the full smoke-test mode so application screens can update after the U8g2 boot test.


## First hardware test fixes

The first field smoke test on two Heltec WiFi LoRa 32 V3 boards led to these firmware adjustments:

- Buttons now use debounced short press events, not long press. One press creates one start or finish request, and holding the BOOT/PRG button does not repeat the action.
- StartStation accepts the physical start button only in `Ready`; other states are logged as ignored.
- FinishStation accepts the physical finish button only in `WaitFinish`; `Idle` presses are logged as no active run, and `FinishSent` presses do not create duplicate `FINISH` messages.
- Power saving is disabled at boot. StartStation disables Wi-Fi sleep with `WiFi.setSleep(false)`, and both roles log `Power save: disabled`.
- FinishStation sends a `STATUS` heartbeat every 1 second with `stationId`, `state`, `uptimeMs`, `heartbeat`, `activeRunId`, and `buttonReady`.
- StartStation keeps FinishStation online for 5 seconds after the last `STATUS`, so one lost heartbeat does not immediately flip the Web UI to offline.
- `/api/status` now includes `finishLastSeenAgoMs`, `finishLastStatusMs`, `finishHeartbeatCount`, `loraLastRssi`, and `loraLastSnr` for easier LoRa diagnostics.
- The countdown is non-blocking and OLED should show `3`, `2`, `1`, and `GO` as separate visible steps before `RUN_START`.
- `FinishSent` is not an error. It means the lower station sent `FINISH` and is waiting for `FINISH_ACK`; it should be shown as a pending/normal state in the UI.
- Finish retry remains timer-driven: one `FINISH` per second, up to 5 attempts. Only an ACK timeout after the final attempt becomes an error.
- StartStation resends `FINISH_ACK` when it receives a duplicate `FINISH` for the already completed run, without creating a second result.
- StartStation OLED keeps the finished result visible for at least 5 seconds, then the normal status screen keeps `Last: mm:ss.mmm`.
- The Web UI polls less aggressively, uses `cache: no-store`, treats one or two fetch failures as a soft unstable-connection warning, and only shows `No response from station` after repeated failures. If `failed to fetch` appears, check for loop blocking, missing `webServer.handleClient()` calls, Wi-Fi AP stability, and LoRa/OLED calls that take too long.

## Build environments

### StartStation

`start_station` enables everything needed for the upper Heltec:

- OLED via U8g2
- Wi-Fi Access Point only (`WIFI_AP` + `WiFi.softAP`, no STA mode)
- Web UI and JSON API
- LoRa at 868.0 MHz
- Physical start button on `START_BUTTON_PIN=0` with `INPUT_PULLUP`
- `BuzzerStub` only

Flash firmware and upload the Web UI filesystem:

```bash
pio run -e start_station -t upload
pio run -e start_station -t uploadfs
```

The top-level PlatformIO setting is:

```ini
[platformio]
data_dir = start-station/data
```

`data_dir` must stay in `[platformio]`; PlatformIO ignores it under `[env:start_station]`.

### FinishStation

`finish_station` enables only the services needed for the lower Heltec:

- OLED via U8g2
- LoRa at 868.0 MHz
- Physical finish button on `FINISH_BUTTON_PIN=0` with `INPUT_PULLUP`
- `BuzzerStub` only
- No Wi-Fi and no Web server

Flash firmware:

```bash
pio run -e finish_station -t upload
```

## StartStation boot flow

The StartStation boot sequence is:

1. Serial first.
2. Reset diagnostics.
3. OLED init.
4. Start button init.
5. `BuzzerStub` init.
6. Wi-Fi AP init.
7. LittleFS init.
8. WebServer init.
9. LoRa init.
10. State Ready.

Expected boot log checkpoints include:

```text
[BOOT] OLED init...
[BOOT] OLED OK/FAIL
[BOOT] Button OK
[BOOT] WiFi AP init...
[BOOT] WiFi AP OK ssid=EnduroTimer ip=192.168.4.1
[BOOT] LittleFS init...
[BOOT] LittleFS OK/FAIL
[BOOT] WebServer init...
[BOOT] WebServer OK/FAIL
[BOOT] LoRa init...
[BOOT] LoRa OK/FAIL
[BOOT] State Ready
```

An OLED, Wi-Fi, LittleFS, Web, or LoRa init failure is not fatal. The loop continues, Serial heartbeat continues, and `/api/status` reports the relevant service flag as false.

## FinishStation boot flow

The FinishStation boot sequence is:

1. Serial first.
2. Reset diagnostics.
3. OLED init.
4. Finish button init.
5. `BuzzerStub` init.
6. LoRa init.
7. State Idle.

Expected boot log checkpoints include:

```text
[BOOT] OLED init...
[BOOT] OLED OK/FAIL
[BOOT] Finish button OK
[BOOT] LoRa init...
[BOOT] LoRa OK/FAIL
[BOOT] State Idle
```

Wi-Fi and Web are not started on FinishStation.

## Wi-Fi AP and Web UI

StartStation creates only an Access Point:

- SSID: `EnduroTimer`
- Password: `endurotimer`
- URL: `http://192.168.4.1`

Expected Wi-Fi log:

```text
WiFi AP starting...
WiFi AP OK
SSID: EnduroTimer
IP: 192.168.4.1
```

If `WiFi.softAP()` fails, Serial prints `WiFi AP FAIL`, the OLED shows Wi-Fi failure/offline information, and the loop continues.

The Web UI is served from LittleFS files in `start-station/data`:

- `index.html`
- `app.js`
- `style.css`

If the filesystem was not uploaded or `index.html` is missing, `GET /` returns a built-in fallback page that says:

- `EnduroTimer StartStation`
- `LittleFS web files not found`
- `Use: pio run -e start_station -t uploadfs`

## StartStation JSON API

### `GET /api/status`

Returns current smoke-test status, for example:

```json
{
  "device": "StartStation",
  "state": "Ready",
  "oledOk": true,
  "wifiOk": true,
  "webOk": true,
  "loraOk": true,
  "finishStationOnline": true,
  "finishState": "Idle",
  "loraLastRssi": -72,
  "loraLastSnr": 8.5,
  "finishLastSeenAgoMs": 1200,
  "finishLastStatusMs": 123000,
  "finishHeartbeatCount": 42,
  "currentRunId": "",
  "currentRiderName": "Test Rider",
  "countdownText": "",
  "lastResultMs": 20000,
  "lastResultFormatted": "00:20.000",
  "lastFinishSource": "BUTTON_STUB",
  "uptimeMs": 123456,
  "heap": 123456,
  "minHeap": 123456
}
```

### `POST /api/runs/start`

When state is `Ready`, starts the non-blocking countdown and quickly returns:

```json
{ "ok": true, "state": "Countdown" }
```

If a countdown or ride is already active, returns HTTP 409:

```json
{ "ok": false, "error": "Run already active" }
```

### `POST /api/system/reset`

Clears the active run and returns the StartStation to `Ready`. The last result may stay in RAM.

### `GET /api/runs`

Returns the recent run list stored in RAM.

## Web UI

The minimum Web UI shows:

- `EnduroTimer StartStation` heading
- StartStation state
- OLED OK/FAIL
- Wi-Fi OK/FAIL
- Web OK/FAIL
- LoRa OK/FAIL
- Finish online/offline
- Finish state
- RSSI/SNR
- Countdown
- Current run ID
- Last result
- `Start test run` button
- `Reset` button
- Notes: `Старт: кнопка на верхнем Heltec или кнопка в вебке.` and `Финиш: кнопка на нижнем Heltec.`
- Recent runs table

The UI polls `/api/status` every 1000 ms and handles short fetch outages without breaking the dashboard.

## LoRa protocol

The firmware uses the shared `RadioProtocol` / `RadioMessage` JSON protocol.

FinishStation sends `STATUS` every 1 second. StartStation marks FinishStation online when a status packet was received within the last 5 seconds.

StartStation sends `RUN_START` on GO:

```json
{
  "type": "RUN_START",
  "messageId": "...",
  "runId": "...",
  "riderName": "Test Rider",
  "startTimestampMs": 123456789
}
```

FinishStation sends and retries `FINISH` once per second until `FINISH_ACK` or 5 attempts:

```json
{
  "type": "FINISH",
  "messageId": "...",
  "runId": "...",
  "finishTimestampMs": 123476789,
  "source": "BUTTON_STUB"
}
```

StartStation replies to a matching active run with `FINISH_ACK`:

```json
{
  "type": "FINISH_ACK",
  "messageId": "...",
  "runId": "..."
}
```

Unknown `runId` FINISH packets are logged and ignored.

## Smoke-test flow

1. Flash StartStation:
   ```bash
   pio run -e start_station -t upload
   ```
2. Upload StartStation filesystem:
   ```bash
   pio run -e start_station -t uploadfs
   ```
3. Flash FinishStation:
   ```bash
   pio run -e finish_station -t upload
   ```
4. Open serial monitors for both boards.
5. Both OLEDs should show boot/status screens.
6. Connect to Wi-Fi:
   - SSID: `EnduroTimer`
   - password: `endurotimer`
7. Open `http://192.168.4.1`.
8. Wait until the Web UI shows Finish online.
9. Press the StartStation board button or click `Start test run` in the Web UI.
10. FinishStation should show `WAIT FINISH`.
11. Press the FinishStation board button.
12. StartStation should show the result on OLED.
13. Web UI should show the result and recent run row.

## OLED screens

StartStation screens:

```text
ENDURO TIMER
START
AP: 192.168.4.1
FIN: OK/OFF
LoRa: OK/OFF
READY
```

```text
COUNTDOWN
3 / 2 / 1 / GO
```

```text
RIDING
Run: short id
FIN: OK/OFF
```

```text
FINISHED
00:20.123
```

FinishStation screens:

```text
ENDURO TIMER
FINISH
LoRa: OK/OFF
IDLE
HB: counter
```

```text
WAIT FINISH
Run: short id
Btn: finish
```

```text
FINISH SENT
Run: short id
Sent: x/5
```

```text
ERROR
...
```

## Troubleshooting

### No app logs after ROM boot

Keep `ARDUINO_USB_CDC_ON_BOOT=0` and `ARDUINO_USB_MODE=0` for CP210x COM-port serial output. Confirm `monitor_speed = 115200`, use a data-capable USB cable, and press Reset/EN with the monitor open.

### OLED is blank

Known working config:

```text
SDA=17 SCL=18 RST=21 VEXT=36 active LOW address=0x3C driver=SSD1306_NONAME U8g2
```

The OLED init path logs each step and does not restart or stop the loop on failure.

### Wi-Fi AP is not visible

Check `ENABLE_WIFI=1` in `start_station`. `ENABLE_WEB` can be set to `0` for an AP-only test, but the full smoke test uses `ENABLE_WEB=1`.

### Web page is not loading

Run:

```bash
pio run -e start_station -t uploadfs
```

If files are still missing, the fallback page at `http://192.168.4.1/` should appear and show the uploadfs command.

### FinishStation is offline

- Check that the upper board was flashed with `start_station` and the lower board with `finish_station`.
- Check that LoRa is enabled on both environments.
- Check that both boards use `LORA_FREQUENCY_MHZ=868.0`.
- Attach antennas to both boards.
- Review Serial logs for LoRa begin result codes and TX errors.

### Unexpected reset

Check USB power, cable quality, and reset reason in Serial diagnostics.

## Intentional limitations

Do not add these to the current smoke test:

- PDF/Excel export
- SQLite/EF or C# backend
- Real E3JK GPIO sensor
- Real buzzer GPIO
- Encoder
- RFID

Serial diagnostics and one-second heartbeats are intentionally kept on both roles.
