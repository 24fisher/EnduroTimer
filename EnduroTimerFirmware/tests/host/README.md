# Host regression tests

Run from EnduroTimerFirmware. No board is needed. The C++ test compiles the real
StartState and RaceClock sources against a minimal Arduino stub.

With a C++17 compiler:

```sh
g++ -std=c++17 -Itests/host/stubs -Istart-station/src -Icommon/time tests/host/start_state_test.cpp start-station/src/StartState.cpp common/time/RaceClock.cpp -o /tmp/start-state-test
/tmp/start-state-test
node tests/host/ui-test.cjs start-station/data/app.js
```

MSVC also works: use an x64 Native Tools prompt and replace g++ with
`cl /EHsc /std:c++17`, `-I` with `/I`, and `-o ...` with `/Fe:...`.
Keep generated executables/objects outside the source tree.

Covers cancellation in all states, gate-only timestamps, duplicate gates,
completion, countdown through millis wrap, UI button visibility, duplicate POST
suppression, and HTTP 409 display. Hardware tests in README.md remain required
for GPIO debounce, radio delivery, and OLED output.
