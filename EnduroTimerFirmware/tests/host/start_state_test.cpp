#include <cassert>
#include <iostream>
#include "StartState.h"
#include "RaceClock.h"
String formatSeconds(uint32_t value) { return String(value); }
void countdown(StartState& s) {
  String error;
  assert(s.startCountdown(1, "", 0, "r", "Rider", "t", "Trail", error));
}
void go(StartState& s) {
  for (int i = 0; i < 3; ++i) { fakeMillis += 1000; s.updateCountdown(fakeMillis); }
  assert(s.state() == StartRunState::WaitingStartGate);
  assert(s.currentRun().raceStartTimeMs == 0);
  assert(s.currentRun().startTimestampMs == 0);
}
int main() {
  StartState s; String error; RunRecord run;
  assert(!s.cancelPendingStart(error));
  assert(!s.startRidingFromGate(42, 1, run));
  s.begin();
  assert(s.cancelPendingStart(error));
  assert(!s.startRidingFromGate(42, 1, run));
  fakeMillis = 100;
  countdown(s);
  assert(!s.startRidingFromGate(42, 1, run));
  assert(s.cancelPendingStart(error));
  assert(s.currentRun().runId.empty() && s.runs().empty());
  s.updateCountdown(10000);
  assert(s.state() == StartRunState::Ready);
  countdown(s); go(s);
  assert(s.cancelPendingStart(error));
  assert(s.goTimestampMs() == 0 && s.countdownStartedMs() == 0);
  assert(s.currentRun().runId.empty() && s.runs().empty());
  countdown(s); go(s);
  fakeMillis += 10000;
  s.updateCountdown(fakeMillis);
  assert(s.state() == StartRunState::WaitingStartGate);
  RaceClock clock; clock.setOffsetToMaster(25);
  const uint32_t captured = fakeMillis;
  fakeMillis += 70; // debounce + queue latency must not affect start time
  const uint32_t raceStart = clock.raceMsFromLocalMillis(captured);
  assert(s.startRidingFromGate(raceStart, 2, run));
  assert(run.raceStartTimeMs == captured + 25 && run.startTimestampMs == raceStart);
  assert(run.status == "Riding");
  assert(!s.startRidingFromGate(raceStart + 500, 2, run));
  assert(s.currentRun().raceStartTimeMs == raceStart);
  assert(!s.cancelPendingStart(error));
  assert(error == "Race already started; use DNF/cancel run flow instead");
  RunRecord completed;
  assert(s.completeRunSynced(run.runId, raceStart + 1234, 1234, "FINISH", 2, completed));
  assert(completed.resultMs == 1234 && s.runs().size() == 1);
  assert(!s.cancelPendingStart(error));
  assert(!s.startRidingFromGate(100, 1, run));
  s.tickAutoReady(fakeMillis + 8000);
  assert(s.state() == StartRunState::Ready);
  fakeMillis = UINT32_MAX - 1500; countdown(s); go(s);
  assert(s.cancelPendingStart(error));
  s.setError(); assert(!s.cancelPendingStart(error));
  std::cout << "PASS: cancellation, gate-only timing, duplicate gate, completion, countdown wraparound\n";
}
