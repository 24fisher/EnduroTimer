Firmware version: 0.31.



Context:

v0.29 improved FinishStation FINISH\_ACK handling.

v0.30 goal was to fix RUN\_START\_ACK / retry congestion.

Now extend that task with:



\* battery level display on StartStation, FinishStation, RepeaterStation

\* rarer HELLO packets

\* clearer RepeaterStation OLED UI



Current observed problems from logs:



\* StartStation sends RUN\_START retries up to 15/15.

\* StartStation prints RUN\_START ACK TIMEOUT attempts=15.

\* FinishStation receives duplicate RUN\_START while already Riding/FinishSent.

\* FinishStation sends RUN\_START\_ACK repeatedly for duplicate RUN\_START.

\* FINISH\_ACK eventually arrives and matches, but only after many FINISH attempts.

\* HELLO/HELLO\_ACK are huge: about 184/194 bytes.

\* RepeaterStation display is unclear; it is hard to understand what it is showing.



Root cause:

At SF10, every 100-byte packet takes about 1 second airtime. RUN\_START retry storm, HELLO/HELLO\_ACK, STATUS traffic and repeater echo congest the half-duplex LoRa channel.



Goal:

Reduce LoRa congestion, make RUN\_START\_ACK reliable, keep FINISH\_ACK logic from v0.29, add battery display on all terminals, and improve RepeaterStation OLED readability.



================================================================================

PART 1. Disable or greatly reduce HELLO / HELLO\_ACK

===================================================



HELLO packets must not spam the channel.



Default config:



\* START\_HELLO\_ENABLED\_CFG = 0

\* FINISH\_HELLO\_ENABLED\_CFG = 0

\* REPEATER\_RELAY\_HELLO\_CFG = 0



If HELLO is enabled explicitly for debug:



\* HELLO interval must be at least 120000 ms

\* HELLO\_ACK must be disabled by default

\* HELLO\_ACK may be sent only if HELLO\_ACK\_ENABLED\_CFG = 1

\* HELLO/HELLO\_ACK must not be sent during:



&#x20; \* RUN\_START pending ACK

&#x20; \* FINISH pending ACK

&#x20; \* FINISH\_ACK burst pending

&#x20; \* any priority LoRa exchange



Do not send huge HELLO/HELLO\_ACK packets in normal race mode.



Expected normal logs:



\* no HELLO

\* no HELLO\_ACK

&#x20; unless explicitly enabled by build config.



================================================================================

PART 2. Suppress non-critical TX during priority exchanges

==========================================================



During any priority exchange, suppress non-critical LoRa TX.



Suppress STATUS/HELLO/HELLO\_ACK if:



\* RUN\_START pending ACK

\* FINISH pending ACK

\* FINISH\_ACK burst pending

\* RUN\_START\_ACK was just received/being processed

\* Repeater is currently relaying a critical packet



Allowed critical packets:



\* RUN\_START

\* RUN\_START\_ACK

\* FINISH

\* FINISH\_ACK



STATUS may be sent only when no critical exchange is active.



Log suppression clearly:



\* TX deferred STATUS because RUN\_START pending

\* TX deferred STATUS because FINISH pending ACK

\* TX deferred HELLO because priority exchange active

\* TX deferred HELLO\_ACK because disabled



================================================================================

PART 3. Fix RUN\_START retry timing on StartStation

==================================================



Add RX listen window after every RUN\_START TX.



Constants:



\* RUN\_START\_ACK\_LISTEN\_WINDOW\_MS = 1500UL

\* RUN\_START\_RETRY\_INTERVAL\_MS = 2500UL

\* MAX\_RUN\_START\_ATTEMPTS = 8



After RUN\_START TX:



\* restore RX immediately

\* set runStartAckListenUntilMs = millis() + RUN\_START\_ACK\_LISTEN\_WINDOW\_MS

\* log:

&#x20; RUN\_START ACK listen window until=...



Retry RUN\_START only if:



\* state is Riding / start pending ACK

\* runStartAckReceived == false

\* now >= runStartAckListenUntilMs

\* now - lastRunStartTxMs >= RUN\_START\_RETRY\_INTERVAL\_MS

\* attempts < MAX\_RUN\_START\_ATTEMPTS



Do not enter RUN\_START ACK TIMEOUT until:



\* attempts >= MAX\_RUN\_START\_ATTEMPTS

\* runStartAckReceived == false

\* now >= runStartAckListenUntilMs



Add diagnostics:



\* RUN\_START retry skipped: waiting ACK window

\* RUN\_START retry skipped: retry interval not elapsed

\* RUN\_START ACK timeout delayed until listen window expires



================================================================================

PART 4. StartStation must accept RUN\_START\_ACK robustly

=======================================================



When StartStation receives RUN\_START\_ACK, log:



RUN\_START\_ACK raw payload=...

RUN\_START\_ACK parsed stationId=... src=... dst=... runId=... runNumber=...

RUN\_START\_ACK matched, retry stopped



If dropped, log exact reason:



\* RX drop RUN\_START\_ACK: not for start dst=...

\* RX drop RUN\_START\_ACK: not from finish stationId=... src=...

\* RX drop RUN\_START\_ACK: runId mismatch expected=... got=...

\* RX drop RUN\_START\_ACK: state mismatch state=...

\* RX drop RUN\_START\_ACK: already acked



When valid RUN\_START\_ACK is received:



\* set runStartAckReceived = true

\* stop RUN\_START retry immediately

\* do not send another RUN\_START

\* state remains Riding



================================================================================

PART 5. FinishStation duplicate RUN\_START behavior

==================================================



If FinishStation is already Riding for the same runId:



\* send RUN\_START\_ACK, but rate-limit it

\* do not ACK every duplicate RUN\_START



Add:

RUN\_START\_ACK\_DUPLICATE\_MIN\_INTERVAL\_MS = 2500UL



If duplicate RUN\_START arrives sooner:

log:

Duplicate RUN\_START ACK suppressed by rate limit



If FinishStation is FinishSent or AckTimeout waiting for FINISH\_ACK:



\* do not send RUN\_START\_ACK

\* keep existing behavior:

&#x20; RX type=RUN\_START while state=FinishSent; TX response suppressed waiting FINISH\_ACK



================================================================================

PART 6. Keep FINISH\_ACK logic from v0.29

========================================



Preserve:



\* FinishStation FINISH\_ACK listen window

\* delayed ACK timeout

\* accepting FINISH\_ACK in FinishSent / AckTimeout / Idle if runId matches activeRunId or lastFinishedRunId

\* no result recalculation on resend

\* no clearing last result



FinishStation retry logic:



\* after every FINISH TX, restore RX

\* listen for ACK

\* do not retry during ACK listen window

\* do not enter ACK TIMEOUT until final listen window expires



================================================================================

PART 7. Battery level support on all terminals

==============================================



Add battery reading and display for:



\* StartStation

\* FinishStation

\* RepeaterStation



Important:



\* Do not guess board pins blindly.

\* Inspect existing Heltec WiFi LoRa 32 V3 board definitions, examples, or existing project code.

\* Use the correct battery ADC / voltage divider method for Heltec WiFi LoRa 32 V3.

\* If the board package exposes a battery voltage helper, prefer that.

\* If raw ADC is used, put all constants in one shared config file.



Create shared module:

common/hardware/BatteryMonitor.h

common/hardware/BatteryMonitor.cpp



BatteryMonitor should expose:



\* begin()

\* update(nowMs)

\* voltageMv()

\* percent()

\* isChargingKnown()

\* isValid()



Use smoothing:



\* sample every 2000-5000 ms

\* average/filter readings

\* avoid jumping percentage on OLED



Voltage to percent:

Use a conservative 1S Li-ion/LiPo curve:



\* > = 4200 mV => 100%

\* 4100 mV => 90%

\* 4000 mV => 75%

\* 3900 mV => 60%

\* 3800 mV => 45%

\* 3700 mV => 30%

\* 3600 mV => 15%

\* 3500 mV => 5%

\* <= 3400 mV => 0%



If battery voltage is not valid / USB-only / ADC unavailable:



\* display "BAT: --%"

\* API should return batteryValid=false

\* do not show fake 100%



Battery must be displayed on OLED:

StartStation:



\* Ready/Riding/Finished screens should show battery in a compact form:

&#x20; BAT 87% or BAT --%

\* Do not remove important race info.



FinishStation:



\* Idle/Riding/FinishSent/AckTimeout screens should show:

&#x20; BAT 87% or BAT --%

\* Keep Last result visible.



RepeaterStation:



\* show battery clearly:

&#x20; BAT: 87%  3.92V

&#x20; or BAT: --%



Add battery to lightweight status/debug API where available:



\* /api/status on StartStation

\* /api/debug/status if exists

\* Finish sync-status POST may include finish battery info if easy

&#x20; Do not make LoRa critical packets bigger for battery.

&#x20; Do not add battery to RUN\_START / RUN\_START\_ACK / FINISH / FINISH\_ACK.



Battery may be included in STATUS only if compact:



\* use "bat":87 and optionally "bv":3920

\* do not include long names

\* STATUS still should stay compact and rare



================================================================================

PART 8. Improve RepeaterStation OLED UI

=======================================



Current Repeater OLED is unclear. Redesign it to be readable and self-explanatory.



Use clear labels and stable layout.



Suggested 128x64 layout:



Line 1:

REPEATER v0.31



Line 2:

MODE: RELAY



Line 3:

S:-72 F:-80



Line 4:

RX:123 TX:120 D:3



Line 5:

LAST: FINISH\_ACK



Line 6:

BAT:87% 3.92V



Rules:



\* S = last RSSI from StartStation

\* F = last RSSI from FinishStation

\* RX = received packet count

\* TX = relayed packet count

\* D = duplicate/drop count

\* LAST = last critical packet type:

&#x20; RS, RSA, F, FA, or --

\* BAT = battery percent and voltage if valid



If no signal:



\* S:-- F:--



If battery invalid:



\* BAT:--%



Use readable font.

Avoid cryptic abbreviations except:



\* S

\* F

\* RX

\* TX

\* D

\* BAT



Do not rapidly flicker screen.

Update OLED at low rate:



\* 500 ms or 1000 ms

\* never block critical LoRa handling longer than necessary



Repeater should also log:

REPEATER RX type=... src=... dst=... hop=... rssi=...

REPEATER TX type=... src=... dst=... hop=...

REPEATER DROP duplicate mid=...

REPEATER DROP hop limit mid=...



================================================================================

PART 9. Repeater LoRa behavior

==============================



Repeater must relay critical packets:



\* RUN\_START

\* RUN\_START\_ACK

\* FINISH

\* FINISH\_ACK



Repeater must not relay by default:



\* STATUS

\* HELLO

\* HELLO\_ACK



Config defaults:



\* REPEATER\_RELAY\_STATUS\_CFG = 0

\* REPEATER\_RELAY\_HELLO\_CFG = 0



Do not change original src/dst.

Example:

FINISH\_ACK from start to finish through repeater:

src="s"

dst="f"

via="r"

hop=1



Do not turn src into "r".



Deduplicate by mid with TTL.

Do not relay own src="r".

Drop hop >= mh.



================================================================================

PART 10. Expected behavior

==========================



Start sequence:



\* StartStation sends RUN\_START.

\* FinishStation receives RUN\_START and sends RUN\_START\_ACK.

\* StartStation listens for ACK before retrying.

\* RUN\_START retries should stop after 1-3 attempts, not 15.

\* FinishStation should not receive RUN\_START duplicates during FinishSent.



Finish sequence:



\* FinishStation sends FINISH.

\* StartStation receives FINISH and completes run.

\* StartStation sends FINISH\_ACK burst.

\* FinishStation receives FINISH\_ACK and stops retry.

\* ACK TIMEOUT appears only if no FINISH\_ACK after final listen window.



LoRa channel:



\* No HELLO/HELLO\_ACK in normal race logs.

\* STATUS rare and suppressed during priority exchanges.

\* Critical packets remain compact.

\* Battery is not added to critical packets.



OLED:



\* StartStation shows battery.

\* FinishStation shows battery and last result.

\* RepeaterStation display is understandable without guessing.



Do not change:



\* RaceClock

\* result calculation

\* LoRa frequency/SF/TX power

\* OLED driver type

\* button task

\* RepeaterStation routing semantics

\* compact critical packet format

\* non-blocking LoRa polling

\* USB CDC settings



