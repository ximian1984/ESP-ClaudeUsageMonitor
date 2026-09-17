#include "refresh_scheduler.h"

#include <algorithm>

#include "claude_client.h"
#include "config.h"
#include "config_manager.h"
#include "oauth_client.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "usage_parser.h"
#include "wifi_manager.h"

RefreshScheduler refreshScheduler;

// ⚠ [vason merendo] a TLS-kezfogas stackigenye; a token-frissites es a usage-lekeres kulon TLS-kapcsolat.
// 16 KB: a run() DeviceConfig-masolatot (~4,7 KB) tart a stacken a TLS-hivasok alatt (a loopTask 8 KB-ja emiatt
// tulcsordult, PLAN 2.11). A tenyleges maradekot a fetch utani naplo mutatja (stack HWM).
static const uint32_t TASK_STACK = 16384;
static const uint32_t PARSER_PENDING_RETRY_MS = 300000UL;

void RefreshScheduler::begin() {
  // Core 0: a Wi-Fi stack magja; a loop()/kijelzo/webszerver a core 1-en fut tovabb.
  xTaskCreatePinnedToCore(taskEntry, "claude_refresh", TASK_STACK, this, 1, nullptr, 0);
}

void RefreshScheduler::taskEntry(void *arg) { static_cast<RefreshScheduler *>(arg)->run(); }

static uint32_t backoffMs(FetchError e, uint16_t failures, uint32_t periodMs) {
  switch (e) {
    case FetchError::Auth:
      return CLAUDE_AUTH_BACKOFF_S * 1000UL;  // a szerver x-should-retry: false-t kuld
    case FetchError::ReloginRequired:
      return CLAUDE_AUTH_BACKOFF_S * 1000UL;  // a felhasznalonak kell belepnie; ritkan probalunk
    case FetchError::RefreshFailed:
      return OAUTH_REFRESH_RETRY_S * 1000UL;
    case FetchError::ParserPending:
      return PARSER_PENDING_RETRY_MS;
    case FetchError::NotConfigured:
      return periodMs;
    default: {
      uint32_t ms = periodMs;
      for (uint16_t i = 1; i < failures && ms < CLAUDE_BACKOFF_MAX_S * 1000UL; i++) ms *= 2;
      return min(ms, (uint32_t)(CLAUDE_BACKOFF_MAX_S * 1000UL));
    }
  }
}

static uint32_t profileIdentity(const ClaudeProfile &c) {
  if (!c.used) return 0;
  // FNV-1a a transport+org+auth-ra — csak osszehasonlitashoz, sehova nem kerul ki.
  // A refresh tokent NEM vesszuk bele: a token-rotacio ne szamitson uj profilnak (kulonben nullaznank a cache-t).
  uint32_t id = 2166136261UL;
  id = (id ^ c.transport) * 16777619UL;
  for (const char *s : {c.orgId, "\x1f", c.auth})
    for (const char *p = s; *p; p++) id = (id ^ (uint8_t)*p) * 16777619UL;
  return id;
}

// OAuth-profilnal a lekeres ELOTT gondoskodik ervenyes access tokenrol (auto-refresh).
// Visszaad: None ha a c.auth mostantol hasznalhato; kulonben a jelzendo hiba.
// A refreshelt tokent config_manager.saveOAuthTokens irja (editSeq-vedelemmel); a c snapshot NEM frissul,
// ezert siker eseten az uj access tokent az outAccess-be adja vissza a hivonak.
static FetchError ensureOAuthToken(int idx, const ClaudeProfile &c, String &outAccess) {
  outAccess = c.auth;
  if (c.transport != (uint8_t)ClaudeTransport::OAuth) return FetchError::None;

  time_t nowEpoch = timeManager.now();
  bool haveAccess = c.auth[0] != '\0';
  bool haveRefresh = c.refresh[0] != '\0';
  bool expiring = c.expiresAt != 0 && nowEpoch + OAUTH_REFRESH_MARGIN_S >= (time_t)c.expiresAt;

  if (haveAccess && !expiring) return FetchError::None;  // van ervenyes access token
  if (!haveRefresh) return haveAccess ? FetchError::None : FetchError::NotConfigured;

  OAuthTokens t = oauthRefresh(c.refresh, c.scope);
  if (!t.ok) {
    if (t.invalidGrant) return FetchError::ReloginRequired;  // a refresh token vegleg elhalt
    return haveAccess && !expiring ? FetchError::None : FetchError::RefreshFailed;
  }
  uint32_t newExpiry = t.expiresIn ? (uint32_t)(nowEpoch + t.expiresIn) : 0;
  // ELOSZOR perzisztal (a rotalt refresh tokennel egyutt), csak utana hasznaljuk.
  configManager.saveOAuthTokens(idx, c.editSeq, t.access.c_str(), t.refresh.c_str(), newExpiry, t.scope.c_str());
  outAccess = t.access;
  return FetchError::None;
}

void RefreshScheduler::run() {
  uint32_t seenVersion = 0;
  uint32_t nextDue[MAX_CLAUDE_PROFILES] = {0};
  bool active[MAX_CLAUDE_PROFILES] = {false};
  uint32_t identity[MAX_CLAUDE_PROFILES] = {0};  // ha az org vagy az auth valtozik, a regi cache ervenytelen
  uint32_t periodMs = CLAUDE_REFRESH_DEFAULT_S * 1000UL;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    uint32_t now = millis();

    if (configManager.version() != seenVersion) {
      seenVersion = configManager.version();
      DeviceConfig cfg = configManager.snapshot();
      periodMs = (uint32_t)cfg.refreshSec * 1000UL;
      int n = 0;
      for (int i = 0; i < MAX_CLAUDE_PROFILES; i++)
        if (cfg.claude[i].used && cfg.claude[i].enabled) n++;
      // Csak az UJ vagy MEGVALTOZOTT profil kap uj idopontot; mas konfigvaltozas (pl. rotacio,
      // token-rotacio) nem inditja ujra a lekereseket. Az uj profilok a period/N racsra kerulnek.
      int k = 0;
      for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
        const ClaudeProfile &c = cfg.claude[i];
        uint32_t id = profileIdentity(c);
        bool nowActive = c.used && c.enabled;
        if (id != identity[i]) usageCache.clear(i);
        if (nowActive && (!active[i] || id != identity[i])) {
          nextDue[i] = now + 2000 + (uint32_t)k * (periodMs / (uint32_t)n);
        }
        if (nowActive) k++;
        identity[i] = id;
        active[i] = nowActive;
      }
    }

    if (!wifiManager.staConnected() || !timeManager.synced()) continue;

    // A legregebben esedekes profil (millis-atfordulas-biztos kulonbseggel).
    int pick = -1;
    uint32_t maxLate = 0;
    for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
      if (!active[i]) continue;
      int32_t late = (int32_t)(now - nextDue[i]);
      if (late >= 0 && (pick < 0 || (uint32_t)late > maxLate)) {
        pick = i;
        maxLate = late;
      }
    }
    if (pick < 0) continue;

    FetchError err;
    ClaudeResponse resp;
    {
      DeviceConfig cfg = configManager.snapshot();
      const ClaudeProfile &c = cfg.claude[pick];
      String access;
      err = ensureOAuthToken(pick, c, access);  // OAuth: auto-refresh a lekeres elott
      if (err == FetchError::None) {
        resp = fetchUsage((ClaudeTransport)c.transport, c.orgId, access.c_str());
        _fetchCount++;
        // OAuth + 401/403: hatha eppen most jart le -> egyszeri refresh + ujra.
        if (resp.error == FetchError::Auth && c.transport == (uint8_t)ClaudeTransport::OAuth && c.refresh[0]) {
          OAuthTokens t = oauthRefresh(c.refresh, c.scope);
          if (t.ok) {
            uint32_t exp = t.expiresIn ? (uint32_t)(timeManager.now() + t.expiresIn) : 0;
            configManager.saveOAuthTokens(pick, c.editSeq, t.access.c_str(), t.refresh.c_str(), exp, t.scope.c_str());
            resp = fetchUsage(ClaudeTransport::OAuth, c.orgId, t.access.c_str());
            _fetchCount++;
          } else if (t.invalidGrant) {
            resp.error = FetchError::ReloginRequired;
          }
        }
        err = resp.error;
        Serial.printf("[sched] stack HWM %u B, heap szabad %u B (min %u B)\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
      }
    }

    UsageData data;
    if (err == FetchError::None) err = parseUsage(resp.body, data);
    resp.body = "";  // felszabaditas

    // Kozben modosult/torolt profil: az eredmeny mar nem ehhez a profilhoz tartozik.
    if (identity[pick] != profileIdentity(configManager.snapshot().claude[pick])) continue;

    uint32_t done = millis();
    if (err == FetchError::None) {
      usageCache.storeSuccess(pick, data, timeManager.now());
      nextDue[pick] = nextDue[pick] + periodMs;
      if ((int32_t)(done - nextDue[pick]) >= 0) nextDue[pick] = done + periodMs;  // lemaradas utan ne zuduljon
    } else {
      usageCache.storeError(pick, err, resp.httpStatus);
      uint16_t fails = usageCache.get(pick).consecutiveFailures;
      uint32_t wait = backoffMs(err, fails, periodMs);
      // 429 + Retry-After: a szerver kerese elsobbseget kap, legfeljebb 1 oraig.
      if (err == FetchError::RateLimited && resp.retryAfterS > 0)
        wait = std::max(wait, std::min(resp.retryAfterS, (uint32_t)3600) * (uint32_t)1000);
      nextDue[pick] = done + wait;
    }
  }
}
