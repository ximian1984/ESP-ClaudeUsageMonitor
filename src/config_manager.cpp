#include "config_manager.h"

#include <Preferences.h>
#include <esp_random.h>

// NVS: namespace "cmon", kulcsok <=15 karakter (NVS-korlat).
// A semaversio a jovobeli migraciohoz kell; firmware-frissites az NVS-t nem torli (spec 22.).
static const char *NS = "cmon";
static const uint16_t SCHEMA_VERSION = 1;

ConfigManager configManager;

static String key(char kind, int idx, const char *field) {
  return String(kind) + String(idx) + field;  // pl. "w0ssid", "c3auth"
}

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : _m(m) { xSemaphoreTake(_m, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(_m); }
 private:
  SemaphoreHandle_t _m;
};

static void generateApPassword(char *out, size_t outLen) {
  // Nem-osszekeverheto karakterek (nincs 0/O, 1/l/I). 10 karakter, WPA2 min. 8.
  static const char ALPHA[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  size_t n = 10;
  if (n > outLen - 1) n = outLen - 1;
  for (size_t i = 0; i < n; i++) out[i] = ALPHA[esp_random() % (sizeof(ALPHA) - 1)];
  out[n] = '\0';
}

void ConfigManager::begin() {
  _mtx = xSemaphoreCreateMutex();
  load();
}

void ConfigManager::load() {
  Lock l(_mtx);
  Preferences p;
  p.begin(NS, false);
  uint16_t schema = p.getUShort("schema", 0);
  if (schema == 0) p.putUShort("schema", SCHEMA_VERSION);

  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    WifiProfile &w = _cfg.wifi[i];
    w.used = p.getBool(key('w', i, "used").c_str(), false);
    if (!w.used) continue;
    w.enabled = p.getBool(key('w', i, "en").c_str(), true);
    w.priority = p.getShort(key('w', i, "pri").c_str(), 0);
    p.getString(key('w', i, "ssid").c_str(), w.ssid, sizeof(w.ssid));
    p.getString(key('w', i, "pass").c_str(), w.password, sizeof(w.password));
  }
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    ClaudeProfile &c = _cfg.claude[i];
    c.used = p.getBool(key('c', i, "used").c_str(), false);
    if (!c.used) continue;
    c.enabled = p.getBool(key('c', i, "en").c_str(), true);
    c.transport = p.getUChar(key('c', i, "tr").c_str(), (uint8_t)ClaudeTransport::WebSession);
    if (c.transport >= CLAUDE_TRANSPORT_COUNT) c.transport = (uint8_t)ClaudeTransport::WebSession;
    p.getString(key('c', i, "name").c_str(), c.name, sizeof(c.name));
    p.getString(key('c', i, "org").c_str(), c.orgId, sizeof(c.orgId));
    p.getString(key('c', i, "auth").c_str(), c.auth, sizeof(c.auth));
    p.getString(key('c', i, "rt").c_str(), c.refresh, sizeof(c.refresh));
    p.getString(key('c', i, "sc").c_str(), c.scope, sizeof(c.scope));
    c.expiresAt = p.getUInt(key('c', i, "exp").c_str(), 0);
    c.editSeq = ++_editCounter;
  }
  _cfg.rotationSec = constrain(p.getUChar("rot", ROTATION_DEFAULT_S), ROTATION_MIN_S, ROTATION_MAX_S);
  _cfg.refreshSec = constrain((int)p.getUShort("refr", CLAUDE_REFRESH_DEFAULT_S), REFRESH_PERIOD_MIN_S, REFRESH_PERIOD_MAX_S);

  if (p.getString("appass", _cfg.apPassword, sizeof(_cfg.apPassword)) == 0 || strlen(_cfg.apPassword) < 8) {
    generateApPassword(_cfg.apPassword, sizeof(_cfg.apPassword));
    p.putString("appass", _cfg.apPassword);
  }
  p.end();
  // Szandekosan nincs Serial-log a tartalomrol (spec 19.).
}

DeviceConfig ConfigManager::snapshot() {
  Lock l(_mtx);
  return _cfg;
}

void ConfigManager::writeWifi(int i) {
  const WifiProfile &w = _cfg.wifi[i];
  Preferences p;
  p.begin(NS, false);
  if (w.used) {
    p.putBool(key('w', i, "used").c_str(), true);
    p.putBool(key('w', i, "en").c_str(), w.enabled);
    p.putShort(key('w', i, "pri").c_str(), w.priority);
    p.putString(key('w', i, "ssid").c_str(), w.ssid);
    p.putString(key('w', i, "pass").c_str(), w.password);
  } else {
    for (const char *f : {"used", "en", "pri", "ssid", "pass"}) p.remove(key('w', i, f).c_str());
  }
  p.end();
}

void ConfigManager::writeClaude(int i) {
  const ClaudeProfile &c = _cfg.claude[i];
  Preferences p;
  p.begin(NS, false);
  if (c.used) {
    p.putBool(key('c', i, "used").c_str(), true);
    p.putBool(key('c', i, "en").c_str(), c.enabled);
    p.putUChar(key('c', i, "tr").c_str(), c.transport);
    p.putString(key('c', i, "name").c_str(), c.name);
    p.putString(key('c', i, "org").c_str(), c.orgId);
    p.putString(key('c', i, "auth").c_str(), c.auth);
    p.putString(key('c', i, "rt").c_str(), c.refresh);
    p.putString(key('c', i, "sc").c_str(), c.scope);
    p.putUInt(key('c', i, "exp").c_str(), c.expiresAt);
  } else {
    for (const char *f : {"used", "en", "tr", "name", "org", "auth", "rt", "sc", "exp"}) p.remove(key('c', i, f).c_str());
  }
  p.end();
}

bool ConfigManager::saveWifi(int idx, const WifiProfile &w) {
  if (idx < 0 || idx >= MAX_WIFI_PROFILES) return false;
  Lock l(_mtx);
  _cfg.wifi[idx] = w;
  _cfg.wifi[idx].used = true;
  writeWifi(idx);
  _version++;
  return true;
}

bool ConfigManager::deleteWifi(int idx) {
  if (idx < 0 || idx >= MAX_WIFI_PROFILES) return false;
  Lock l(_mtx);
  _cfg.wifi[idx] = WifiProfile();
  writeWifi(idx);
  _version++;
  return true;
}

bool ConfigManager::saveClaude(int idx, const ClaudeProfile &c) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return false;
  Lock l(_mtx);
  _cfg.claude[idx] = c;
  _cfg.claude[idx].used = true;
  _cfg.claude[idx].editSeq = ++_editCounter;
  writeClaude(idx);
  _version++;
  return true;
}

bool ConfigManager::deleteClaude(int idx) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return false;
  Lock l(_mtx);
  _cfg.claude[idx] = ClaudeProfile();
  _cfg.claude[idx].editSeq = ++_editCounter;
  writeClaude(idx);
  _version++;
  return true;
}

bool ConfigManager::saveRotation(uint8_t sec) {
  if (sec < ROTATION_MIN_S || sec > ROTATION_MAX_S) return false;
  Lock l(_mtx);
  _cfg.rotationSec = sec;
  Preferences p;
  p.begin(NS, false);
  p.putUChar("rot", sec);
  p.end();
  _version++;
  return true;
}

bool ConfigManager::saveRefreshSec(uint16_t sec) {
  if (sec < REFRESH_PERIOD_MIN_S || sec > REFRESH_PERIOD_MAX_S) return false;
  Lock l(_mtx);
  _cfg.refreshSec = sec;
  Preferences p;
  p.begin(NS, false);
  p.putUShort("refr", sec);
  p.end();
  _version++;
  return true;
}

ClaudeBrief ConfigManager::brief() {
  Lock l(_mtx);
  ClaudeBrief b;
  b.rotationSec = _cfg.rotationSec;
  b.refreshSec = _cfg.refreshSec;
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    const ClaudeProfile &c = _cfg.claude[i];
    if (!c.used || !c.enabled) continue;
    b.idx[b.count] = i;
    strlcpy(b.name[b.count], c.name, sizeof(b.name[0]));
    b.count++;
  }
  return b;
}

bool ConfigManager::saveOAuthTokens(int idx, uint32_t editSeq, const char *access, const char *refreshTok,
                                    uint32_t expiresAt, const char *scope) {
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES || !access || !access[0]) return false;
  Lock l(_mtx);
  ClaudeProfile &c = _cfg.claude[idx];
  if (!c.used || c.editSeq != editSeq) return false;  // kozben a felhasznalo mas titkot adott meg
  Preferences p;
  p.begin(NS, false);
  bool ok = true;
  // ELOSZOR a (rotalt) refresh token (power-loss biztos: az uj perzisztal, mielott a regit eldobjuk).
  if (refreshTok && refreshTok[0] && strcmp(refreshTok, c.refresh) != 0) {
    ok = p.putString(key('c', idx, "rt").c_str(), refreshTok) > 0;
    if (ok) strlcpy(c.refresh, refreshTok, sizeof(c.refresh));
  }
  if (ok) ok = p.putString(key('c', idx, "auth").c_str(), access) > 0;
  if (ok) strlcpy(c.auth, access, sizeof(c.auth));
  p.putUInt(key('c', idx, "exp").c_str(), expiresAt);
  c.expiresAt = expiresAt;
  if (scope && scope[0]) {
    p.putString(key('c', idx, "sc").c_str(), scope);
    strlcpy(c.scope, scope, sizeof(c.scope));
  }
  p.end();
  _version++;
  return ok;
}
