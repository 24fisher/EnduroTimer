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

Open the URL printed by ASP.NET Core, then use the single-page UI. The default static page is served from `/`.

## How to use the UI

1. Enter a rider name or rider number.
2. Click **Start**.
3. The backend performs the `3`, `2`, `1`, `GO` countdown. On `GO`, the upper station records `startTimestampMs`, creates a `Run` in `Riding`, and sends `RunStart` to the lower station.
4. Click **Simulate finish sensor** to emulate the E3JK finish beam trigger.
5. The lower station records `finishTimestampMs`, sends `Finish` to the upper station, and the upper station calculates `resultMs = finishTimestampMs - startTimestampMs`.
6. The latest result is displayed as `mm:ss.fff`, and recent runs are listed in the table.

Additional controls:

- **Sync time** sends `SyncTime` and displays the RTC offset warning if the emulated clock offset is greater than 100 ms.
- **Reset** clears in-memory runs and returns stations to their ready/idle state.
- **DNF** is available for active/riding runs in the recent runs table.

## API endpoints

- `GET /api/status` - station state, diagnostics, beam status, RTC offset, active/last run.
- `POST /api/time/sync` - sends `SyncTime` and processes `SyncTimeAck`.
- `POST /api/runs/start` - body: `{ "rider": "42" }`.
- `POST /api/finish/simulate` - simulates the lower finish sensor.
- `GET /api/runs` - recent runs.
- `GET /api/runs/{id}` - one run by ID.
- `POST /api/runs/{id}/dnf` - marks a run as DNF.
- `POST /api/system/reset` - clears in-memory state.

## Time and result format

- Internal timestamps are Unix milliseconds stored as `long`.
- Run results are stored as `resultMs` (`long`).
- The UI formats results as `mm:ss.fff`.
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

1. Upper sends `RunStart` with `runId`, rider payload, and `startTimestampMs`.
2. Lower enters `WaitFinish`.
3. Lower sends `Finish` with the same `runId` and locally captured `finishTimestampMs`.
4. Upper stores the result and sends `FinishAck`.
5. Lower resets back to `Idle` after `FinishAck`.

## Diagnostics

The status API and UI expose:

- Upper station online/offline flag.
- Lower station online/offline flag.
- `BeamClear` / `BeamBlocked`.
- Emulated last RSSI.
- Emulated battery voltage for both stations.
- RTC offset and warning when absolute offset exceeds 100 ms.

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

- Start creates a run in `Riding`.
- Finish after start moves the run to `Finished`.
- `resultMs` is calculated as `finishTimestampMs - startTimestampMs`.
- Duplicate finish within 5 seconds is ignored.
- Finish before `RunStart` is ignored.
- RTC offset greater than 100 ms produces a warning.
