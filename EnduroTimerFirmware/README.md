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
- `FIRMWARE_VERSION=0.08`
- `STATUS_LED_PIN=35`
- `STATUS_LED_ACTIVE_LEVEL=1`

`OLED_TEST_PATTERN_ONLY=0` for the full smoke-test mode so application screens can update after the U8g2 boot test.

## Current hardware-test behavior

- A run starts **only from the physical StartStation button**. The Web UI no longer starts runs.
- `ENABLE_WEB_START=0` is set for StartStation. `POST /api/runs/start` remains present for future debug, but returns HTTP 403 by default with `Start is only available from hardware button`.
- FinishStation completes a run with the physical finish button while the lower terminal is in the `Riding` state (`canFinish() == true`). Manual finish is accepted only in `Riding`; Idle presses show `NO ACTIVE RUN`, and FinishSent/AckTimeout presses resend the saved FINISH packet without changing the timestamp.
- Buttons use debounced short press events with `INPUT_PULLUP`; one press creates one start or finish event.
- Countdown, timers, finish retries, OLED refresh, WebServer handling, and LoRa polling are all millis-based and should not block the main loop.
- Runtime init failures for OLED, Wi-Fi, LittleFS, Web, or LoRa are logged and exposed in `/api/status`; they do not stop the loop or restart the ESP32.
- Serial diagnostics and periodic `START alive` / `FINISH alive` heartbeats are intentionally kept; link detail logs are rate-limited to five seconds.
- Bare Heltec WiFi LoRa 32 V3 boards do not include an onboard buzzer. Sound will require an external buzzer module later; this smoke-test firmware uses the onboard status LED for extra event indication.

## Versioning

- Firmware version is a manual semantic-like incremental string.
- Initial version: `0.00`.
- Each MR/iteration increments the string by `0.01` manually.
- Current version: `0.08`.
- The version is configured with `FIRMWARE_VERSION` and has a source fallback of `0.08`.
- Version is shown in OLED headers, Serial boot logs, Web API status, the Web UI status block, and non-heartbeat LoRa payloads where appropriate. Compact `STATUS` heartbeat packets intentionally omit the version to stay small.


## v0.08 compact LoRa STATUS heartbeat

Firmware v0.08 keeps StartStation and FinishStation as separate applications and fixes oversized LoRa heartbeat packets. `STATUS` is now sent as compact JSON instead of full debug JSON, for example `{"t":"S","sid":"f","st":"I","hb":3,"up":23,"sr":-37,"ss":12}` from FinishStation and `{"t":"S","sid":"s","st":"R","hb":71,"up":123}` from StartStation.

- Full debug data is no longer sent in the heartbeat. Web UI diagnostics are assembled from local `LinkStatus` and locally cached state, not by inflating the LoRa `STATUS` payload.
- Compact keys are used for the heartbeat: `t` (`S` for STATUS), `sid` (`s` or `f`), `st` (`R`, `C`, `G`, `I`, `F`, `A`, `E`), `hb`, and uptime seconds in `up`. FinishStation may include compact reverse-link signal fields `sr`, `ss`, and `sa`.
- The target heartbeat payload size is below 160 bytes, typically around 80-140 bytes with signal fields.
- If a `STATUS` payload exceeds 200 bytes, firmware logs `STATUS payload too large len=...`.
- If a `STATUS` payload exceeds 240 bytes, firmware skips that oversized packet and transmits an emergency minimal heartbeat such as `{"t":"S","sid":"f","hb":2}` instead.
- Priority messages (`RUN_START`, `RUN_START_ACK`, `FINISH`, `FINISH_ACK`, `HELLO`, and `HELLO_ACK`) keep their full JSON fields where needed, including run/rider/trail/result data, but their payload lengths are logged and warned when oversized.
- The reason for this split is that LoRa payloads must remain small and reliable; previous full `STATUS` debug packets could grow after link data was added and exceed the SX1262/Radiolib transmit limit.

The deserializer accepts both legacy full `STATUS` packets (`type`, `stationId`, `state`, `heartbeat`) and the new compact heartbeat (`t`, `sid`, `st`, `hb`) so mixed-version smoke tests can still exchange link heartbeats.

## v0.08 timing, run numbers, browser time sync, battery, and buttons

Firmware v0.08 keeps StartStation and FinishStation as separate applications. It does not add Web UI start, real E3JK/buzzer/encoder/RFID hardware, NTP, PDF/Excel/SQLite exports, or blocking loop delays.

### Human run numbers and result time

- StartStation now assigns a RAM-only human `runNumber` counter when a new run is created: `1`, `2`, `3`, and so on. After reboot the counter starts from `1` again; persistent sequence restoration from saved runs is planned for a later iteration.
- The LoRa protocol and ACK matching still use the technical `runId`.
- The Web UI results table uses the human run number as the main visible id and keeps the technical `runId` available as debug/tooltip data.
- Finished runs include `startedAtText` / `runStartedAtText` so the results table and CSV can show when the run was conducted. If station time was not synchronized, `RunTime` is `TIME NOT SYNCED`.
- CSV export columns are now:

```text
RunNumber;RunId;RunTime;Rider;Trail;StartMs;FinishMs;ResultMs;Result;Status;Source;TimingSource
```

### Browser Web Time Sync

- StartStation runs as a Wi-Fi AP and may not have Internet, so v0.08 does not use NTP.
- A DS3231 RTC is not required or used in this iteration.
- The browser sends calendar time to StartStation with `POST /api/time/sync` using `epochMs`, `timezoneOffsetMinutes`, and `isoLocal`.
- The Web UI automatically syncs time once when opened and also provides a **Synchronize time** button.
- `/api/status` reports `wallClockSynced`, `currentTimeText`, `lastTimeSyncText`, and `timeSource`.
- If time is not synchronized, starting a run is still allowed; the run simply stores `TIME NOT SYNCED`.

### Timing model and current smoke-test accuracy

- Countdown `3, 2, 1` is preparation only and is not part of the result.
- StartStation fixes `startTimestampMs` only at `GO`, starts the riding timer only at `GO`, and sends `RUN_START` only after `GO`.
- FinishStation starts its local elapsed timer when it receives `RUN_START`.
- On finish, FinishStation sends `finishTimestampMs = remoteStartTimestampMs + localElapsedSinceRunStartReceived`.
- StartStation uses the received `finishTimestampMs` as the primary timing source and marks the run as `FINISH_LOCAL_ELAPSED`. If the finish timestamp is missing, StartStation falls back to receive time and marks `START_RECEIVE_FALLBACK`.
- Without synchronized RTCs on both stations, LoRa delivery latency still creates a small offset. Future real accuracy should use DS3231 synchronization or an explicit station time-sync protocol.

### Signal, battery, and button behavior

- Signal text is consistently `NO SIGNAL`, not `NO SIG`, `NO-SIG`, `NOSIG`, or `OFF`.
- `BatteryService` is a safe abstraction shared by both stations. By default `BATTERY_ADC_ENABLED=0`, so OLED/API show USB or unknown instead of fake battery values. Battery percent requires configuring and calibrating `BATTERY_ADC_PIN`, `BATTERY_VOLTAGE_DIVIDER_RATIO`, `BATTERY_MIN_VOLTAGE`, and `BATTERY_MAX_VOLTAGE` for the actual board and wiring.
- Main start/finish actions use debounced short press events on the pressed edge (`INPUT_PULLUP`, active LOW). Long press is not required. Holding the button does not repeat the event; the next action is possible only after release and a later press.
- Serial heartbeat diagnostics include button raw/pressed state and timing/link details for troubleshooting.

## v0.04 reliable race flow

Version `0.04` restores the main race path and makes the start/finish radio chain resilient to background traffic:

- `RUN_START` is no longer a single best-effort packet. StartStation sends it immediately on `GO`, then retries every 500 ms up to 10 attempts until FinishStation returns `RUN_START_ACK`.
- FinishStation sends `RUN_START_ACK` after every valid `RUN_START`. Duplicate `RUN_START` packets for the active run do not reset the timer; they only resend the ACK.
- Priority packets are `RUN_START`, `RUN_START_ACK`, `FINISH`, and `FINISH_ACK`. Background `HELLO`, `HELLO_ACK`, and `STATUS` are deferred when a priority packet or retry is pending.
- HELLO discovery pauses during active runs and ACK-wait windows. STATUS can continue, but only when no priority transmit is pending.
- FinishStation has a single active finish-wait state: `Riding`; `FinishState::canFinish()` is true only there.
- FinishStation button handling logs raw transitions, debounced presses, current state, `canFinish`, and active run ID. A short press in `Riding` sends FINISH from `BUTTON_STUB`; short presses in `FinishSent` or `AckTimeout` resend the saved FINISH.
- FINISH remains reliable with 15 attempts at a 1000 ms interval, independent of discovery/STATUS traffic. StartStation treats duplicate FINISH packets for the completed run as ACK-resend requests, not duplicate results.
- After every LoRa transmit, firmware yields briefly, waits 1 ms for radio settle, and logs `LoRa RX mode restored` so the next poll can receive again.



## v0.04 finish confirmation and OLED result display

Firmware v0.04 keeps StartStation and FinishStation as separate applications and focuses on the finish confirmation path:

- FinishStation sends `FINISH` with the saved finish timestamp and then waits for `FINISH_ACK` from StartStation.
- StartStation accepts the first valid `FINISH` for the active run, calculates `resultMs`, stores `resultFormatted` in seconds (for example `20.123 s`), and replies immediately with `FINISH_ACK`.
- `FINISH_ACK` includes `stationId: "start"`, `version: "0.08"`, `bootId`, `runId`, `resultMs`, `resultFormatted`, and `timestampMs`.
- StartStation sends the ACK three times total using a non-blocking millis-based resend policy (`FINISH_ACK_REPEAT_COUNT=3`, `FINISH_ACK_REPEAT_INTERVAL_MS=250`).
- Repeated `FINISH` packets for an already completed `runId` do not create duplicate run records or recalculate the result; they immediately trigger another `FINISH_ACK`.
- FinishStation updates the StartStation link from any valid `FINISH_ACK`, accepts matching ACKs for either the active run or the last finished run, stops FINISH retries immediately, leaves `AckTimeout` if a late matching ACK arrives, and shows `ACK OK` with the result.
- If no matching ACK is received after 15 FINISH attempts, FinishStation shows `ACK TIMEOUT`, the best known result in seconds, and `PRESS RESEND`; pressing the finish button resends the saved FINISH without changing the timestamp.
- Both OLEDs show the last run time in seconds. StartStation shows `FINISHED` with the rider/trail and continues to show `Last: 20.123 s` after returning to Ready. FinishStation shows the local result while waiting and replaces it with the ACK result when received.
- `/api/status` includes finish ACK diagnostics such as `pendingFinishAck`, `finishAckRepeatCount`, `lastFinishedRunId`, `lastFinishAckSentMs`, `lastFinishAckRunId`, and `finishAckSendCount`.

## Onboard LED indication

Bare Heltec WiFi LoRa 32 V3 boards have no built-in buzzer. For now, the onboard LED provides additional non-audio event indication; a real buzzer is intentionally not connected by this firmware. The LED pin is configured by `STATUS_LED_PIN`, the active level by `STATUS_LED_ACTIVE_LEVEL`, and the shared `LedIndicator` service disables itself safely when no usable pin is configured.

LED patterns are millis-based and do not block the main loop:

- Ready slow blink
- Countdown fast blink
- Riding solid
- Finish flash
- Ack timeout fast blink
- Error fast blink
- No-signal blink

## Link status and LoRa signal

Both stations use one source of truth for radio link state: the timestamp of the last valid packet received from the opposite station. RSSI/SNR and packet age are updated together in the shared `LinkStatus` model.

Signal display rules:

- `NO SIGNAL` means no valid packet from the other station has been received for more than 25 seconds.
- RSSI without an active link should not happen in normal UI/API output: if a fresh packet updates RSSI, it also updates the last-packet timestamp and the link is active until it becomes stale.
- Signal and station state are separate. A stale link displays `NO SIGNAL`; the station state is reported as `Unknown` in the API while the last known state remains available for diagnostics.
- `FIN:-72dBm` means StartStation is currently hearing packets from FinishStation.
- `START:-33dBm` means FinishStation is currently hearing packets from StartStation.
- StartStation sends a `STATUS` heartbeat with `stationId: "start"` every 5000 ms, so FinishStation can show StartStation signal while idle.
- FinishStation sends a `STATUS` heartbeat with `stationId: "finish"` every 5000 ms in all states: `Idle`, externally displayed `Riding`, `FinishSent`, and `Error` / ACK timeout. FINISH retry packets keep priority; if a retry and STATUS would be transmitted in the same millisecond, STATUS is deferred to the next loop.
- Any valid packet from the opposite station updates link status, not only `STATUS`. StartStation updates the FinishStation link from `STATUS`, `FINISH`, and any other valid `stationId: "finish"` packet. FinishStation updates the StartStation link from `STATUS`, `RUN_START`, `FINISH_ACK`, and any other valid `stationId: "start"` packet.
- Every received LoRa packet logs `LORA RX type=... rssi=... snr=... raw=...`; valid opposite-station packets also log `LORA RX from=... type=... rssi=... snr=... age=0 count=...`. JSON parse failures, missing `stationId`, and unknown `type` are logged explicitly with the raw packet.
- After every LoRa TX (`HELLO`, `HELLO_ACK`, `STATUS`, `RUN_START`, `RUN_START_ACK`, `FINISH`, or `FINISH_ACK`), the firmware yields briefly, waits 1 ms for SX1262 settle, and then lets the next polling `receive()` re-enter receive/listen mode after the short blocking transmit. Serial prints `LoRa RX mode restored` after every transmit; STATUS is now only sent every five seconds, so each STATUS heartbeat is logged.
- `STATUS payload len=...` is logged for heartbeat diagnostics and whenever a STATUS payload exceeds 200 bytes. STATUS packets are serialized in a compact JSON form to keep the LoRa payload small while remaining accepted by the same deserializer.
- StartStation and FinishStation include `version: "0.08"` and a per-boot `bootId` in `RUN_START`, `RUN_START_ACK`, `FINISH`, `FINISH_ACK`, `HELLO`, and `HELLO_ACK` payloads. Compact `STATUS` omits version and boot id by design.
- FinishStation includes only compact numeric reverse-link fields (`sr`, `ss`, and optionally `sa`) in `STATUS`; StartStation combines those values with local link state for `/api/status`.


## LoRa discovery and remote reboot detection

Firmware v0.04 adds an explicit discovery handshake for stations that are powered on at different times or rebooted independently:

- `LINK_HEARTBEAT_INTERVAL_MS` is 5000 ms.
- `LINK_TIMEOUT_MS` is 25000 ms.
- While no active link exists, each station sends `HELLO` every 1000 ms.
- The other station answers with `HELLO_ACK`.
- Any valid packet from the opposite station refreshes `LinkStatus`, RSSI/SNR, last packet type, and last seen age.
- When a link is active, normal `STATUS` heartbeat packets continue every 5000 ms in every state.
- `NO SIGNAL` is shown only when the shared `LinkStatus` is stale for more than 25000 ms; as soon as a fresh `HELLO`, `HELLO_ACK`, `STATUS`, `RUN_START`, `FINISH`, or `FINISH_ACK` arrives, the display and Web UI switch back to RSSI.
- Each boot generates a station-specific `bootId`. If the remote station sends the same `stationId` with a different `bootId`, the receiver logs `REMOTE REBOOT detected ...`, updates the debug boot id, increments the remote reboot counter, and clears stale per-session assumptions.
- StartStation `/api/status` exposes discovery state, remote boot id, remote reboot count, last packet type, packet count, signal, and FinishStation-reported reverse StartStation signal.

## FinishStation riding and finish flow

Firmware v0.04 restores the lower-terminal run state machine:

- FinishStation enters externally visible `Riding` immediately after a valid `RUN_START` packet from StartStation.
- The Riding OLED screen shows `FINISH TERM v0.08`, a moving `RIDING >` indicator, rider name, elapsed timer, and StartStation RSSI or `START:NO SIGNAL`.
- A short press on the finish button in Riding accepts the finish, shows `FINISH LINE / CROSSED`, sends a `FINISH` packet with `source: "BUTTON_STUB"`, and moves to `FinishSent`.
- `FINISH_MAX_RETRY_ATTEMPTS` is 15 and `FINISH_RETRY_INTERVAL_MS` is 1000 ms.
- In `FinishSent`, automatic retries continue until ACK or timeout. A short button press manually resends the last `FINISH` without changing its timestamp.
- If ACK retries are exhausted, the state becomes `AckTimeout` and the OLED shows `ACK TIMEOUT / PRESS BTN RESEND`. A short button press returns to `FinishSent` and resends the saved `FINISH`.
- StartStation resends `FINISH_ACK` for duplicate `FINISH` packets from an already completed run id without creating duplicate results.

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

- `runNumber`
- `runId`
- `startedAtEpochMs`
- `startedAtText`
- `riderId`
- `riderNameSnapshot`
- `trailId`
- `trailNameSnapshot`
- start/finish/result timestamps
- status, finish source, timing source, and timing note

CSV columns:

```text
RunNumber;RunId;RunTime;Rider;Trail;StartMs;FinishMs;ResultMs;Result;Status;Source;TimingSource
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
  "finishLinkActive": true,
  "finishStationOnline": true,
  "finishSignalText": "-72 dBm",
  "finishState": "Riding",
  "finishLastKnownState": "Riding",
  "finishLastSeenAgoMs": 1200,
  "finishPacketCount": 42,
  "finishLastPacketType": "STATUS",
  "finishHeartbeatCount": 42,
  "startHeartbeatCount": 43,
  "lastStartStatusSentAgoMs": 500,
  "finishActiveRunId": "RUN-...",
  "finishRiderName": "Test Rider",
  "finishElapsedMs": 12345,
  "finishRssi": -72,
  "finishSnr": 8.5,
  "finishReportedStartLinkActive": true,
  "finishReportedStartSignalText": "-69 dBm",
  "finishReportedStartRssi": -69,
  "finishReportedStartSnr": 9.2,
  "finishReportedStartLastSeenAgoMs": 250,
  "finishReportedStartPacketCount": 12,
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
- FinishStation signal, state, heartbeat, last packet age, packet count, and last packet type. Missing radio packets are shown as `NO SIGNAL`, not `OFF`.
- Signal from FinishStation (`Signal from finish: -72 dBm`) and signal reported by FinishStation from StartStation (`Signal reported by finish from start: -33 dBm`) with reverse-link age and packet count.
- Last LoRa packet type and raw packet preview.
- A dedicated **LoRa debug** block: Start heartbeat sent, Finish heartbeat received, last packet from Finish, last seen age, Finish RSSI/SNR, active signal text, last Start STATUS sent age, Finish-reported Start signal, and Finish-reported Start last-seen age. If signal appears only after a `FINISH`, use this block to verify whether STATUS heartbeat TX/RX is running and whether the last packet type changes back to `STATUS`.
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

`STATUS` is sent once per second by both stations and does not require a run ID. It is compact on the radio link to keep the LoRa payload below the practical 200-byte target. The deserializer also accepts the older verbose field names.

StartStation example:

```json
{"t":"S","mid":"start-status-...","sid":"start","st":"Ready","up":123456,"ts":123456,"hb":43,"rid":"RUN-...","rn":"Test Rider","el":12345}
```

FinishStation example with reverse-link diagnostics:

```json
{"t":"S","mid":"finish-status-...","sid":"finish","st":"Idle","up":123456,"ts":123456,"hb":42,"rid":"RUN-...","rn":"Test Rider","el":12345,"sl":true,"sp":12,"sla":250,"sr":-70,"ss":8.5}
```

Compact fields map to the readable form as follows: `t:S` = `type:STATUS`, `sid` = `stationId`, `st` = `state`, `up` = `uptimeMs`, `ts` = `timestampMs`, `hb` = `heartbeat`, `rid` = `activeRunId/runId`, `rn` = `riderName`, `el` = `elapsedMs`, `sl/sp/sla/sr/ss` = FinishStation's reported StartStation link active, packet count, last-seen age, RSSI, and SNR.

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
8. Wait until the Web UI shows the Finish signal as an RSSI value instead of `NO SIGNAL`.
9. Press the StartStation physical button.
10. FinishStation should show `RIDING`, the rider name, timer, and `Btn: FINISH`.
11. Press the FinishStation physical button.
12. FinishStation should show `FINISH SENT` and then `ACK OK` after acknowledgment.
13. StartStation should show `FINISHED` and append the result to `/runs.csv`.
14. Download results from `GET /api/export/runs.csv` or the Web UI CSV link.

## OLED screens

StartStation Ready:

```text
START TERM v0.08
FIN:-72dBm
Rider: Test Rider
Trail: Default trail
PKT:STATUS
```

StartStation Countdown uses immediate rendering on every countdown text change and is not throttled by the normal status screen refresh:

```text
START v0.08
3 / 2 / 1 / GO
```

The countdown state machine is millis-based and non-blocking: `3` for 1000 ms, `2` for 1000 ms, `1` for 1000 ms, and `GO` for about 700 ms. Serial logs include `COUNTDOWN step=... time=...` and `OLED countdown rendered step=... at ms=...`.

StartStation Riding includes a non-blocking moving `>` animation updated from `millis()` about every 250 ms:

```text
START TERM v0.08
RIDING >
Time: 00:12
FIN:-72dBm
PKT:STATUS
```

StartStation Finished:

```text
START v0.08
FINISHED
Test Rider
00:20.123
```

FinishStation Idle:

```text
FINISH TERM v0.08
LoRa: OK
State: IDLE
START:-70dBm
PKT:STATUS
```

FinishStation Riding also shows the moving `>` animation:

```text
FINISH TERM v0.08
RIDING >
Time: 00:12
START:-70dBm
PKT:STATUS
```

FinishStation finish-line overlay appears immediately after an accepted finish button press while retries and ACK handling continue in the background:

```text
FINISH TERM v0.08
FINISH LINE
CROSSED
```

FinishStation FinishSent:

```text
FINISH TERM v0.08
FINISH SENT
Sent: x/15
START:-70dBm
PKT:STATUS
```

FinishStation ACK:

```text
FINISH TERM v0.08
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
3. Watch FinishStation Serial for `FINISH STATUS sent hb=5 state=...` every fifth heartbeat. STATUS is sent every five seconds and is deferred behind priority race packets.
4. Watch StartStation Serial for `LORA RX from=finish type=STATUS ... age=0 count=...`, `STATUS RX from=finish hb=... rssi=...`, and the five-second `START LINK DEBUG:` diagnostic.
5. Watch FinishStation Serial for `LORA RX from=start type=STATUS ... age=0 count=...`, `STATUS RX from=start hb=... rssi=...`, and the five-second `FINISH LINK DEBUG:` diagnostic; this confirms StartStation heartbeat packets are reaching FinishStation in Idle.
6. In the Web UI **LoRa debug** block, check that heartbeat sent and heartbeat received keep increasing, last packet type regularly returns to `STATUS`, and last-seen age stays near one second.
7. If packet parsing fails, inspect `LORA parse failed raw=... error=...` on Serial.
8. Check that antennas are connected to both boards.

### Rider/trail Add button does nothing

1. Open the browser console and confirm `EnduroTimer UI loaded` appears. If it does not, check that `/app.js` is served from LittleFS and upload the filesystem again.
2. Press Add and check for `Adding rider ...` or `Adding trail ...` in the console.
3. Watch StartStation Serial for `HTTP POST /api/riders/add body=...` or `HTTP POST /api/trails/add body=...`.
4. Use `GET /api/debug/routes` to confirm the expected API routes are registered.
5. The Web UI now shows per-section success/error text in `ridersMessage` and `trailsMessage`.

### Finish button does not react

1. Check that `FINISH_BUTTON_PIN=0` is used for the temporary BOOT-button finish input.
2. Watch Serial for `button raw pressed transition` and `debounced short press` logs.
3. Watch the five-second FinishStation heartbeat fields `buttonRaw=...` and `buttonPressed=...`.
4. Confirm FinishStation state is `Riding` and `canFinish()` is true. Finish presses in Idle are ignored and briefly show `NO ACTIVE RUN`.

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

Serial diagnostics and STATUS heartbeats are intentionally kept on both roles.
