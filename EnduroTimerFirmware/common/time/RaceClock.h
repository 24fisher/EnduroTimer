#pragma once

#include <Arduino.h>

class RaceClock {
public:
  void begin();
  uint32_t localMillis() const;
  bool isSynced() const;
  uint32_t nowRaceMs() const;
  int32_t offsetToMasterMs() const;
  void setOffsetToMaster(int32_t offsetMs);
  void markSynced(uint32_t accuracyMs = 0);
  void clearSync();
  uint32_t syncAccuracyMs() const;

private:
  bool synced_ = false;
  int32_t offsetToMasterMs_ = 0;
  uint32_t syncAccuracyMs_ = 0;
};
