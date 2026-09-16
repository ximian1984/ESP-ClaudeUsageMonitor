#include "web_setup.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "claude_client.h"
#include "config.h"
#include "config_manager.h"
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
static bool guardPost() {
  if (server.header("X-CMon") != "1") {
    sendError(403, "missing X-CMon header");
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
  doc["uptimeS"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["claudeFetchCount"] = refreshScheduler.fetchCount();

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
    o["orgId"] = p.orgId;
    o["enabled"] = p.enabled;
    o["hasAuth"] = p.auth[0] != '\0';  // az auth maga SOHA
  }
  doc["rotationSec"] = cfg.rotationSec;
  doc["maxWifi"] = MAX_WIFI_PROFILES;
  doc["maxClaude"] = MAX_CLAUDE_PROFILES;
  sendJson(200, doc);
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
  if (wifiManager.state() != WifiState::ApForced) wifiManager.requestReselect();
  sendOk();
}

static void handleWifiDelete() {
  if (!guardPost()) return;
  if (!configManager.deleteWifi(argInt("idx", -1))) return sendError(400, "bad idx");
  if (wifiManager.state() != WifiState::ApForced) wifiManager.requestReselect();
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
  if (name.isEmpty() || name.length() > CLAUDE_NAME_MAX || !printableAscii(name, true))
    return sendError(400, "name: 1-12 ASCII chars");
  String org = server.arg("orgId");
  org.trim();
  if (!isValidOrgId(org.c_str())) return sendError(400, "Organization ID must be a UUID");

  ClaudeProfile p = cfg.claude[idx];
  if (!p.used || argBool("changeAuth")) {
    String auth = server.arg("auth");
    auth.trim();
    // Fejlec-injektalas ellen: csak lathato ASCII, szokoz/;/, nelkul (sutiertekbe kerul).
    if (auth.length() > CLAUDE_AUTH_MAX || !printableAscii(auth, false) || auth.indexOf(';') >= 0 || auth.indexOf(',') >= 0)
      return sendError(400, "auth: max 256 visible ASCII chars, no ; or ,");
    strlcpy(p.auth, auth.c_str(), sizeof(p.auth));
  }
  strlcpy(p.name, name.c_str(), sizeof(p.name));
  strlcpy(p.orgId, org.c_str(), sizeof(p.orgId));
  p.enabled = argBool("enabled");
  if (!configManager.saveClaude(idx, p)) return sendError(500, "save failed");
  sendOk();
}

static void handleClaudeDelete() {
  if (!guardPost()) return;
  if (!configManager.deleteClaude(argInt("idx", -1))) return sendError(400, "bad idx");
  sendOk();
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

static bool restartPending = false;
static uint32_t restartAtMs = 0;

static void handleRestart() {
  if (!guardPost()) return;
  sendOk();
  restartPending = true;
  restartAtMs = millis() + 500;  // a valasz meg kimenjen
}

void WebSetup::begin() {
  static const char *headers[] = {"X-CMon"};
  server.collectHeaders(headers, 1);

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
  server.on("/api/scan", HTTP_POST, handleScanStart);
  server.on("/api/scan", HTTP_GET, handleScanResults);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void WebSetup::loop() {
  server.handleClient();
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}
