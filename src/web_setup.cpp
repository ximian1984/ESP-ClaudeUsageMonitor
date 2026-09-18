#include "web_setup.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <esp_random.h>
#include <memory>
#include <utility>

#include "admin_auth.h"
#include "backup_crypto.h"
#include "claude_client.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "oauth_client.h"
#include "provider_auth.h"
#include "token_cache.h"
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

// Olvaso kapu: admin-jelszo eseten a konfiguracio (SSID-k, profilnevek, org-ID-k, IP, scan) is csak belepve latszik
// (projektgazda, 2026-09-17: "login nelkul is latom a beallitasokat"). Kulonben 401.
static bool readAllowed(bool touch) {
  return !adminRequired() || adminAuth.tokenValid(server.header("X-CMon-Token"), touch);
}

static bool guardRead() {
  if (readAllowed(true)) return true;
  sendError(401, "login required");
  return false;
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
  if (!readAllowed(false)) {  // zarolva: semmi (se tipus, se verzio) — idegen ne tudja meg, mi fut rajta
    doc["locked"] = true;
    return sendJson(200, doc);
  }
  doc["locked"] = false;
  doc["board"] = BOARD_NAME;
  doc["firmware"] = FW_VERSION;
  doc["adminSet"] = adminAuth.passwordSet();
  doc["adminRequired"] = adminRequired();
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
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["loopStackFreeMin"] = uxTaskGetStackHighWaterMark(nullptr);  // a loopTask eletideje alatti legkisebb szabad stack (B)
  doc["claudeFetchCount"] = refreshScheduler.fetchCount();

  auto cfgHeap = configManager.heapSnapshot();
  DeviceConfig &cfg = *cfgHeap;
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
  if (!guardRead()) return;
  auto cfgHeap = configManager.heapSnapshot();
  DeviceConfig &cfg = *cfgHeap;
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
  doc["displayFlip"] = cfg.displayFlip;
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
  auto cfgHeap = configManager.heapSnapshot();
  DeviceConfig &cfg = *cfgHeap;
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
  if (idx >= 0 && idx < MAX_WIFI_PROFILES) strlcpy(oldSsid, configManager.heapSnapshot()->wifi[idx].ssid, sizeof(oldSsid));
  if (!configManager.deleteWifi(idx)) return sendError(400, "bad idx");
  reselectIfAffected(oldSsid, oldSsid);
  sendOk();
}

static void handleClaudeSave() {
  if (!guardPost()) return;
  auto cfgHeap = configManager.heapSnapshot();
  DeviceConfig &cfg = *cfgHeap;
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
  // Csak a Claude web-utnal kerul az urlapbol; a tobbi forrasnal a login adja (lent megtartjuk), az urlap erteket
  // nem validaljuk (az Enable/Disable gomb pl. a Gemini project-ID-t kuldi vissza).
  if (transport == (int)ClaudeTransport::WebSession && !org.isEmpty() && !isValidOrgId(org.c_str()))
    return sendError(400, "Organization ID must be a UUID or empty");

  ClaudeProfile p = cfg.claude[idx];
  bool transportChanged = p.used && p.transport != transport;
  if (transportChanged) {  // masik ut -> a regi titkok ervenytelenek
    p.auth[0] = p.refresh[0] = p.scope[0] = '\0';
    p.expiresAt = 0;
  }
  p.transport = (uint8_t)transport;

  if (providerIsOAuth((ClaudeTransport)transport)) {
    // OAuth-jellegu forras (Claude/Gemini/ChatGPT/Grok): a tokeneket NEM ez a form adja, hanem a login.
    // Itt csak nev/engedelyezes/uj-profil. A meglevo tokenek es az account-/project-ID megmaradnak (p a snapshotbol).
    if (transportChanged || !p.used) p.orgId[0] = '\0';
    else org = p.orgId;
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

// --- On-device login (Claude/Gemini: kodmasolas; ChatGPT/Grok: eszkozkod) ---
// Egyszerre egy fuggoben levo login (egy admin egy eszkozt allit be). Csak RAM-ban.
static PendingLogin g_login;

static void finishLoginFailed(const String &err, int code = 400) {
  Serial.printf("[web] login hiba: %s\n", err.c_str());
  sendError(code, err.length() ? err.c_str() : "login failed");
}

// Sikeres token-valasz mentese szolgaltatonkent. true = kesz.
static bool storeLoginTokens(const ProviderTokens &t, String &err) {
  int idx = g_login.idx;
  ClaudeTransport tr = g_login.transport;
  if (tr == ClaudeTransport::OAuth) {
    uint32_t exp = t.expiresIn ? (uint32_t)(timeManager.now() + t.expiresIn) : 0;
    if (!configManager.saveOAuthTokens(idx, g_login.editSeq, t.access.c_str(), t.refresh.c_str(), exp, t.scope.c_str())) {
      err = "profile changed during login, try again";
      return false;
    }
  } else {
    String orgId = t.accountId;  // ChatGPT: account-ID az id_token-bol
    if (tr == ClaudeTransport::Gemini) {
      String perr;
      orgId = geminiLoadProject(t.access, perr);  // ures is lehet: a scheduler kesobb ujraprobalja
      if (orgId.isEmpty()) Serial.printf("[web] gemini project: %s\n", perr.c_str());
    }
    if (!configManager.saveProviderLogin(idx, g_login.editSeq, t.refresh.c_str(), orgId.length() ? orgId.c_str() : nullptr,
                                         t.scope.c_str())) {
      err = t.refresh.length() > CLAUDE_REFRESH_MAX ? "refresh token too long for this firmware" : "profile changed during login, try again";
      return false;
    }
    tokenCache.set(idx, t.access, timeManager.now() + (time_t)(t.expiresIn ? t.expiresIn : PROVIDER_ACCESS_DEFAULT_TTL_S));
  }
  usageCache.forgetLastKnown(idx);  // uj bejelentkezes: lehet masik fiok
  return true;
}

static void handleOAuthStart() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  if (idx < 0 || idx >= MAX_CLAUDE_PROFILES) return sendError(400, "bad idx");
  auto cfgHeap = configManager.heapSnapshot();
  const ClaudeProfile &c = cfgHeap->claude[idx];
  ClaudeTransport tr = (ClaudeTransport)c.transport;
  if (!c.used || !providerIsOAuth(tr)) return sendError(400, "not an OAuth profile");
  // Eszkozkodnal mar az inditas is HTTPS-keres (TLS -> pontos ido kell).
  PendingLogin lg;
  String err;
  if (!timeManager.synced() && (tr == ClaudeTransport::ChatGpt || tr == ClaudeTransport::Grok))
    return sendError(503, "no internet time yet (connect Wi-Fi, wait for NTP), then retry");
  if (!providerBeginLogin(tr, lg, err)) return finishLoginFailed(err, 502);
  lg.active = true;
  lg.idx = idx;
  lg.editSeq = c.editSeq;
  lg.startedMs = millis();
  g_login = lg;
  JsonDocument doc;
  doc["ok"] = true;
  doc["provider"] = providerLabel(tr);
  doc["mode"] = lg.mode == LoginMode::Device ? "device" : "code";
  doc["url"] = lg.url;                // NEM titok
  doc["authorizeUrl"] = lg.url;       // regi feluletnek
  if (lg.mode == LoginMode::Device) {
    doc["userCode"] = lg.userCode;    // a felhasznalonak mutatando kod (nem titok)
    doc["interval"] = lg.intervalS;
    doc["expiresIn"] = lg.expiresS;
  } else {
    doc["codeFormat"] = tr == ClaudeTransport::OAuth ? "code#state" : "code";
  }
  sendJson(200, doc);
}

static bool loginValid(int idx, LoginMode mode) {
  if (!g_login.active || g_login.idx != idx || g_login.mode != mode) {
    sendError(400, "no pending login for this profile");
    return false;
  }
  uint32_t ttl = mode == LoginMode::Device ? g_login.expiresS * 1000UL : OAUTH_LOGIN_TTL_MS;
  if (millis() - g_login.startedMs > ttl) {
    g_login.clear();
    sendError(408, "login expired, start again");
    return false;
  }
  return true;
}

static void handleOAuthFinish() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  if (!loginValid(idx, LoginMode::CodePaste)) return;
  String code = server.arg("code");
  code.trim();
  if (code.isEmpty()) return sendError(400, "paste the code from the sign-in page");
  // Pontos ido nelkul a TLS-tanusitvany ervenyessege nem ellenorizheto, es a lejarat (now + expires_in) is hamis lenne.
  // A login ezert csak Wi-Fi (STA) + NTP utan mehet; a pending login (verifier/state) megmarad, ujra bekuldheto.
  if (!timeManager.synced()) return sendError(503, "no internet time yet (connect Wi-Fi, wait for NTP), then resubmit");
  ProviderTokens t = providerFinishCode(g_login, code);  // blokkolo (TLS + kodcsere)
  if (!t.ok) return finishLoginFailed(t.error.length() ? t.error : String("token exchange failed"));
  String err;
  bool ok = storeLoginTokens(t, err);
  g_login.clear();  // titok (verifier/state) torlese a RAM-bol
  if (!ok) return finishLoginFailed(err, 409);
  sendOk();
}

// Eszkozkod: a felulet a kapott interval szerint hivja; egy hivas = legfeljebb egy lekerdezes.
static void handleOAuthPoll() {
  if (!guardPost()) return;
  int idx = argInt("idx", -1);
  if (!loginValid(idx, LoginMode::Device)) return;
  JsonDocument doc;
  doc["ok"] = true;
  if (millis() - g_login.lastPollMs < g_login.intervalS * 1000UL) {  // a szolgaltato slow_down-t adna
    doc["status"] = "pending";
    return sendJson(200, doc);
  }
  g_login.lastPollMs = millis();
  ProviderTokens t = providerPollDevice(g_login);
  if (t.pending) {
    if (t.slowDown) g_login.intervalS += 5;  // RFC 8628 3.5
    doc["status"] = "pending";
    doc["interval"] = g_login.intervalS;
    return sendJson(200, doc);
  }
  if (!t.ok) {
    String e = t.error.length() ? t.error : String("device login failed");
    g_login.clear();
    return finishLoginFailed(e);
  }
  String err;
  bool ok = storeLoginTokens(t, err);
  g_login.clear();
  if (!ok) return finishLoginFailed(err, 409);
  doc["status"] = "done";
  sendJson(200, doc);
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
  configManager.saveDisplayFlip(argBool("flip"));  // 180 fokos forgatas; azonnal ervenyes
  sendOk();
}

// A kijelzo pontos kepe (a TFT_eSPI sprite BAJTCSERELT RGB565-e, W*H szo; a kliens forditja vissza): a bongeszo ugyanazt rajzolja ki, mint ami az LCD-n van.
// Belepve erheto el (guardRead). ~25,6 KB, darabokban kuldve (a WebServer String-buffere igy nem no meg).
static void handleScreen() {
  if (!guardRead()) return;
  int w = 0, h = 0;
  const uint16_t *fbuf = displayManager.framebuffer(w, h);
  if (!fbuf) return sendError(503, "no framebuffer");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Screen-Size", String(w) + "x" + String(h));
  server.setContentLength((size_t)w * h * 2);
  server.send(200, "application/octet-stream", "");
  const size_t chunk = 2048;  // bajt
  const char *p = (const char *)fbuf;
  size_t total = (size_t)w * h * 2;
  for (size_t off = 0; off < total; off += chunk) server.sendContent(p + off, min(chunk, total - off));
}

static void handleScanStart() {
  if (!guardPost()) return;
  wifiManager.requestScan();
  sendOk();
}

static void handleScanResults() {
  if (!guardRead()) return;
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


// ---------------------------------------------------------------------------------------------
// Beallitasok exportja / importja (projektgazda, 2026-09-17).
// Export: JSON-fajl. Titok (Wi-Fi-jelszo, Claude access/refresh token) CSAK kifejezett keresre (?secrets=1), es csak ha
// van admin-jelszo — kulonben egy jelszo nelkuli eszkozrol a LAN-on barki kimenthetne a tokeneket.
// Import: az osszes Wi-Fi- es Claude-profil + megjelenites/TZ cserje. Ahol a fajlban nincs titok, a MEGLEVO titkot
// tartja meg (Wi-Fi: azonos SSID; Claude: azonos nev + forras), igy egy titok nelkuli export visszatoltese nem
// jelentkeztet ki.
// ---------------------------------------------------------------------------------------------
static const char *EXPORT_FORMAT = "device-config";
static const int EXPORT_VERSION = 1;

static void buildExport(bool secrets, JsonDocument &doc) {
  std::unique_ptr<DeviceConfig> cfg(new DeviceConfig);
  configManager.copyTo(*cfg);
  doc["format"] = EXPORT_FORMAT;
  doc["version"] = EXPORT_VERSION;
  doc["firmware"] = FW_VERSION;
  doc["exportedAt"] = TimeManager::localDateTime(timeManager.now());
  doc["secrets"] = secrets;
  JsonArray wa = doc["wifi"].to<JsonArray>();
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    const WifiProfile &w = cfg->wifi[i];
    if (!w.used) continue;
    JsonObject o = wa.add<JsonObject>();
    o["ssid"] = w.ssid;
    o["enabled"] = w.enabled;
    o["priority"] = w.priority;
    if (secrets) o["password"] = w.password;
  }
  JsonArray ca = doc["claude"].to<JsonArray>();
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    const ClaudeProfile &c = cfg->claude[i];
    if (!c.used) continue;
    JsonObject o = ca.add<JsonObject>();
    o["name"] = c.name;
    o["transport"] = transportName((ClaudeTransport)c.transport);
    o["orgId"] = c.orgId;
    o["enabled"] = c.enabled;
    if (secrets) {
      o["auth"] = c.auth;
      o["refresh"] = c.refresh;
      o["scope"] = c.scope;
      o["expiresAt"] = c.expiresAt;
    }
  }
  doc["rotationSec"] = cfg->rotationSec;
  doc["refreshSec"] = cfg->refreshSec;
  doc["displayFlip"] = cfg->displayFlip;
  doc["tz"] = cfg->tz;
  Serial.printf("[web] export: %u Wi-Fi, %u Claude, titok=%s\n", (unsigned)wa.size(), (unsigned)ca.size(), secrets ? "igen" : "nem");
}

// Titok nelkuli, olvashato export.
static void handleExport() {
  if (!guardRead()) return;
  if (server.arg("secrets") == "1") return sendError(400, "secrets are only exported encrypted (POST /api/export-encrypted)");
  JsonDocument doc;
  buildExport(false, doc);
  sendJson(200, doc);
}

// Titkot tartalmazo export, az admin-jelszoval titkositva (projektgazda, 2026-09-17). A jelszot ujra ellenorizzuk
// (nem eleg a munkamenet-token), es a hibas probalkozas a loginnal kozosen zarol.
static void handleExportEncrypted() {
  if (!guardPost()) return;
  if (!adminAuth.passwordSet()) return sendError(403, "set an admin password before exporting secrets");
  String pw = server.arg("password");
  switch (adminAuth.checkPassword(pw)) {
    case AdminAuth::LoginResult::Ok:
      break;
    case AdminAuth::LoginResult::LockedOut:
      return sendError(429, "too many attempts, wait 60 s");
    default:
      return sendError(403, "wrong admin password");  // nem 401: az a munkamenet lejartat jelenti a feluletnek
  }
  String plain;
  {
    JsonDocument doc;
    buildExport(true, doc);
    serializeJson(doc, plain);
  }
  JsonDocument env;
  String err;
  bool ok = backupEncrypt(plain, pw, env, err);
  for (size_t i = 0; i < plain.length(); i++) plain.setCharAt(i, 0);  // titok torlese a RAM-bol
  if (!ok) return sendError(500, err.c_str());
  env["ok"] = true;  // a felulet post()-ja ezt varja; a letoltott fajlbol a JS kiveszi
  sendJson(200, env);
}

static bool tokenString(const char *s, size_t maxLen) {  // fejlecbe kerul: lathato ASCII, szokoz/;/, nelkul
  if (!s) return false;
  size_t n = strlen(s);
  if (n > maxLen) return false;
  for (size_t i = 0; i < n; i++)
    if (s[i] <= 0x20 || s[i] > 0x7E || s[i] == ';' || s[i] == ',') return false;
  return true;
}

static void handleImport() {
  if (!guardPost()) return;
  const String &body = server.arg("plain");
  if (body.isEmpty() || body.length() > 32768) return sendError(400, "empty or too large file");
  JsonDocument in;
  {
    JsonDocument outer;
    if (deserializeJson(outer, body)) return sendError(400, "not a valid JSON file");
    // Titkositott fajl: a felulet {"backup": <boritek>, "password": "..."} alakban kuldi.
    JsonVariantConst env = outer["backup"].is<JsonObjectConst>() ? outer["backup"].as<JsonVariantConst>() : outer.as<JsonVariantConst>();
    if (env["format"] == BACKUP_ENC_FORMAT) {
      String pw = outer["password"] | "";
      if (pw.isEmpty()) return sendError(400, "this file is encrypted: enter the admin password it was exported with");
      String plain, err;
      if (!backupDecrypt(env, pw, plain, err)) {
        Serial.printf("[web] import: visszafejtes sikertelen (%s)\n", err.c_str());
        return sendError(403, err.c_str());
      }
      DeserializationError de = deserializeJson(in, plain);
      for (size_t i = 0; i < plain.length(); i++) plain.setCharAt(i, 0);
      if (de) return sendError(400, "decrypted content is not valid JSON");
    } else {
      in = outer;
    }
  }
  if (!(in["format"] == EXPORT_FORMAT) || (in["version"] | 0) != EXPORT_VERSION)
    return sendError(400, "not a settings export of this device (format/version)");
  JsonArrayConst wa = in["wifi"].as<JsonArrayConst>();
  JsonArrayConst ca = in["claude"].as<JsonArrayConst>();
  if (wa.size() > MAX_WIFI_PROFILES) return sendError(400, "too many Wi-Fi profiles");
  if (ca.size() > MAX_CLAUDE_PROFILES) return sendError(400, "too many Claude profiles");

  std::unique_ptr<DeviceConfig> cur(new DeviceConfig), nc(new DeviceConfig);
  configManager.copyTo(*cur);
  *nc = *cur;  // AP-jelszo es minden, amit a fajl nem ir felul
  for (auto &w : nc->wifi) w = WifiProfile();
  for (auto &c : nc->claude) c = ClaudeProfile();

  int wi = 0;
  for (JsonObjectConst o : wa) {
    const char *ssid = o["ssid"] | "";
    if (!ssid[0] || strlen(ssid) > WIFI_SSID_MAX) return sendError(400, "Wi-Fi: SSID length 1-32");
    WifiProfile &w = nc->wifi[wi++];
    w.used = true;
    strlcpy(w.ssid, ssid, sizeof(w.ssid));
    w.enabled = o["enabled"] | true;
    w.priority = (int16_t)constrain((int)(o["priority"] | 0), -1000, 1000);
    if (o["password"].is<const char *>()) {
      const char *pw = o["password"];
      size_t n = strlen(pw);
      if (n && (n < 8 || n > WIFI_PASS_MAX)) return sendError(400, "Wi-Fi: password length 8-64 or empty");
      strlcpy(w.password, pw, sizeof(w.password));
    } else {
      for (const WifiProfile &old : cur->wifi)  // nincs a fajlban: a meglevo jelszo marad (azonos SSID)
        if (old.used && strcmp(old.ssid, ssid) == 0) strlcpy(w.password, old.password, sizeof(w.password));
    }
  }

  int ci = 0;
  for (JsonObjectConst o : ca) {
    ClaudeProfile &c = nc->claude[ci];
    String name = o["name"] | "";
    name.trim();
    if (name.isEmpty()) {
      char buf[CLAUDE_NAME_MAX + 1];
      snprintf(buf, sizeof(buf), "Profile-%02u", (unsigned)(esp_random() % 100));
      name = buf;
    }
    if (name.length() > CLAUDE_NAME_MAX || !printableAscii(name, true)) return sendError(400, "Claude: name max 12 ASCII chars");
    String tr = o["transport"] | "oauth";
    int transport = -1;
    for (int t = 0; t < CLAUDE_TRANSPORT_COUNT; t++)
      if (tr == transportName((ClaudeTransport)t)) transport = t;
    if (transport < 0) return sendError(400, "Claude: transport must be oauth or web-session");
    const char *org = o["orgId"] | "";
    // WebSession: org-UUID kotelezo; a tobbi forrasnal az orgId a login altal adott account-/project-ID.
    if (transport == (int)ClaudeTransport::WebSession && !isValidOrgId(org))
      return sendError(400, "Claude web: Organization ID must be a UUID");
    if (strlen(org) > CLAUDE_ORG_MAX || !printableAscii(String(org), false)) return sendError(400, "profile: invalid orgId");
    c.used = true;
    c.enabled = o["enabled"] | true;
    c.transport = (uint8_t)transport;
    strlcpy(c.name, name.c_str(), sizeof(c.name));
    strlcpy(c.orgId, org, sizeof(c.orgId));
    if (o["auth"].is<const char *>() || o["refresh"].is<const char *>()) {
      const char *a = o["auth"] | "", *r = o["refresh"] | "", *sc = o["scope"] | "";
      if (!tokenString(a, CLAUDE_AUTH_MAX) || !tokenString(r, CLAUDE_REFRESH_MAX) || strlen(sc) > CLAUDE_SCOPE_MAX)
        return sendError(400, "profile: invalid token field");
      strlcpy(c.auth, a, sizeof(c.auth));
      strlcpy(c.refresh, r, sizeof(c.refresh));
      strlcpy(c.scope, sc, sizeof(c.scope));
      c.expiresAt = o["expiresAt"] | (uint32_t)0;
    } else {
      for (const ClaudeProfile &old : cur->claude)  // nincs a fajlban: a meglevo tokenek maradnak (nev + forras)
        if (old.used && old.transport == c.transport && strcmp(old.name, c.name) == 0) {
          strlcpy(c.auth, old.auth, sizeof(c.auth));
          strlcpy(c.refresh, old.refresh, sizeof(c.refresh));
          strlcpy(c.scope, old.scope, sizeof(c.scope));
          c.expiresAt = old.expiresAt;
        }
    }
    ci++;
  }

  int rot = in["rotationSec"] | (int)cur->rotationSec;
  int refr = in["refreshSec"] | (int)cur->refreshSec;
  if (rot < ROTATION_MIN_S || rot > ROTATION_MAX_S) return sendError(400, "rotation 1-60 sec");
  if (refr < REFRESH_PERIOD_MIN_S || refr > REFRESH_PERIOD_MAX_S) return sendError(400, "refresh 60-3600 sec");
  nc->rotationSec = (uint8_t)rot;
  nc->refreshSec = (uint16_t)refr;
  nc->displayFlip = in["displayFlip"] | cur->displayFlip;
  const char *tz = in["tz"] | cur->tz;
  if (!TimeManager::validPosixTz(tz)) return sendError(400, "invalid POSIX TZ");
  strlcpy(nc->tz, tz, sizeof(nc->tz));

  // Minden validalva -> egyben csere.
  if (!configManager.importAll(*nc)) return sendError(500, "save failed");
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++) {
    const ClaudeProfile &a = cur->claude[i], &b = nc->claude[i];
    if (a.used != b.used || a.transport != b.transport || strcmp(a.name, b.name) || strcmp(a.orgId, b.orgId))
      usageCache.forgetLastKnown(i);
  }
  timeManager.setTimezone(nc->tz);
  // Wi-Fi: ujravalasztas csak ha a mostani kapcsolat erintett (nincs benne engedelyezve, vagy mas a jelszava).
  bool keep = false;
  if (wifiManager.state() == WifiState::Connected) {
    String curSsid = wifiManager.staSsid();
    for (const WifiProfile &w : nc->wifi) {
      if (!w.used || !w.enabled || curSsid != w.ssid) continue;
      for (const WifiProfile &o : cur->wifi)
        if (o.used && curSsid == o.ssid && strcmp(o.password, w.password) == 0) keep = true;
    }
  }
  if (!keep && wifiManager.state() != WifiState::ApForced) wifiManager.requestReselect();
  Serial.printf("[web] import: %d Wi-Fi, %d Claude, ujravalasztas=%s\n", wi, ci, keep ? "nem" : "igen");
  JsonDocument doc;
  doc["ok"] = true;
  doc["wifi"] = wi;
  doc["claude"] = ci;
  doc["reconnect"] = !keep;
  sendJson(200, doc);
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

  server.on("/", HTTP_GET, [] {  // semleges login-hej; a felulet a /api/ui-rol jon, belepve
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", LOGIN_SHELL_HTML);
  });
  server.on("/api/ui", HTTP_GET, [] {
    if (!guardRead()) return;
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
  server.on("/api/oauth/poll", HTTP_POST, handleOAuthPoll);
  server.on("/api/scan", HTTP_POST, handleScanStart);
  server.on("/api/scan", HTTP_GET, handleScanResults);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/admin", HTTP_POST, handleAdminPassword);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/screen", HTTP_GET, handleScreen);
  server.on("/screen", HTTP_GET, [] {  // onallo oldal (uj ablakban vagy kozvetlen URL-rol), belepessel
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", SCREEN_PAGE_HTML);
  });
  server.on("/api/export-encrypted", HTTP_POST, handleExportEncrypted);
  server.on("/api/import", HTTP_POST, handleImport);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void WebSetup::loop() {
  server.handleClient();
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}
