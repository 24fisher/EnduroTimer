#pragma once

#include <Arduino.h>

class RaceClock {
public:
  void begin();
  uint32_t localMillis() const;
  bool isSynced() const;
  int32_t offsetToMasterMs() const;
  uint32_t nowRaceMs() const;
  void setOffsetToMaster(int32_t offsetMs);
  void markSynced();
  void clearSync();

private:
  bool synced_ = false;
  int32_t offsetToMasterMs_ = 0;
};
