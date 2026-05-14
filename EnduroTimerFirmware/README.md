# EnduroTimer Firmware Smoke Test

This folder contains the PlatformIO firmware for a two-board **Heltec WiFi LoRa 32 V3 / ESP32-S3** smoke test.

The firmware keeps StartStation and FinishStation as separate applications:

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

The smoke test still does **not** connect a real E3JK sensor, buzzer GPIO, encoder, or RFID reader. Those parts remain stubs.

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

## Current hardware-test behavior

- A run starts **only from the physical StartStation button**. The Web UI no longer starts runs.
- `ENABLE_WEB_START=0` is set for StartStation. `POST /api/runs/start` remains present for future debug, but returns HTTP 403 by default with `Start is only available from hardware button`.
- FinishStation completes a run with the physical finish button while the lower terminal is in the riding/wait-for-finish state.
- Buttons use debounced short press events with `INPUT_PULLUP`; one press creates one start or finish event.
- Countdown, timers, finish retries, OLED refresh, WebServer handling, and LoRa polling are all millis-based and should not block the main loop.
- Runtime init failures for OLED, Wi-Fi, LittleFS, Web, or LoRa are logged and exposed in `/api/status`; they do not stop the loop or restart the ESP32.
- Serial diagnostics and one-second `START alive` / `FINISH alive` heartbeats are intentionally kept.

## FinishStation online status and LoRa signal

FinishStation sends `STATUS` every 1000 ms in all states: `Idle`, externally displayed `Riding`, `FinishSent`, and `Error` / ACK timeout. StartStation also sends a `STATUS` heartbeat with `stationId: "start"` every 1000 ms, so FinishStation can show StartStation signal even while idle.

Signal display rules:

- `NO SIGNAL` means no valid packet from the other station has been received for more than 10 seconds.
- `FIN:-72dBm` means StartStation is currently hearing packets from FinishStation.
- `START:-33dBm` means FinishStation is currently hearing packets from StartStation.
- StartStation updates the FinishStation signal from any valid `stationId: "finish"` packet, including both `STATUS` and `FINISH`.
- FinishStation updates the StartStation signal from any valid `stationId: "start"` packet, including `STATUS`, `RUN_START`, and `FINISH_ACK`.
- Every received LoRa packet logs `LORA RX type=... rssi=... snr=... raw=...`; JSON parse failures, missing `stationId`, and unknown `type` are logged explicitly.
- FinishStation includes its reported StartStation RSSI/SNR and `startLastSeenAgoMs` in `STATUS`, so StartStation can show both directions in `/api/status`.

## Riders, trails, settings, and results

StartStation stores simple LittleFS data files:

- `/riders.json` — rider list, created automatically when missing. Riders are added with `POST /api/riders/add`.
- `/trails.json` — trail list, created automatically when missing. Trails are added with `POST /api/trails/add`.
- `/settings.json` — selected rider/trail.
- `/runs.csv` — finished run export with UTF-8 BOM and semicolon-separated columns.

Run selection rules for the physical start button:

1. Use `selectedRiderId` if it points to an active rider.
2. Otherwise use the first active rider.
3. If no active rider exists, use `Test Rider`.
4. Use `selectedTrailId` if it points to an active trail.
5. Otherwise use the first active trail.
6. If no trail exists, create/use `Default trail`.

Every finished run stores:

- `runId`
- `riderId`
- `riderNameSnapshot`
- `trailId`
- `trailNameSnapshot`
- start/finish/result timestamps
- status and finish source

CSV columns:

```text
RunId;Rider;Trail;StartMs;FinishMs;ResultMs;Result;Status;Source
```

Export endpoint:

- `GET /api/export/runs.csv`
- Content type: `text/csv; charset=utf-8`
- Download filename: `enduro_runs.csv`

## Build environments

### StartStation

`start_station` enables:

- OLED via U8g2
- Wi-Fi Access Point only (`WIFI_AP` + `WiFi.softAP`, no STA mode)
- Web UI and JSON API
- LoRa at 868.0 MHz
- Physical start button on `START_BUTTON_PIN=0` with `INPUT_PULLUP`
- `ENABLE_WEB_START=0`
- `BuzzerStub` only

Flash firmware and upload the Web UI filesystem:

```bash
pio run -e start_station -t upload
pio run -e start_station -t uploadfs
```

### FinishStation

`finish_station` enables:

- OLED via U8g2
- LoRa at 868.0 MHz
- Physical finish button on `FINISH_BUTTON_PIN=0` with `INPUT_PULLUP`
- `BuzzerStub` only
- No Wi-Fi and no Web server

Flash firmware:

```bash
pio run -e finish_station -t upload
```

## StartStation JSON API

### `GET /api/status`

Returns current status, for example:

```json
{
  "device": "StartStation",
  "state": "Riding",
  "oledOk": true,
  "wifiOk": true,
  "webOk": true,
  "loraOk": true,
  "finishStationOnline": true,
  "finishSignalText": "-72 dBm",
  "finishState": "Riding",
  "finishLastSeenAgoMs": 1200,
  "finishHeartbeatCount": 42,
  "finishRssi": -72,
  "finishSnr": 8.5,
  "finishReportedStartSignalText": "-69 dBm",
  "finishReportedStartRssi": -69,
  "finishReportedStartSnr": 9.2,
  "currentRunId": "RUN-...",
  "currentRiderName": "Test Rider",
  "currentTrailName": "Default trail",
  "selectedTrailId": "t001",
  "selectedTrailName": "Default trail",
  "lastLoRaPacketType": "STATUS",
  "lastLoRaRawShort": "{...}",
  "currentRunElapsedMs": 12345,
  "currentRunElapsedFormatted": "00:12",
  "ridingAnimationFrame": 3,
  "countdownText": "",
  "lastResultMs": 20123,
  "lastResultFormatted": "00:20.123",
  "lastFinishSource": "BUTTON_STUB",
  "uptimeMs": 123456,
  "heap": 123456,
  "minHeap": 123456
}
```

### Other endpoints

- `POST /api/runs/start` — disabled by default with HTTP 403 unless `ENABLE_WEB_START=1` is compiled.
- `POST /api/system/reset` — clears the active run and returns StartStation to `Ready`.
- `GET /api/runs` — recent finished runs in RAM, populated immediately after a valid `FINISH` message.
- `GET /api/export/runs.csv` — finished run CSV download from `/runs.csv`.
- `GET /api/riders` — rider list.
- `POST /api/riders/add` — JSON body `{ "displayName": "..." }`.
- `POST /api/riders/deactivate` — JSON body `{ "riderId": "..." }`.
- `GET /api/trails` — trail list.
- `POST /api/trails/add` — JSON body `{ "displayName": "..." }`.
- `POST /api/trails/deactivate` — JSON body `{ "trailId": "..." }`.
- `GET /api/settings` — selected rider/trail.
- `POST /api/settings` — JSON body `{ "selectedRiderId": "...", "selectedTrailId": "..." }`.

## Web UI

The LittleFS Web UI shows:

- StartStation service flags and state.
- FinishStation signal, state, heartbeat, and last seen age. Missing radio packets are shown as `NO SIGNAL`, not `OFF`.
- Signal from FinishStation (`Signal from finish: -72 dBm`) and signal reported by FinishStation from StartStation (`Signal reported by finish from start: -33 dBm`).
- Last LoRa packet type.
- Countdown and current run timer.
- Current rider and current trail.
- Visible riders and trails sections, including select, add, deactivate, and per-section error messages. Empty rider/trail add forms show `Введите имя райдера` or `Введите название трассы`.
- Reset system button.
- CSV download link.
- Recent results table with Rider, Trail, Result, Status, Source, and Run ID.

The UI polls `/api/status` once per second, `/api/runs` every two seconds, and rider/trail/settings catalogs on load and after catalog changes. If one endpoint fails, the matching section shows an error while the rest of the UI stays visible. It does not show a hard connection error for a single failed status fetch; after more than five consecutive failures it shows `Нет связи с верхним терминалом`.

## LoRa protocol

`RUN_START` from StartStation includes the selected rider and trail snapshots:

```json
{
  "type": "RUN_START",
  "messageId": "...",
  "stationId": "start",
  "runId": "...",
  "riderName": "Test Rider",
  "trailName": "Default trail",
  "startTimestampMs": 123456789
}
```

FinishStation `STATUS` includes state, active run, timer, button readiness, and reverse signal diagnostics:

```json
{
  "type": "STATUS",
  "stationId": "finish",
  "state": "Riding",
  "uptimeMs": 123456,
  "heartbeat": 42,
  "activeRunId": "RUN-...",
  "riderName": "Test Rider",
  "elapsedMs": 12345,
  "startRssi": -70,
  "startSnr": 8.5,
  "startLastSeenAgoMs": 250,
  "buttonReady": true
}
```

FinishStation sends and retries `FINISH` once per second until `FINISH_ACK` or 15 attempts. A short FinishStation button press in `FinishSent` immediately resends the same saved `FINISH`; a short press after ACK timeout resets attempts to 1 and resends the last `FINISH` without changing the saved finish timestamp:

```json
{
  "type": "FINISH",
  "messageId": "...",
  "stationId": "finish",
  "runId": "...",
  "finishTimestampMs": 123476789,
  "source": "BUTTON_STUB"
}
```

StartStation replies to a matching active run with `FINISH_ACK`. Duplicate `FINISH` packets for the already completed run receive another ACK without creating a second result.

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
5. Connect to Wi-Fi:
   - SSID: `EnduroTimer`
   - password: `endurotimer`
6. Open `http://192.168.4.1`.
7. Add/select a rider and trail if desired.
8. Wait until the Web UI shows Finish online.
9. Press the StartStation physical button.
10. FinishStation should show `RIDING`, the rider name, timer, and `Btn: FINISH`.
11. Press the FinishStation physical button.
12. FinishStation should show `FINISH SENT` and then `ACK OK` after acknowledgment.
13. StartStation should show `FINISHED` and append the result to `/runs.csv`.
14. Download results from `GET /api/export/runs.csv` or the Web UI CSV link.

## OLED screens

StartStation Ready:

```text
START TERMINAL
FIN:-72dBm
Rider: Test Rider
Trail: Default trail
Last: 00:20.1
```

StartStation Countdown uses immediate rendering on every countdown text change and is not throttled by the normal status screen refresh:

```text
START
3 / 2 / 1 / GO
```

The countdown state machine is millis-based and non-blocking: `3` for 1000 ms, `2` for 1000 ms, `1` for 1000 ms, and `GO` for about 700 ms. Serial logs include `COUNTDOWN step=... time=...` and `OLED countdown rendered step=... at ms=...`.

StartStation Riding includes a non-blocking moving `>` animation updated from `millis()` about every 250 ms:

```text
START TERMINAL
RIDING >
Rider: Test Rider
Time: 00:12
FIN:-72dBm
```

StartStation Finished:

```text
START TERMINAL
FINISHED
Test Rider
00:20.123
```

FinishStation Idle:

```text
FINISH TERMINAL
LoRa: OK
State: IDLE
START:-70dBm
```

FinishStation Riding also shows the moving `>` animation:

```text
FINISH TERMINAL
RIDING >
Rider: Test Rider
Time: 00:12
START:-70dBm
```

FinishStation finish-line overlay appears immediately after an accepted finish button press while retries and ACK handling continue in the background:

```text
FINISH TERMINAL
FINISH LINE
CROSSED
```

FinishStation FinishSent:

```text
FINISH TERMINAL
FINISH SENT
Sent: x/15
START:-70dBm
```

FinishStation ACK:

```text
FINISH TERMINAL
ACK OK
IDLE
```

OLED text is ASCII-safe. UTF-8 Cyrillic names remain unchanged in Web UI, JSON, and CSV, but OLED strings are transliterated and limited to short display widths; for example, `Лесной СУ 1` becomes `Lesnoy SU 1`.

## Troubleshooting

### No app logs after ROM boot

Keep `ARDUINO_USB_CDC_ON_BOOT=0` and `ARDUINO_USB_MODE=0` for CP210x COM-port serial output. Confirm `monitor_speed = 115200`, use a data-capable USB cable, and press Reset/EN with the monitor open.

### OLED is blank

Known working config:

```text
SDA=17 SCL=18 RST=21 VEXT=36 active LOW address=0x3C driver=SSD1306_NONAME U8g2
```

The OLED init path logs each step and does not restart or stop the loop on failure.

### Web page is not loading

Run:

```bash
pio run -e start_station -t uploadfs
```

If files are still missing, the fallback page at `http://192.168.4.1/` should appear and show the uploadfs command.

### FinishStation or StartStation is always `NO SIGNAL`

1. Check that `ENABLE_LORA=1` is present for both `start_station` and `finish_station` in `platformio.ini`.
2. Check that both boards use `LORA_FREQUENCY_MHZ=868.0`.
3. Watch FinishStation Serial for `STATUS sent heartbeat=5 state=...` every fifth heartbeat. STATUS is still sent every second.
4. Watch StartStation Serial for `LORA RX type=STATUS ...` and `STATUS received heartbeat=... state=... rssi=... snr=...`.
5. Watch FinishStation Serial for `START STATUS received heartbeat=... rssi=... snr=...`; this confirms StartStation heartbeat packets are reaching FinishStation in Idle.
6. If packet parsing fails, inspect `LORA parse failed raw=... error=...` on Serial.
7. Check that antennas are connected to both boards.

### Rider/trail Add button does nothing

1. Open the browser console and confirm `EnduroTimer UI loaded` appears. If it does not, check that `/app.js` is served from LittleFS and upload the filesystem again.
2. Press Add and check for `Adding rider ...` or `Adding trail ...` in the console.
3. Watch StartStation Serial for `HTTP POST /api/riders/add body=...` or `HTTP POST /api/trails/add body=...`.
4. Use `GET /api/debug/routes` to confirm the expected API routes are registered.
5. The Web UI now shows per-section success/error text in `ridersMessage` and `trailsMessage`.

### Finish button does not react

1. Check that `FINISH_BUTTON_PIN=0` is used for the temporary BOOT-button finish input.
2. Watch Serial for `button raw pressed transition` and `debounced short press` logs.
3. Watch the one-second FinishStation heartbeat fields `buttonRaw=...` and `buttonPressed=...`.
4. Confirm FinishStation state is externally shown as `Riding` (`WaitFinish` internally is accepted too). Finish presses in Idle are ignored and briefly show `NO ACTIVE RUN`.

### Countdown updates slowly

The StartStation countdown must use its immediate OLED render path, not only the regular status refresh. Serial should show `COUNTDOWN step=3/2/1/GO time=...` and `OLED countdown rendered step=... at ms=...` for each transition.

### Cyrillic looks wrong on OLED

The OLED uses ASCII transliteration/sanitization only for display. Web UI, JSON storage, and CSV export keep the original UTF-8 text.

### Finish-line message is missing

When the finish button is accepted on FinishStation, the OLED should immediately show:

```text
FINISH LINE
CROSSED
```

The overlay is held for about two seconds without blocking FINISH packet sends or ACK processing.

## Intentional limitations

Do not add these to the current smoke test:

- PDF/Excel export
- SQLite/EF or C# backend
- Real E3JK GPIO sensor
- Real buzzer GPIO
- Encoder
- RFID
- C# backend integration

Serial diagnostics and one-second heartbeats are intentionally kept on both roles.
