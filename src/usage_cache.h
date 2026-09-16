// Profilonkenti usage-cache (spec 13., 18.). A kijelzo csak innen olvas, API-t soha nem hiv.
#pragma once
#include <Arduino.h>

#include "config.h"
#include "usage_types.h"

struct ProfileUsage {
  bool hasData = false;       // volt-e mar ervenyes adat
  UsageData data;             // az utolso ERVENYES adat — hiba nem irja felul
  uint32_t lastOkMs = 0;      // millis() az utolso sikernel (adat kora; NTP nelkul is mukodik)
  time_t lastOkEpoch = 0;     // ha volt NTP-ido
  FetchError lastError = FetchError::None;
  int lastHttpStatus = 0;
  uint32_t lastAttemptMs = 0;
  uint16_t consecutiveFailures = 0;
};

class UsageCache {
 public:
  void begin();
  ProfileUsage get(int idx);
  void storeSuccess(int idx, const UsageData &d, time_t epochNow);
  void storeError(int idx, FetchError e, int httpStatus);
  void clear(int idx);

 private:
  ProfileUsage _p[MAX_CLAUDE_PROFILES];
  SemaphoreHandle_t _mtx = nullptr;
};

extern UsageCache usageCache;
