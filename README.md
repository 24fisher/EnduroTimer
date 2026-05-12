# EnduroTimer MTB Enduro Timing Prototype

EnduroTimer is a PC-runnable C#/.NET prototype for an MTB enduro training timing system. It models an upper/start station with a web UI and run database, plus a lower/finish station with a photoelectric finish sensor and simulated LoRa transport.

## Project layout

```text
EnduroTimer.sln
src/EnduroTimer.Core      Domain models, message protocol, state machines, simulated hardware/transport
src/EnduroTimer.Web       ASP.NET Core Web API and single-page Web UI
tests/EnduroTimer.Tests   Dependency-free console test runner for the core timing logic
```

## How to run

> The current prototype has no real hardware dependency. All station IO is simulated in process.

```bash
dotnet run --project src/EnduroTimer.Web/EnduroTimer.Web.csproj
```

Open the URL printed by ASP.NET Core, then use the Russian-language single-page UI. The default static page is served from `/`.

## How to use the UI

The web UI is localized to Russian while this README is maintained in English. The main controls are:

1. After application startup, click **Синхронизировать время** before the first start. Runs are blocked until both station RTCs are synchronized.
2. Enter a rider name or rider number in **Имя или номер райдера**. The backend rejects an empty rider name with `Rider name is required`.
3. Optionally enter **Название трассы**. Empty trail names are stored as `Default trail`.
4. Click **Старт**.
5. The backend immediately enters `Countdown` and returns the new `runId`; it does not hold the HTTP request open while the countdown runs.
6. The UI polls `GET /api/status` every 250 ms and displays the live `countdownText` values `3`, `2`, `1`, and `GO` in the central status panel.
7. On `GO`, the upper station records `startTimestampMs`, changes the run to `Riding`, and sends `RunStart` to the lower station.
8. Click **Сымитировать финиш** to emulate the E3JK finish beam trigger.
9. The lower station records `finishTimestampMs`, sends `Finish` to the upper station, and the upper station calculates `resultMs = finishTimestampMs - startTimestampMs`.
10. The latest result is displayed as `mm:ss.fff`, and recent runs are listed in **Последние заезды**. Each rider's fastest finished run is highlighted as their personal best.

Additional controls:

- **Синхронизировать время** sends `SyncTime`, clears the initial start lock when the RTC offset is no more than 100 ms, and displays a warning while the offset is too large.
- **Сброс** clears in-memory runs and returns stations to their ready/idle state.
- **DNF** is available for active/riding runs in the recent runs table.
- **Статистика** opens `/statistics`, a rider statistics page with totals, finished/DNF counts, best/average/last results, best trail, and personal-best counts.
- **Export Excel** downloads `/api/export/results.xlsx` with `Results` and `Rider Statistics` worksheets.
- **Export PDF** downloads `/api/export/results.pdf` with compact results and rider statistics tables.

## API endpoints

- `GET /api/status` - station state, `countdownText`, `isCountdownActive`, diagnostics, beam status, `rtcOffsetMs`, `rtcWarning`, `isTimeSynchronized`, `timeSyncRequired`, `canStartRun`, `startBlockedReason`, active/current run ID, and last result. The web UI uses regular polling of this endpoint to show the live countdown and start-lock reason instead of relying on the start response.
- `POST /api/time/sync` - sends `SyncTime` and processes `SyncTimeAck`. After a successful check with offset <= 100 ms, `isTimeSynchronized` becomes `true`; larger offsets set `timeSyncRequired` and keep start blocked.
- `POST /api/runs/start` - body: `{ "riderName": "Andrey", "trailName": "Лесной СУ 1" }`. `rider`, `riderName`, or `riderNumber` are accepted for compatibility, and `trackName` is accepted as an alias for `trailName`. Returns quickly after setting the upper station to `Countdown`; returns `409 Conflict` with `Time synchronization required before starting a run` before sync or `Run already active` if a run is already in `Countdown` or `Riding`.
- `POST /api/finish/simulate` - simulates the lower finish sensor.
- `GET /api/runs` - recent runs with `runId`, `riderName`, `trailName`, timestamps, `resultMs`, `resultFormatted`, `status`, and `isPersonalBest`.
- `GET /api/runs/{id}` - one run by ID.
- `POST /api/runs/{id}/dnf` - marks a run as DNF.
- `GET /api/statistics/riders` - rider aggregates with total runs, finished runs, DNF runs, best/average/last result fields, `lastRunAt`, `bestTrailName`, and `personalBestCount`.
- `GET /api/export/results.xlsx` - Excel export with `Results` and `Rider Statistics` sheets. Empty datasets still produce headers.
- `GET /api/export/results.pdf` - compact PDF export with results and rider statistics tables. Empty datasets still produce headers.
- `POST /api/system/reset` - clears in-memory state and requires time synchronization again before the next start.

## Time and result format

- Internal timestamps are Unix milliseconds stored as `long`.
- Run results are stored as `resultMs` (`long`).
- Backend DTOs and the UI format results as `mm:ss.fff`, with milliseconds always padded to three digits (for example, `63218` ms becomes `01:03.218`).
- LoRa transport latency does not affect timing because start and finish timestamps are captured by station clocks before radio transmission.

## Station state machines

Upper/start station:

- `Boot`
- `Ready`
- `Countdown`
- `Riding`
- `Finished`
- `Error`

Lower/finish station:

- `Boot`
- `Idle`
- `WaitFinish`
- `Finished`
- `SensorBlocked`

Finish debounce rules:

- Finish triggers are accepted only in `WaitFinish`.
- The first accepted trigger sends one `Finish` message and moves the lower station to `Finished`.
- Repeated triggers within 5 seconds do not create additional `Finish` messages.
- Triggers before `RunStart` are ignored.

## Radio protocol

All radio messages use this shape:

```json
{
  "messageId": "guid",
  "type": "RunStart",
  "stationId": "upper-start",
  "runId": "guid-or-null",
  "timestampMs": 1710000000000,
  "payload": {}
}
```

Supported message types:

- `Ping`
- `Pong`
- `SyncTime`
- `SyncTimeAck`
- `RunStart`
- `Finish`
- `FinishAck`
- `Status`

Current simulated flow:

1. A start request creates a pending run, sets the upper station to `Countdown`, and returns immediately.
2. Upper advances `countdownText` through `3`, `2`, `1`, and `GO` in the background.
3. On `GO`, upper captures `startTimestampMs` and sends `RunStart` with `runId`, rider payload, and the captured start timestamp.
4. Lower enters `WaitFinish`.
5. Lower sends `Finish` with the same `runId` and locally captured `finishTimestampMs`.
6. Upper stores the result and sends `FinishAck`.
7. Lower resets back to `Idle` after `FinishAck`.

## Diagnostics

The status API and UI expose:

- Upper station online/offline flag.
- Lower station online/offline flag.
- `BeamClear` / `BeamBlocked`.
- Emulated last RSSI.
- Emulated battery voltage for both stations.
- RTC offset and warning when absolute offset exceeds 100 ms.
- Start eligibility fields: `isTimeSynchronized`, `timeSyncRequired`, `canStartRun`, and `startBlockedReason`.

## Future Heltec/LoRa integration

The prototype keeps hardware behind interfaces so real implementations can be added without changing controller or domain logic:

- `IClockService` can be backed by a DS3231 RTC driver.
- `IBuzzerService` can drive a GPIO buzzer output.
- `IFinishSensorService` can read the E3JK finish input through a GPIO/interrupt path.
- `IRadioTransport` can be replaced by a serial or native LoRa implementation for Heltec modules.

A real LoRa transport should serialize the existing `RadioMessage` protocol, preserve station IDs and message IDs, and pass the station-captured timestamps without rewriting them on receipt.

## Hardware sketch

- Upper station: `1×18650 → boost 5V → Heltec + DS3231 + start button + buzzer`.
- Lower station: `3S 18650 → 12V E3JK-R4M1 DC + buck 5V Heltec + DS3231 + sensor input`.

## Tests

The test project is intentionally dependency-free and can be run as a console program:

```bash
dotnet run --project tests/EnduroTimer.Tests/EnduroTimer.Tests.csproj
```

Covered scenarios:

- Start immediately creates a pending run and enters `Countdown`.
- Start returns before the countdown completes.
- Countdown emits `3`, `2`, `1`, and `GO`.
- `startTimestampMs` is captured on `GO`, then the run enters `Riding`.
- Duplicate starts during `Countdown`/`Riding` are rejected.
- Finish after start moves the run to `Finished`.
- `resultMs` is calculated as `finishTimestampMs - startTimestampMs`.
- Duplicate finish within 5 seconds is ignored.
- Finish before `RunStart` is ignored.
- RTC offset greater than 100 ms produces a warning.
- Result formatting uses `mm:ss.fff`.
- Personal best flags are calculated per rider from finished runs only.
