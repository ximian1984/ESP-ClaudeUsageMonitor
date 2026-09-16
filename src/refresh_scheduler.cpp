#include "refresh_scheduler.h"

#include "claude_client.h"
#include "config.h"
#include "config_manager.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "usage_parser.h"
#include "wifi_manager.h"

RefreshScheduler refreshScheduler;

// ⚠ [vason merendo] a TLS-kezfogas stackigenye; 12 KB ovatos kezdoertek.
static const uint32_t TASK_STACK = 12288;
static const uint32_t PERIOD_MS = CLAUDE_REFRESH_PERIOD_S * 1000UL;
static const uint32_t PARSER_PENDING_RETRY_MS = 300000UL;

void RefreshScheduler::begin() {
  // Core 0: a Wi-Fi stack magja; a loop()/kijelzo/webszerver a core 1-en fut tovabb.
  xTaskCreatePinnedToCore(taskEntry, "claude_refresh", TASK_STACK, this, 1, nullptr, 0);
}

void RefreshScheduler::taskEntry(void *arg) { static_cast<RefreshScheduler *>(arg)->run(); }

static uint32_t backoffMs(FetchError e, uint16_t failures) {
  switch (e) {
    case FetchError::Auth:
      return CLAUDE_AUTH_BACKOFF_S * 1000UL;  // a szerver x-should-retry: false-t kuld
    case FetchError::ParserPending:
      return PARSER_PENDING_RETRY_MS;
    case FetchError::NotConfigured:
      return PERIOD_MS;
    default: {
      uint32_t ms = PERIOD_MS;
      for (uint16_t i = 1; i < failures && ms < CLAUDE_BACKOFF_MAX_S * 1000UL; i++) ms *= 2;
      return min(ms, (uint32_t)(CLAUDE_BACKOFF_MAX_S * 1000UL));
    }
  }
}

static uint32_t profileIdentity(const ClaudeProfile &c) {
  if (!c.used) return 0;
  // FNV-1a az org+auth-ra — csak osszehasonlitashoz, sehova nem kerul ki
  uint32_t id = 2166136261UL;
  for (const char *s : {c.orgId, "\x1f", c.auth})
    for (const char *p = s; *p; p++) id = (id ^ (uint8_t)*p) * 16777619UL;
  return id;
}

void RefreshScheduler::run() {
  uint32_t seenVersion = 0;
  uint32_t nextDue[MAX_CLAUDE_PROFILES] = {0};
  bool active[MAX_CLAUDE_PROFILES] = {false};
  uint32_t identity[MAX_CLAUDE_PROFILES] = {0};  // ha az org vagy az auth valtozik, a regi cache ervenytelen

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    uint32_t now = millis();

    if (configManager.version() != seenVersion) {
      seenVersion = configManager.version();
      DeviceConfig cfg = configManager.snapshot();
      int n = 0;
      for (int i = 0; i < MAX_CLAUDE_PROFILES; i++)
        if (cfg.claude[i].used && cfg.claude[i].enabled) n++;
      // Csak az UJ vagy MEGVALTOZOTT profil kap uj idopontot; mas konfigvaltozas (pl. rotacio)
      // nem inditja ujra a lekereseket. Az uj profilok a period/N racsra kerulnek.
      int k = 0;
      for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
        const ClaudeProfile &c = cfg.claude[i];
        uint32_t id = profileIdentity(c);
        bool nowActive = c.used && c.enabled;
        if (id != identity[i]) usageCache.clear(i);
        if (nowActive && (!active[i] || id != identity[i])) {
          nextDue[i] = now + 2000 + (uint32_t)k * (PERIOD_MS / (uint32_t)n);
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

    ClaudeResponse resp;
    {
      DeviceConfig cfg = configManager.snapshot();
      resp = fetchUsage(cfg.claude[pick].orgId, cfg.claude[pick].auth);
    }
    _fetchCount++;

    FetchError err = resp.error;
    UsageData data;
    if (err == FetchError::None) err = parseUsage(resp.body, data);
    resp.body = "";  // felszabaditas

    // Kozben modosult/torolt profil: az eredmeny mar nem ehhez a profilhoz tartozik.
    if (identity[pick] != profileIdentity(configManager.snapshot().claude[pick])) continue;

    uint32_t done = millis();
    if (err == FetchError::None) {
      usageCache.storeSuccess(pick, data, timeManager.now());
      nextDue[pick] = nextDue[pick] + PERIOD_MS;
      if ((int32_t)(done - nextDue[pick]) >= 0) nextDue[pick] = done + PERIOD_MS;  // lemaradas utan ne zuduljon
    } else {
      usageCache.storeError(pick, err, resp.httpStatus);
      uint16_t fails = usageCache.get(pick).consecutiveFailures;
      nextDue[pick] = done + backoffMs(err, fails);
    }
  }
}
