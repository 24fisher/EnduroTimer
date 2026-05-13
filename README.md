# EnduroTimer MTB Enduro Timing Prototype

EnduroTimer is a PC-runnable C#/.NET prototype for an MTB enduro training timer. The current prototype is intentionally simplified so it can act as the logical reference for a future autonomous Heltec ESP32-S3 web UI.

The .NET app models:

- an upper/start Heltec station with Wi-Fi AP style Web UI, LoRa, RTC sync, START button, buzzer, encoder, RFID simulation, LED display preview, and LittleFS-like files;
- a lower/finish Heltec station with LoRa, RTC, and a simulated E3JK-R4M1 photoelectric finish sensor.

No real hardware is required or connected. Hardware is emulated through the Web UI and small service interfaces.

## Project layout

```text
EnduroTimer.sln
src/EnduroTimer.Core      Domain models, state machine, file repositories, simulated hardware/transport
src/EnduroTimer.Web       ASP.NET Core JSON API and one static HTML/CSS/JS page
tests/EnduroTimer.Tests   Existing dependency-free console test runner
```

## How to run

```bash
dotnet run --project src/EnduroTimer.Web/EnduroTimer.Web.csproj
```

Open the URL printed by ASP.NET Core. The default page is served from `/` and is a single static HTML document that calls the JSON API.

## ESP32 portability assumptions

The prototype is kept close to the intended Heltec ESP32-S3 implementation:

- UI remains simple static HTML/CSS/JavaScript.
- API remains simple JSON over HTTP.
- Storage is file-based JSON/CSV/JSONL.
- There is no database dependency.
- There is no server-side rendering requirement.
- There is no PDF/XLSX generation on the device.
- There is no heavy frontend framework.
- CSV and JSON backup are the target interchange formats.
- PDF/XLSX reports can be generated later on a PC from exported CSV files.

## File-based storage

Runtime data is stored under `src/EnduroTimer.Web/data/` by default:

```text
data/settings.json      Current system settings, including trailName and groupStartIntervalSeconds
data/riders.json        Registered riders
data/group_queue.json   Group queue rider IDs, position, and loop flag
data/runs.jsonl         Main append-style run journal, one JSON record per line
data/runs.csv           Human-readable run log with UTF-8 BOM and semicolon delimiter
```

The core logic depends on repository interfaces rather than concrete files, but the default implementation is file-based to match future LittleFS/microSD storage.

## Web UI blocks

The single `index.html` page contains these ESP32-style blocks:

- System status
- LED display preview
- Settings / trail name / group start interval
- Rider selection
- Encoder simulation
- RFID simulation
- Group queue
- Start controls
- Active runs
- Recent runs
- Rider statistics
- Export CSV/Backup

No React, Vue, Angular, Bootstrap, external fonts, or binary assets are used.

## State machine

Start station states:

- `Boot`
- `Ready`
- `Countdown`
- `Riding`
- `Finished`
- `Error`

Finish station states:

- `Boot`
- `Idle`
- `WaitFinish`
- `Finished`
- `SensorBlocked`
- `Offline`

Operation modes:

- `ManualEncoderSelection`
- `GroupQueue`

## Time synchronization

Time synchronization is mandatory before starting a run.

At application start:

- `isTimeSynchronized = false`
- `canStartRun = false`
- `ledDisplayText = "SYNC TIME"`
- `startBlockedReason = "Time synchronization required"`

Endpoints:

- `POST /api/time/sync` emulates RTC synchronization between stations.
- `POST /api/time/simulate-offset` accepts `{ "rtcOffsetMs": 250 }` and keeps start blocked if the absolute offset is greater than 100 ms.

## Registered riders

A registered rider has:

- `riderId`
- `displayName`
- optional `rfidTagId`
- `isActive`
- `createdAtMs`

Endpoints:

- `GET /api/riders`
- `POST /api/riders`
- `PUT /api/riders/{id}`
- `POST /api/riders/{id}/deactivate`

Runs store a rider name snapshot so old results still display correctly after rider edits.

## Encoder and RFID simulation

`ManualEncoderSelection` uses active riders as the encoder list.

- `POST /api/encoder/left`
- `POST /api/encoder/right`
- `POST /api/encoder/press`

RFID is simulated only:

- `POST /api/rfid/simulate`

```json
{ "tagId": "E2000017221101441890ABCD" }
```

A known active tag selects the rider and updates the LED preview. An unknown tag shows `UNKNOWN TAG` and blocks start.

## Group queue continuous start mode

`GroupQueue` mode runs a continuous start session instead of a single isolated run. The key trail assumption is **no overtaking**: riders are expected to finish in the same order they started.

When `POST /api/runs/start` is called in `GroupQueue`, it starts the queue auto-start session:

- the current queue rider becomes `NOW`;
- the following queue rider becomes `NEXT`;
- the first rider receives an immediate `3`, `2`, `1`, `GO` countdown;
- after `GO`, the run is marked `Riding`, assigned an increasing `sequenceNumber`, and appended to the in-memory FIFO `activeRuns` list;
- the queue position advances immediately and the next rider preparation begins without waiting for the previous rider to finish;
- the last three seconds before each subsequent start show `3 Rider`, `2 Rider`, `1 Rider`, then `GO Rider`.

`groupStartIntervalSeconds` controls spacing between starts and is clamped to a minimum of 3 seconds. The default is 10 seconds and it is stored in `data/settings.json`:

```json
{
  "trailName": "Default trail",
  "groupStartIntervalSeconds": 10
}
```

The LED/Web UI meanings are:

- `NOW` = who is starting now;
- `NEXT` = who is preparing next;
- `Expected finisher` = the first active FIFO run and therefore the rider expected at the finish next.

Finish logic in `GroupQueue` is FIFO. Each finish sensor trigger completes the earliest active `Riding` run by `sequenceNumber` / start timestamp. The finish API does not require a `runId` in this mode. Manual DNF still targets a specific `runId`, removes that run from `activeRuns`, and preserves the order of the remaining riders.

Stopping a group session only stops future starts. It does not finish or DNF riders already on course; they remain available for FIFO finish simulation or manual DNF. Active queue sessions are held in memory and are not intended to be restored after an application restart in this prototype.

Endpoints:

- `GET /api/group-queue`
- `POST /api/group-queue`
- `POST /api/group-queue/start-session`
- `POST /api/group-queue/stop-session`
- `POST /api/group-queue/next`
- `POST /api/group-queue/reset`
- `POST /api/group-queue/move-up`
- `POST /api/group-queue/move-down`
- `POST /api/group-queue/remove`

Stored queue shape:

```json
{
  "riderIds": ["...", "..."],
  "position": 0,
  "loop": true
}
```

## Run flow

`POST /api/runs/start` does not use a manually typed rider name as the main scenario. The rider is selected by the active mode:

- `ManualEncoderSelection` starts one run for `selectedRiderId` and blocks another start while that run is counting down or riding.
- `GroupQueue` starts the queue auto-start session; a running session returns a conflict instead of starting another session.

Common start blockers:

- time not synchronized;
- finish station offline;
- finish beam blocked;
- countdown/riding already active;
- no selected rider;
- empty or finished group queue.

Countdown runs in the background and the endpoint returns quickly. The UI observes `3`, `2`, `1`, and `GO` through `/api/status` polling.

Finish is emulated with:

- `POST /api/finish/simulate`

In `ManualEncoderSelection`, the simulated finish completes the single active run and duplicate finish triggers inside five seconds are ignored by the finish station simulation.

In `GroupQueue`, the same button is global: it completes the next expected FIFO finisher from `activeRuns`, calculates `resultMs` / `resultFormatted`, saves the finished run to `runs.jsonl` / `runs.csv`, and briefly shows `FIN Rider 00:00.000` before returning the LED preview to the current starter.

## Settings

Trail name and `groupStartIntervalSeconds` are stored in `data/settings.json` and are read at run start / group session start.

Endpoints:

- `GET /api/settings`
- `POST /api/settings`

An empty trail name falls back to `Default trail`. `groupStartIntervalSeconds` defaults to 10 and is clamped to at least 3.

## Statistics and export

Rider statistics are computed on demand from `runs.jsonl` and `riders.json`; no separate statistics database is used.

Endpoints:

- `GET /api/statistics/riders`
- `GET /api/export/runs.csv`
- `GET /api/export/riders.csv`
- `GET /api/export/statistics.csv`
- `GET /api/export/backup.json`

CSV exports use UTF-8 BOM and semicolon delimiter for easier Excel opening in Russian locales.

`backup.json` contains:

```json
{
  "settings": {},
  "riders": [],
  "runs": [],
  "groupQueue": {}
}
```

## Removed heavy export path

PDF and Excel `.xlsx` export are not part of the autonomous Heltec architecture. The prototype does not expose PDF/XLSX endpoints and does not require heavy PDF/XLSX libraries. If printable reports are needed later, generate them on a PC from CSV exports.

## Future hardware integration

The prototype keeps hardware behind interfaces so real implementations can be added later without changing the JSON API shape:

- `IClockService` can be backed by DS3231 RTC.
- `IBuzzerService` can drive GPIO buzzer output.
- `IFinishSensorService` can read the E3JK finish sensor.
- `IRadioTransport` can become a LoRa transport for Heltec modules.
- `IRfidReaderService` can be replaced by the real RFID reader.
- `ILedDisplayService` can drive the external LED display.

A real LoRa transport should preserve station-captured timestamps and not rewrite timing data on receipt.
