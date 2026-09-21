#include "usage_cache.h"

#include <Preferences.h>

UsageCache usageCache;

// Kulon NVS-namespace, hogy a konfig ("cmon") es a gyakrabban irt usage-adat ne keveredjen.
static const char *NS_LK = "cmonlk";
// Csak akkor irunk, ha az idopont ennyinel tobbet valtozott (a resets_at tort masodperce ne okozzon irast).
static const long LK_WRITE_MIN_DELTA_S = 60;

static String lkKey(int idx, char f) { return String("u") + idx + f; }

void UsageCache::begin() {
  _mtx = xSemaphoreCreateMutex();
  Preferences p;
  if (!p.begin(NS_LK, true)) return;  // meg nem letezik (elso inditas)
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    _lk[i].sessionReset = (time_t)p.getULong64(lkKey(i, 's').c_str(), 0);
    _lk[i].weeklyReset = (time_t)p.getULong64(lkKey(i, 'w').c_str(), 0);
    _lk[i].savedEpoch = (time_t)p.getULong64(lkKey(i, 't').c_str(), 0);
  }
  p.end();
}

LastKnownResets UsageCache::lastKnown(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return LastKnownResets();
  xSemaphoreTake(_mtx, portMAX_DELAY);
  LastKnownResets c = _lk[idx];
  xSemaphoreGive(_mtx);
  return c;
}

void UsageCache::forgetLastKnown(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  _lk[idx] = LastKnownResets();
  xSemaphoreGive(_mtx);
  Preferences p;
  if (!p.begin(NS_LK, false)) return;
  p.remove(lkKey(idx, 's').c_str());
  p.remove(lkKey(idx, 'w').c_str());
  p.remove(lkKey(idx, 't').c_str());
  p.end();
}

static bool differs(time_t a, time_t b) { return labs((long)(a - b)) > LK_WRITE_MIN_DELTA_S; }

ProfileUsage UsageCache::get(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return ProfileUsage();
  xSemaphoreTake(_mtx, portMAX_DELAY);
  ProfileUsage copy = _p[idx];
  xSemaphoreGive(_mtx);
  return copy;
}

void UsageCache::storeSuccess(int idx, const UsageData &d, time_t epochNow) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  ProfileUsage &p = _p[idx];
  p.hasData = true;
  p.data = d;
  p.lastOkMs = millis();
  p.lastOkEpoch = epochNow;
  p.lastError = FetchError::None;
  p.lastHttpStatus = 200;
  p.lastAttemptMs = p.lastOkMs;
  p.consecutiveFailures = 0;

  LastKnownResets nk;
  const UsageLimit *ls = d.find(LimitKind::Session);
  const UsageLimit *lw = d.find(LimitKind::Weekly);
  nk.sessionReset = (ls && ls->hasReset) ? ls->resetAt : 0;
  nk.weeklyReset = (lw && lw->hasReset) ? lw->resetAt : 0;
  nk.savedEpoch = epochNow;
  bool write = differs(nk.sessionReset, _lk[idx].sessionReset) || differs(nk.weeklyReset, _lk[idx].weeklyReset);
  _lk[idx] = nk;  // RAM-ban mindig friss
  xSemaphoreGive(_mtx);

  // A resetek csak valtozaskor mennek NVS-be; az utolso SIKER ideje ('t') minden sikernel (projektgazda, 2026-09-21:
  // a kijelzon ujrainditas utan is latszodjon, mikor sikerult utoljara). 3 percenkent egy 8 bajtos bejegyzes: az NVS
  // kopasa ettol elhanyagolhato (~20 iras/ora, a 20 KB-os particio lapjai korbeforognak).
  Preferences pr;
  if ((write || epochNow >= 1704067200) && pr.begin(NS_LK, false)) {
    if (write) {
      pr.putULong64(lkKey(idx, 's').c_str(), (uint64_t)nk.sessionReset);
      pr.putULong64(lkKey(idx, 'w').c_str(), (uint64_t)nk.weeklyReset);
      Serial.printf("[cache] profil %d: utolso ismert reset NVS-be mentve\n", idx);
    }
    pr.putULong64(lkKey(idx, 't').c_str(), (uint64_t)nk.savedEpoch);
    pr.end();
  }
}

void UsageCache::storeError(int idx, FetchError e, int httpStatus) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  ProfileUsage &p = _p[idx];
  p.lastError = e;  // az utolso ervenyes adat (p.data) megmarad — spec 18.
  p.lastHttpStatus = httpStatus;
  p.lastAttemptMs = millis();
  if (p.consecutiveFailures < UINT16_MAX) p.consecutiveFailures++;
  xSemaphoreGive(_mtx);
}

void UsageCache::clear(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  _p[idx] = ProfileUsage();
  xSemaphoreGive(_mtx);
}
