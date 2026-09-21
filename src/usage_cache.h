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

// Utolso ismert keret-reset idopontok, NVS-ben (ujrainditas es Wi-Fi nelkul is megmutathato; projektgazda, 2026-09-17).
// Csak idopontok (UTC epoch), titok nincs benne. 0 = ismeretlen.
struct LastKnownResets {
  time_t sessionReset = 0;
  time_t weeklyReset = 0;
  time_t savedEpoch = 0;  // az utolso SIKERES lekeres ideje (minden sikernel NVS-be kerul)
  bool any() const { return sessionReset || weeklyReset; }
};

class UsageCache {
 public:
  void begin();  // a mentett LastKnownResets betoltese
  LastKnownResets lastKnown(int idx);
  void forgetLastKnown(int idx);  // profil mentese/torlese a setup-oldalon
  ProfileUsage get(int idx);
  void storeSuccess(int idx, const UsageData &d, time_t epochNow);
  void storeError(int idx, FetchError e, int httpStatus);
  void clear(int idx);

 private:
  ProfileUsage _p[MAX_CLAUDE_PROFILES];
  LastKnownResets _lk[MAX_CLAUDE_PROFILES];
  SemaphoreHandle_t _mtx = nullptr;
};

extern UsageCache usageCache;
