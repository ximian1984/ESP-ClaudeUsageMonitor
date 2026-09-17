#include "token_cache.h"

TokenCache tokenCache;

void TokenCache::begin() { _mtx = xSemaphoreCreateMutex(); }

bool TokenCache::get(int idx, String &out, time_t now, uint32_t marginS) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  bool ok = _tok[idx][0] && _exp[idx] > now + (time_t)marginS;
  if (ok) out = _tok[idx];
  xSemaphoreGive(_mtx);
  return ok;
}

void TokenCache::set(int idx, const String &access, time_t expiresAt) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  if (access.length() <= PROVIDER_ACCESS_MAX) {
    strlcpy(_tok[idx], access.c_str(), sizeof(_tok[idx]));
    _exp[idx] = expiresAt;
  } else {
    memset(_tok[idx], 0, sizeof(_tok[idx]));  // tul hosszu: nem vagjuk csonkra (hasznalhatatlan lenne)
    _exp[idx] = 0;
  }
  xSemaphoreGive(_mtx);
  if (access.length() > PROVIDER_ACCESS_MAX)
    Serial.printf("[token] profil %d: access token %u B > %u, nem tarolhato\n", idx, (unsigned)access.length(),
                  (unsigned)PROVIDER_ACCESS_MAX);
}

void TokenCache::clear(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  memset(_tok[idx], 0, sizeof(_tok[idx]));
  _exp[idx] = 0;
  xSemaphoreGive(_mtx);
}

bool TokenCache::has(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  bool h = _tok[idx][0] != 0;
  xSemaphoreGive(_mtx);
  return h;
}
