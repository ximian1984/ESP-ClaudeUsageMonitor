#include "usage_cache.h"

UsageCache usageCache;

void UsageCache::begin() { _mtx = xSemaphoreCreateMutex(); }

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
  xSemaphoreGive(_mtx);
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
