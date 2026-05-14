#include "RaceClock.h"

void RaceClock::begin() {
  clearSync();
}

uint32_t RaceClock::localMillis() const {
  return millis();
}

bool RaceClock::isSynced() const {
  return synced_;
}

int32_t RaceClock::offsetToMasterMs() const {
  return offsetToMasterMs_;
}

uint32_t RaceClock::nowRaceMs() const {
  return static_cast<uint32_t>(static_cast<int64_t>(millis()) + static_cast<int64_t>(offsetToMasterMs_));
}

void RaceClock::setOffsetToMaster(int32_t offsetMs) {
  offsetToMasterMs_ = offsetMs;
}

void RaceClock::markSynced() {
  synced_ = true;
}

void RaceClock::clearSync() {
  synced_ = false;
  offsetToMasterMs_ = 0;
}
