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

Open the URL printed by ASP.NET Core. The default static page is served from `/` and includes dashboard, riders, group queue, and statistics sections.

## Registered riders

The system now keeps a registered rider list for the future RFID workflow. Each rider has:

- `riderId`
- `displayName`
- optional `rfidTagId`
- `isActive`
- `createdAtMs`

Runs keep the historical `riderName` snapshot so old results still display correctly even if a rider is later deactivated or renamed. New runs also store `riderId` when the rider comes from the registered list.

The **Riders** UI section supports listing, adding, editing, and deactivating riders. Active riders are used by start selection and group queues. The admin table shows all riders and includes run count and best result when available.

RFID tag IDs are optional, but two active riders cannot share the same non-empty `rfidTagId`.

## Operation modes

EnduroTimer supports two start-selection modes.

### ManualEncoderSelection

This is the default mode. It prepares the UI for a future physical rotary encoder on the start station.

- The Web UI emulates the encoder with **←**, **Select**, and **→** buttons.
- Left/right cycle through active registered riders.
- The selected rider is shown on the simulated LED display.
- `POST /api/runs/start` uses `selectedRiderId` and `selectedRiderName`.
- If no active riders exist, start is blocked with `No active riders registered` and the LED preview shows `NO RIDERS`.
- If active riders exist but none is selected, start is blocked with `No rider selected`.
- The legacy manual `riderName` field remains available as a fallback only when a registered rider is not selected.

### GroupQueue

This mode is intended for group training sessions with a predefined rider order.

- The **Group queue** UI section lets you add active riders, move them up/down, remove them, reset the position, and enable/disable looping.
- `groupQueuePosition` points at the next rider.
- `POST /api/runs/start` ignores the manual rider field and starts the next rider from the queue.
- After a successful finished run, the queue advances automatically.
- If `loopGroupQueue` is `true`, the queue wraps to the beginning at the end.
- If `loopGroupQueue` is `false`, reaching the end blocks start with `Group queue finished`.
- An empty queue blocks start with `Group queue is empty` and the LED preview shows `QUEUE EMPTY`.

## LED display preview

Real LED hardware is not connected yet. `ILedDisplayService` is implemented by `SimulatedLedDisplayService`, and the Web UI shows a black LED-style preview block.

Display rules include:

- `SYNC TIME` when time synchronization is required.
- `FINISH OFFLINE` when the finish station is offline.
- `BEAM BLOCKED` when the finish beam is blocked.
- `NO RIDERS` when no active riders are registered in manual mode.
- The selected rider name in `ManualEncoderSelection`.
- The next queue rider name in `GroupQueue`.
- Countdown values `3`, `2`, `1`, and `GO` during countdown.
- The rider name while riding.
- The formatted result after finish, then the next selected/queued rider as status refreshes.

## RFID simulation and future RFID integration

Real RFID hardware is not connected yet. The code includes `IRfidReaderService` and `SimulatedRfidReaderService` so a hardware implementation can be added later without changing the Web API shape.

In the Web UI, use **RFID simulation** to enter a tag ID and click **Simulate RFID scan**.

- If the tag matches an active registered rider in `ManualEncoderSelection`, that rider becomes selected and the LED preview shows the rider name.
- If the tag is unknown, the LED preview shows `UNKNOWN TAG` and start is blocked with `Unknown RFID tag`.
- In `GroupQueue`, RFID scans are recorded for visibility, but start still follows the queue.

## PDF Cyrillic font configuration

The repository does **not** include binary fonts. PDF export can embed an external TTF/OTF font when configured, which allows Cyrillic rider names, trail names, and table headings to render correctly.

Recommended fonts:

- `NotoSans-Regular.ttf`
- `DejaVuSans.ttf`

Download one of these fonts yourself and put it outside the repository, or in a local ignored folder that you do not commit. Then configure the path through `appsettings.json` or an environment variable.

Example `src/EnduroTimer.Web/appsettings.json`:

```json
"Pdf": {
  "FontPath": "C:/EnduroTimer/fonts/NotoSans-Regular.ttf",
  "FontFamily": "NotoSans"
}
```

Environment variable example:

```bash
Pdf__FontPath=/opt/endurotimer/fonts/NotoSans-Regular.ttf
```

If `Pdf:FontPath` is empty or points to a missing file, PDF export still works with the built-in fallback font, but the application logs this warning:

```text
PDF Cyrillic font path is not configured. Cyrillic text may render incorrectly.
```

Do not commit `.ttf`, `.otf`, `.woff`, images, archives, or other binary files to this repository.

## API endpoints

### System and run flow

- `GET /api/status` - station state, countdown, diagnostics, start lock fields, active/last run, operation mode, selected/next rider, LED text, and group queue position/length.
- `POST /api/time/sync` - sends `SyncTime` and processes `SyncTimeAck`.
- `POST /api/runs/start` - starts according to the active operation mode. Compatible body: `{ "riderName": "Fallback", "trailName": "Лесной СУ 1" }`.
- `POST /api/finish/simulate` - simulates the lower finish sensor.
- `GET /api/runs` - recent runs with rider, trail, mode, queue position, status, result, and personal-best flag.
- `GET /api/runs/{id}` - one run by ID.
- `POST /api/runs/{id}/dnf` - marks a run as DNF.
- `POST /api/system/reset` - clears in-memory state and requires time synchronization again before the next start.

### Registered riders

- `GET /api/riders` - all registered riders with active flag, run count, and best result.
- `POST /api/riders` - create a rider.

```json
{
  "displayName": "Андрей",
  "rfidTagId": "E2000017221101441890ABCD"
}
```

- `PUT /api/riders/{id}` - update a rider.

```json
{
  "displayName": "Андрей Рыбаков",
  "rfidTagId": "E2000017221101441890ABCD",
  "isActive": true
}
```

- `DELETE /api/riders/{id}` - deactivate a rider.
- `POST /api/riders/{id}/deactivate` - deactivate a rider.

### Mode and selection

- `GET /api/mode` - current operation mode.
- `POST /api/mode` - set `ManualEncoderSelection` or `GroupQueue`.

```json
{ "operationMode": "ManualEncoderSelection" }
```

- `POST /api/encoder/left` - emulate encoder left.
- `POST /api/encoder/right` - emulate encoder right.
- `POST /api/encoder/press` - confirm the current selection.

### Group queue

- `GET /api/group-queue` - current queue entries, position, loop flag, and next rider.
- `POST /api/group-queue` - replace the queue.

```json
{
  "riderIds": ["id1", "id2", "id3"],
  "loopGroupQueue": true
}
```

- `POST /api/group-queue/next` - manually advance to the next rider.
- `POST /api/group-queue/reset` - reset position to the first rider.
- `DELETE /api/group-queue/{index}` - remove a queue entry by index.
- `POST /api/group-queue/remove` - remove by body: `{ "index": 1 }`.

### RFID simulation

- `POST /api/rfid/simulate` - simulate an RFID scan.

```json
{ "tagId": "E2000017221101441890ABCD" }
```

- `GET /api/rfid/last` - last simulated RFID read.

### Statistics and exports

- `GET /api/statistics/riders` - rider aggregates with `riderId`, `riderName`, totals, finished/DNF counts, best/average/last results, `lastRunAt`, and `bestTrailName`. Registered riders with zero runs are included.
- `GET /api/export/results.xlsx` - Excel export with `Results`, `Rider Statistics`, and `Riders` worksheets.
- `GET /api/export/results.pdf` - PDF export with results, rider statistics, and riders. Cyrillic requires a configured external font path.

## Time and result format

- Internal timestamps are Unix milliseconds stored as `long`.
- Run results are stored as `resultMs` (`long`).
- Backend DTOs and the UI format results as `mm:ss.fff`, with milliseconds always padded to three digits.
- LoRa transport latency does not affect timing because start and finish timestamps are captured by station clocks before radio transmission.

## Future Heltec/LoRa hardware integration

The prototype keeps hardware behind interfaces so real implementations can be added later without changing controller or domain logic:

- `IClockService` can be backed by a DS3231 RTC driver.
- `IBuzzerService` can drive a GPIO buzzer output.
- `IFinishSensorService` can read the E3JK finish input through a GPIO/interrupt path.
- `IRadioTransport` can be replaced by a serial or native LoRa implementation for Heltec modules.
- `IRfidReaderService` can be replaced by the real RFID reader.
- `ILedDisplayService` can be replaced by the real external LED display driver.

A real LoRa transport should serialize the existing `RadioMessage` protocol, preserve station IDs and message IDs, and pass station-captured timestamps without rewriting them on receipt.

## Not implemented yet

- Real RFID scanner hardware.
- Real rotary encoder hardware.
- Real external LED display hardware.
- Serial/USB/LoRa hardware implementations.
