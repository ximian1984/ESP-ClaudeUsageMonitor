#include "web_setup.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <esp_random.h>
#include <utility>

#include "admin_auth.h"
#include "claude_client.h"
#include "config.h"
#include "config_manager.h"
#include "oauth_client.h"
#include "refresh_scheduler.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "web_page.h"
#include "wifi_manager.h"

WebSetup webSetup;
static WebServer server(80);

static void sendJson(int code, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(int code, const char *msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(code, doc);
}

static void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

// CSRF-vedelem: a modosito kereseknek egyedi fejlecet kell kuldeniuk. Idegen weboldal ilyet csak
// CORS-preflighttal kuldhetne, amire ez a szerver nem valaszol — igy egy rosszindulatu oldal
// a bongeszobol nem irhatja at a konfigot.
static bool csrfOk() {
  if (server.header("X-CMon") != "1") {
    sendError(403, "missing X-CMon header");
    return false;
  }
  return true;
}

// Admin-jelszo kell-e most? Forced setup (BOOT gomb, fizikai hozzaferes) = nem: ez a helyreallitasi ut.
static bool adminRequired() { return adminAuth.passwordSet() && wifiManager.state() != WifiState::ApForced; }

// Minden modosito keres kapuja: CSRF-fejlec + (ha be van allitva) ervenyes admin-token.
static bool guardPost() {
  if (!csrfOk()) return false;
  if (adminRequired() && !adminAuth.tokenValid(server.header("X-CMon-Token"))) {
    sendError(401, "login required");
    return false;
  }
  return true;
}

static bool printableAscii(const String &s, bool allowSpace) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c < 0x20 || c > 0x7E) return false;
    if (!allowSpace && c == ' ') return false;
  }
  return true;
}

static int argInt(const char *name, int def) {
  if (!server.hasArg(name) || server.arg(name).isEmpty()) return def;
  return server.arg(name).toInt();
}

static bool argBool(const char *name) { return server.arg(name) == "1" || server.arg(name) == "true"; }

// idx < 0 -> elso szabad hely
static int resolveIdx(int idx, int max, bool usedFlags[]) {
  if (idx >= 0) return idx < max ? idx : -2;
  for (int i = 0; i < max; i++)
    if (!usedFlags[i]) return i;
  return -1;
}

static void handleStatus() {
  JsonDocument doc;
  doc["board"] = BOARD_NAME;
  doc["firmware"] = FW_VERSION;
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["state"] = wifiManager.stateName();
  w["ssid"] = wifiManager.staConnected() ? wifiManager.staSsid() : String("");
  w["ip"] = wifiManager.ipString();
  w["rssi"] = wifiManager.rssi();
  w["apSsid"] = wifiManager.apActive() ? wifiManager.apSsid() : String("");
  doc["timeSynced"] = timeManager.synced();
  doc["localTime"] = TimeManager::localDateTime(timeManager.now());
  doc["tz"] = timeManager.timezone();
  doc["uptimeS"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["claudeFetchCount"] = refreshScheduler.fetchCount();
  doc["adminSet"] = adminAuth.passwordSet();
  doc["adminRequired"] = adminRequired();

  DeviceConfig cfg = configManager.snapshot();
  time_t lastUpdate = 0;
  JsonArray arr = doc["claude"].to<JsonArray>();
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    if (!cfg.claude[i].used) continue;
    ProfileUsage u = usageCache.get(i);
    JsonObject o = arr.add<JsonObject>();
    o["idx"] = i;
    o["name"] = cfg.claude[i].name;
    o["hasData"] = u.hasData;
    o["lastOkAgoS"] = u.hasData ? (long)((millis() - u.lastOkMs) / 1000) : -1;
    o["lastError"] = fetchErrorTitle(u.lastError);
    o["httpStatus"] = u.lastHttpStatus;
    // Pontosan a kijelzon latszo reset-szovegek (ellenorzeshez; titok nincs benne).
    time_t tnow = timeManager.now();
    bool syn = timeManager.synced();
    for (const auto &kv : {std::make_pair(LimitKind::Session, "session"), std::make_pair(LimitKind::Weekly, "weekly")}) {
      const UsageLimit *l = u.hasData ? u.data.find(kv.first) : nullptr;
      if (l && l->hasReset) o[String(kv.second) + "Reset"] = TimeManager::resetText(l->resetAt, syn, tnow);
    }
    LastKnownResets lk = usageCache.lastKnown(i);
    if (lk.sessionReset) o["lastKnownSessionReset"] = TimeManager::resetText(lk.sessionReset, syn, tnow);
    if (lk.weeklyReset) o["lastKnownWeeklyReset"] = TimeManager::resetText(lk.weeklyReset, syn, tnow);
    if (u.lastOkEpoch > lastUpdate) lastUpdate = u.lastOkEpoch;
  }
  doc["lastClaudeUpdate"] = (long)lastUpdate;
  sendJson(200, doc);
}

static void handleConfig() {
  DeviceConfig cfg = configManager.snapshot();
  JsonDocument doc;
  JsonArray wa = doc["wifi"].to<JsonArray>();
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    const WifiProfile &p = cfg.wifi[i];
    if (!p.used) continue;
    JsonObject o = wa.add<JsonObject>();
    o["idx"] = i;
    o["ssid"] = p.ssid;
    o["enabled"] = p.enabled;
    o["priority"] = p.priority;
    o["hasPassword"] = p.password[0] != '\0';  // a jelszo maga SOHA
  }
  JsonArray ca = doc["claude"].to<JsonArray>();
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    const ClaudeProfile &p = cfg.claude[i];
    if (!p.used) continue;
    JsonObject o = ca.add<JsonObject>();
    o["idx"] = i;
    o["name"] = p.name;
    o["transport"] = transportName((ClaudeTransport)p.transport);
    o["orgId"] = p.orgId;
    o["enabled"] = p.enabled;
    o["hasAuth"] = p.auth[0] != '\0';       // access token / sessionKey — az ertek SOHA
    o["hasRefresh"] = p.refresh[0] != '\0';  // OAuth refresh token — az ertek SOHA
    o["expiresAt"] = (long)p.expiresAt;
  }
  doc["rotationSec"] = cfg.rotationSec;
  doc["refreshSec"] = cfg.refreshSec;
  doc["tz"] = cfg.tz;
  doc["maxWifi"] = MAX_WIFI_PROFILES;
  doc["maxClaude"] = MAX_CLAUDE_PROFILES;
  sendJson(200, doc);
}

// Ujravalasztas csak akkor, ha a mentes/torles a MOSTANI kapcsolatot erinti (vagy nincs kapcsolat). Kulonben a
// scan + WiFi.disconnect() ~20 s-ra bontana a jo kapcsolatot, es a setup-oldal frissitese elveszne (vason mert,
// 2026-09-17). Korlat: egy ujonnan felvett, nagyobb prioritasu halozatra csak a kovetkezo kapcsolatvesztesnel valt.
static void reselectIfAffected(const char *oldSsid, const char *newSsid) {
  WifiState st = wifiManager.state();
  if (st == WifiState::ApForced) return;
  if (st == WifiState::Connected) {
    String cur = wifiManager.staSsid();
    if (cur != oldSsid && cur != newSsid) return;
  }
  wifiManager.requestReselect();
}

static void handleWifiSave() {
  if (!guardPost()) return;
  DeviceConfig cfg = configManager.snapshot();
  bool used[MAX_WIFI_PROFILES];
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) used[i] = cfg.wifi[i].used;
  int idx = resolveIdx(argInt("idx", -1), MAX_WIFI_PROFILES, used);
  if (idx == -1) return sendError(400, "all wifi slots used");
  if (idx < 0) return sendError(400, "bad idx");

  String ssid = server.arg("ssid");
  if (ssid.isEmpty() || ssid.length() > WIFI_SSID_MAX) return sendError(400, "SSID length 1-32");

  WifiProfile p = cfg.wifi[idx];
  const bool existing = p.used;
  char oldSsid[WIFI_SSID_MAX + 1];
  strlcpy(oldSsid, existing ? p.ssid : "", sizeof(oldSsid));
  if (!existing || argBool("changePassword")) {
    String pass = server.arg("password");
    // WPA2-PSK: 8-63 ASCII vagy 64 hex; ures = nyilt halozat
    if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > WIFI_PASS_MAX)) return sendError(400, "password length 8-64 or empty");
    strlcpy(p.password, pass.c_str(), sizeof(p.password));
  }
  strlcpy(p.ssid, ssid.c_str(), sizeof(p.ssid));
  p.enabled = argBool("enabled");
  p.priority = (int16_t)constrain(argInt("priority", 0), -1000, 1000);
  if (!configManager.saveWifi(idx, p)) return sendError(500, "save failed");
  reselectIfAffected(oldSsid, p.ssid);
  sendOk();
}

static void handleWifiDelete() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  char oldSsid[WIFI_SSID_MAX + 1] = "";
  if (idx >= 0 && idx < MAX_WIFI_PROFILES) strlcpy(oldSsid, configManager.snapshot().wifi[idx].ssid, sizeof(oldSsid));
  if (!configManager.deleteWifi(idx)) return sendError(400, "bad idx");
  reselectIfAffected(oldSsid, oldSsid);
  sendOk();
}

static void handleClaudeSave() {
  if (!guardPost()) return;
  DeviceConfig cfg = configManager.snapshot();
  bool used[MAX_CLAUDE_PROFILES];
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) used[i] = cfg.claude[i].used;
  int idx = resolveIdx(argInt("idx", -1), MAX_CLAUDE_PROFILES, used);
  if (idx == -1) return sendError(400, "all claude slots used");
  if (idx < 0) return sendError(400, "bad idx");

  String name = server.arg("name");
  name.trim();
  if (name.isEmpty()) {  // projektgazda (2026-09-17): ures nev -> "Profile-XX", XX VELETLEN 00-99, masik profillal nem utkozik
    char auto_name[CLAUDE_NAME_MAX + 1];
    for (int tries = 0; tries < 50; tries++) {
      snprintf(auto_name, sizeof(auto_name), "Profile-%02u", (unsigned)(esp_random() % 100));
      bool clash = false;
      for (int i = 0; i < MAX_CLAUDE_PROFILES; i++)
        if (i != idx && cfg.claude[i].used && strcmp(cfg.claude[i].name, auto_name) == 0) clash = true;
      if (!clash) break;
    }
    name = auto_name;
  }
  if (name.length() > CLAUDE_NAME_MAX || !printableAscii(name, true))
    return sendError(400, "name: max 12 ASCII chars (empty = Profile-XX)");
  String tr = server.arg("transport");
  int transport = -1;
  for (int t = 0; t < CLAUDE_TRANSPORT_COUNT; t++)
    if (tr == transportName((ClaudeTransport)t)) transport = t;
  if (transport < 0) return sendError(400, "transport: web-session or oauth");
  String org = server.arg("orgId");
  org.trim();
  if (transportNeedsOrgId((ClaudeTransport)transport) && !isValidOrgId(org.c_str()))
    return sendError(400, "Organization ID must be a UUID");
  if (!org.isEmpty() && !isValidOrgId(org.c_str())) return sendError(400, "Organization ID must be a UUID or empty");

  ClaudeProfile p = cfg.claude[idx];
  bool transportChanged = p.used && p.transport != transport;
  if (transportChanged) {  // masik ut -> a regi titkok ervenytelenek
    p.auth[0] = p.refresh[0] = p.scope[0] = '\0';
    p.expiresAt = 0;
  }
  p.transport = (uint8_t)transport;

  if (transport == (int)ClaudeTransport::OAuth) {
    // OAuth: a tokeneket NEM ez a form adja, hanem az on-device login (handleOAuthFinish).
    // Itt csak nev/engedelyezes/uj-profil. A meglevo tokenek megmaradnak (p a snapshotbol jott).
  } else {
    // WebSession: sessionKey kezi megadasa.
    if (!p.used || transportChanged || argBool("changeAuth")) {
      String auth = server.arg("auth");
      auth.trim();
      // Fejlec-injektalas ellen: csak lathato ASCII, szokoz/;/, nelkul (sutiertekbe kerul).
      if (auth.length() > CLAUDE_AUTH_MAX || !printableAscii(auth, false) || auth.indexOf(';') >= 0 || auth.indexOf(',') >= 0)
        return sendError(400, "auth: max 300 visible ASCII chars, no ; or ,");
      if (auth.isEmpty()) return sendError(400, "sessionKey value required");
      strlcpy(p.auth, auth.c_str(), sizeof(p.auth));
    }
  }
  strlcpy(p.name, name.c_str(), sizeof(p.name));
  strlcpy(p.orgId, org.c_str(), sizeof(p.orgId));
  p.enabled = argBool("enabled");
  if (!configManager.saveClaude(idx, p)) return sendError(500, "save failed");
  // Mas fiok/ut lehet: a regi profil mentett reset-idopontjai nem ervenyesek. (Csak ha tenyleg valtozott a forras.)
  if (!cfg.claude[idx].used || transportChanged || strcmp(cfg.claude[idx].orgId, org.c_str()) != 0) usageCache.forgetLastKnown(idx);
  JsonDocument doc;  // az idx kell a kliensnek: "Authenticate now" = mentes + azonnali login ugyanarra a profilra
  doc["ok"] = true;
  doc["idx"] = idx;
  sendJson(200, doc);
}

// --- OAuth on-device login (PKCE) ---
// Egyszerre egy fuggoben levo login (egy admin egy eszkozt allit be). Csak RAM-ban.
static struct {
  bool active = false;
  int idx = -1;
  uint32_t editSeq = 0;
  uint32_t startedMs = 0;
  String verifier;
  String state;
} g_login;

static void handleOAuthStart() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return sendError(400, "bad idx");
  DeviceConfig cfg = configManager.snapshot();
  if (!cfg.claude[idx].used || cfg.claude[idx].transport != (uint8_t)ClaudeTransport::OAuth)
    return sendError(400, "not an OAuth profile");
  OAuthLogin lg;
  if (!oauthBeginLogin(lg)) return sendError(500, "PKCE init failed");
  g_login.active = true;
  g_login.idx = idx;
  g_login.editSeq = cfg.claude[idx].editSeq;
  g_login.startedMs = millis();
  g_login.verifier = lg.verifier;
  g_login.state = lg.state;
  JsonDocument doc;
  doc["ok"] = true;
  doc["authorizeUrl"] = lg.authorizeUrl;  // NEM titok; a verifier/state a RAM-ban marad
  sendJson(200, doc);
}

static void handleOAuthFinish() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  if (!g_login.active || g_login.idx != idx) return sendError(400, "no pending login for this profile");
  if (millis() - g_login.startedMs > OAUTH_LOGIN_TTL_MS) {
    g_login.active = false;
    return sendError(408, "login expired, start again");
  }
  String code = server.arg("code");
  code.trim();
  if (code.isEmpty()) return sendError(400, "paste the code#state value");
  // Pontos ido nelkul a TLS-tanusitvany ervenyessege nem ellenorizheto, es a lejarat (now + expires_in) is hamis lenne.
  // A login ezert csak Wi-Fi (STA) + NTP utan mehet; a pending login (verifier/state) megmarad, ujra bekuldheto.
  if (!timeManager.synced()) return sendError(503, "no internet time yet (connect Wi-Fi, wait for NTP), then resubmit");
  // Blokkolo (TLS + kodcsere) — a felhasznalo varja; a setup-oldal addig is fut.
  OAuthTokens t = oauthExchangeCode(code, g_login.verifier, g_login.state);
  if (!t.ok) {
    // A hibaszoveg beszedes (pl. "state mismatch", "400 invalid_grant"), titkot nem tartalmaz.
    return sendError(400, t.error.length() ? t.error.c_str() : "token exchange failed");
  }
  uint32_t exp = t.expiresIn ? (uint32_t)(timeManager.now() + t.expiresIn) : 0;
  bool ok = configManager.saveOAuthTokens(idx, g_login.editSeq, t.access.c_str(), t.refresh.c_str(), exp,
                                          t.scope.c_str());
  g_login.active = false;
  g_login.verifier = "";  // titok torlese a RAM-bol
  g_login.state = "";
  if (!ok) return sendError(409, "profile changed during login, try again");
  usageCache.forgetLastKnown(idx);  // uj bejelentkezes: lehet masik fiok
  sendOk();
}

static void handleClaudeDelete() {
  if (!guardPost()) return;
  if (!configManager.deleteClaude(argInt("idx", -1))) return sendError(400, "bad idx");
  usageCache.forgetLastKnown(argInt("idx", -1));
  sendOk();
}

static void handleRefresh() {
  if (!guardPost()) return;
  int sec = argInt("refreshSec", -1);
  if (sec < REFRESH_PERIOD_MIN_S || sec > REFRESH_PERIOD_MAX_S) return sendError(400, "refresh 60-3600 sec");
  if (!configManager.saveRefreshSec((uint16_t)sec)) return sendError(500, "save failed");
  sendOk();
}

static void handleTimezone() {
  if (!guardPost()) return;
  String tz = server.arg("tz");
  tz.trim();
  if (!TimeManager::validPosixTz(tz.c_str())) return sendError(400, "POSIX TZ: 3-47 chars, e.g. CET-1CEST,M3.5.0,M10.5.0/3");
  if (!configManager.saveTimezone(tz.c_str())) return sendError(500, "save failed");
  timeManager.setTimezone(tz.c_str());
  JsonDocument doc;
  doc["ok"] = true;
  doc["localTime"] = TimeManager::localDateTime(timeManager.now());
  sendJson(200, doc);
}

static void handleDisplay() {
  if (!guardPost()) return;
  int sec = argInt("rotationSec", -1);
  if (sec < ROTATION_MIN_S || sec > ROTATION_MAX_S) return sendError(400, "rotation 1-60 sec");
  if (!configManager.saveRotation((uint8_t)sec)) return sendError(500, "save failed");
  sendOk();
}

static void handleScanStart() {
  if (!guardPost()) return;
  wifiManager.requestScan();
  sendOk();
}

static void handleScanResults() {
  JsonDocument doc;
  doc["running"] = wifiManager.scanRunning();
  JsonArray arr = doc["results"].to<JsonArray>();
  for (const ScanEntry &e : wifiManager.scanResults()) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = e.ssid;
    o["rssi"] = e.rssi;
    o["secure"] = e.secure;
  }
  sendJson(200, doc);
}

static void handleLogin() {
  if (!csrfOk()) return;
  String token;
  switch (adminAuth.login(server.arg("password"), token)) {  // a jelszo nem kerul logba
    case AdminAuth::LoginResult::Ok: {
      JsonDocument doc;
      doc["ok"] = true;
      doc["token"] = token;
      return sendJson(200, doc);
    }
    case AdminAuth::LoginResult::LockedOut:
      return sendError(429, "too many attempts, wait 60 s");
    case AdminAuth::LoginResult::Wrong:
      return sendError(401, "wrong password");
  }
}

static void handleAdminPassword() {
  if (!guardPost()) return;
  String pw = server.arg("newPassword");
  if (!pw.isEmpty() && (pw.length() < ADMIN_PASS_MIN || pw.length() > ADMIN_PASS_MAX || !printableAscii(pw, true)))
    return sendError(400, "admin password: 8-64 ASCII chars, or empty to remove");
  if (!adminAuth.setPassword(pw)) return sendError(500, "save failed");
  sendOk();  // minden token ervenytelen lett -> a bongeszonek ujra be kell lepnie
}

static bool restartPending = false;
static uint32_t restartAtMs = 0;

static void handleRestart() {
  if (!guardPost()) return;
  sendOk();
  restartPending = true;
  restartAtMs = millis() + 500;  // a valasz meg kimenjen
}

void WebSetup::begin() {
  static const char *headers[] = {"X-CMon", "X-CMon-Token"};
  server.collectHeaders(headers, 2);

  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", WEB_PAGE_HTML);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/delete", HTTP_POST, handleWifiDelete);
  server.on("/api/claude", HTTP_POST, handleClaudeSave);
  server.on("/api/claude/delete", HTTP_POST, handleClaudeDelete);
  server.on("/api/display", HTTP_POST, handleDisplay);
  server.on("/api/timezone", HTTP_POST, handleTimezone);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/oauth/start", HTTP_POST, handleOAuthStart);
  server.on("/api/oauth/finish", HTTP_POST, handleOAuthFinish);
  server.on("/api/scan", HTTP_POST, handleScanStart);
  server.on("/api/scan", HTTP_GET, handleScanResults);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/admin", HTTP_POST, handleAdminPassword);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void WebSetup::loop() {
  server.handleClient();
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}
