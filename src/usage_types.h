// A megjelenites altal hasznalt, API-fuggetlen usage-adatmodell.
// A usage_parser tolti ki a valos valaszbol; a mezok jelentese itt NEM a Claude JSON-mezoinek neve.
#pragma once
#include <Arduino.h>
#include <time.h>

#include "config.h"

enum class LimitKind : uint8_t {
  Session,  // 5 oras / session limit (spec 4.)
  Weekly,   // 7 napos limit (spec 4.)
  Other,    // barmi mas, ha a valasz tobbet ad (bovithetoseg)
};

struct UsageLimit {
  LimitKind kind = LimitKind::Other;
  char label[12] = "";          // kijelzore: "SESSION", "WEEKLY", ...
  bool hasUtilization = false;
  float utilizationPct = 0;     // 0..100 — a parser normalizal, ha a valasz mas skalat hasznal
  bool hasReset = false;
  time_t resetAt = 0;           // UTC epoch
};

static const int MAX_LIMITS = 6;

struct UsageData {
  UsageLimit limits[MAX_LIMITS];
  uint8_t count = 0;

  const UsageLimit *find(LimitKind k) const {
    for (int i = 0; i < count; i++)
      if (limits[i].kind == k) return &limits[i];
    return nullptr;
  }
};

enum class FetchError : uint8_t {
  None,
  NoWifi,          // nincs STA kapcsolat
  NoTime,          // nincs NTP-ido (TLS-ervenyesseghez kell)
  Connect,         // DNS/TCP/TLS hiba ("internet unavailable")
  Timeout,
  CfChallenge,     // Cloudflare kihivas (cf-mitigated: challenge) — mert jelenseg, PLAN.md 2.1
  Auth,            // 401/403 JSON hibaval
  RateLimited,     // 429
  Http,            // egyeb nem-2xx
  TooLarge,        // valasz > CLAUDE_MAX_BODY_BYTES
  Parse,           // hibas/ismeretlen JSON
  ParserPending,   // a parser meg nincs kesz (valos mintara var)
  NotConfigured,   // hianyzik org-id vagy auth
};

const char *fetchErrorTitle(FetchError e);  // kijelzo 1. sor, pl. "CLAUDE AUTH"
