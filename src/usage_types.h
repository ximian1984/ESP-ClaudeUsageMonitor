// A megjelenites altal hasznalt, API-fuggetlen usage-adatmodell.
// A usage_parser tolti ki a valos valaszbol; a mezok jelentese itt NEM a Claude JSON-mezoinek neve.
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <time.h>

#include "config.h"

enum class LimitKind : uint8_t {
  Session,  // 5 oras / session limit (spec 4.)
  Weekly,   // 7 napos limit (spec 4.)
  Other,    // barmi mas, ha a valasz tobbet ad (bovithetoseg)
};

// A valos valasz limits[].severity erteke. Mert ertekek (2026-09-16 minta): "normal", "warning".
// Minden mas string Unknown — nem talalgatjuk, mit jelent.
enum class Severity : uint8_t { None, Normal, Warning, Unknown };

struct UsageLimit {
  LimitKind kind = LimitKind::Other;
  char label[12] = "";          // kijelzore: "SESSION", "WEEKLY", ...
  bool hasUtilization = false;
  float utilizationPct = 0;     // 0..100 (mert: utilization/percent 0..100 skala)
  bool hasReset = false;
  time_t resetAt = 0;           // UTC epoch
  Severity severity = Severity::None;  // csak limits[]-bol
  bool isActive = false;        // limits[].is_active — jelentese ⚠ [feltarando], csak tarolva
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
  CfChallenge,     // Cloudflare kihivas (cf-mitigated: challenge) — a claude.ai sessionKey-uton mert, tervdoksi 2.1;
                   // az OAuth-ut hosztjai (api.anthropic.com, platform.claude.com) nem adnak kihivast
  Auth,            // 401/403 JSON hibaval
  RateLimited,     // 429
  Http,            // egyeb nem-2xx
  TooLarge,        // valasz > CLAUDE_MAX_BODY_BYTES
  Parse,           // hibas/ismeretlen JSON
  ParserPending,   // a parser meg nincs kesz (valos mintara var)
  NotConfigured,   // hianyzik org-id vagy auth
  ReloginRequired, // OAuth: a refresh token vegleg ervenytelen -> a felhasznalonak ujra be kell lepnie
  RefreshFailed,   // OAuth: a token-frissites atmenetileg nem sikerult
};

const char *fetchErrorTitle(FetchError e);  // kijelzo 1. sor, pl. "CLAUDE AUTH"

// Keret-cimke szine a MARADEK szazalek szerint, folyamatos atmenettel (projektgazda, 2026-09-18):
// 100 % -> zold, 50 % -> sarga, 0 % -> piros. RGB565 (a TFT_eSPI szinformatuma), host-tesztelheto.
uint16_t usageColor565(float leftPct);
