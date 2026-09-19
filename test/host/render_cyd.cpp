// A CYD nativ 320x240-es elrendezesenek gepi (host) kirajzolasa PNG-be — a VALODI src/display_cyd.cpp kodjaval, a
// TFT_eSPI sajat font-tablaival (render_stub/TFT_eSPI.h). Savonkent rajzol, ugyanugy, mint a vason, igy a savhatarok
// illesztese is latszik. Amit NEM mutat: a panelt (driver, szinsorrend, invertalas, gamma) — az csak vason dol el.
// Futtatas: sh test/host/render_cyd.sh <kimeneti mappa>
#include <Arduino.h>
#include <zlib.h>

#include <memory>
#include <vector>

// A tesztnek kell az allapotgepek belso allapota (Wi-Fi-allapot, rotacio); csak ebben a forditasi egysegben.
#define private public
#include "config_manager.h"
#include "display_manager.h"
#include "wifi_manager.h"
#undef private
#include "time_manager.h"
#include "usage_cache.h"

uint32_t g_hostMillis = 1000000;
std::vector<uint16_t> g_screen(320 * 240);

// --- A kijelzo altal hasznalt modulok stubjai (a valodi .cpp-k NVS-t / Wi-Fi-t / mutexet hasznalnak) ---
static String g_ip = "192.168.x.x";
static ClaudeBrief g_brief;
static ProfileUsage g_pu;
static LastKnownResets g_lk;
WifiManager wifiManager;
String WifiManager::ipString() const { return g_ip; }
const char *WifiManager::stateName() const { return "CONNECTING"; }
ConfigManager configManager;
ClaudeBrief ConfigManager::brief() { return g_brief; }
UsageCache usageCache;
ProfileUsage UsageCache::get(int) { return g_pu; }
LastKnownResets UsageCache::lastKnown(int) { return g_lk; }

static void writePng(const std::string &path) {
  const int w = 320, h = 240;
  std::vector<unsigned char> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      uint16_t c = g_screen[y * w + x];
      raw.push_back((unsigned char)(((c >> 11) & 0x1f) * 255 / 31));
      raw.push_back((unsigned char)(((c >> 5) & 0x3f) * 255 / 63));
      raw.push_back((unsigned char)((c & 0x1f) * 255 / 31));
    }
  }
  uLongf zlen = compressBound(raw.size());
  std::vector<unsigned char> z(zlen);
  compress(z.data(), &zlen, raw.data(), raw.size());
  z.resize(zlen);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  auto be32 = [&](uint32_t v) { unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v}; fwrite(b, 1, 4, f); };
  auto chunk = [&](const char *type, const std::vector<unsigned char> &d) {
    be32(d.size());
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), d.begin(), d.end());
    fwrite(td.data(), 1, td.size(), f);
    be32(crc32(0, td.data(), td.size()));
  };
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  fwrite(sig, 1, 8, f);
  chunk("IHDR", {0, 0, 1, 64, 0, 0, 0, 240, 8, 2, 0, 0, 0});  // 320x240, 8 bit RGB
  chunk("IDAT", z);
  chunk("IEND", {});
  fclose(f);
}

static void setBrief(int count, const char *name) {
  g_brief = ClaudeBrief();
  g_brief.count = count;
  g_brief.rotationSec = 60;
  g_brief.refreshSec = 180;
  for (int i = 0; i < count; i++) {
    g_brief.idx[i] = i;
    strlcpy(g_brief.name[i], i == 0 ? name : "Other", sizeof(g_brief.name[i]));
  }
}

static UsageLimit limit(LimitKind k, const char *label, float util, long resetInS, Severity sev = Severity::Normal) {
  UsageLimit l;
  l.kind = k;
  strlcpy(l.label, label, sizeof(l.label));
  l.hasUtilization = true;
  l.utilizationPct = util;
  l.hasReset = resetInS != 0;
  l.resetAt = time(nullptr) + resetInS;
  l.severity = sev;
  return l;
}

static void shot(const std::string &dir, const char *name) {
  g_hostMillis += 1000;  // > REDRAW_MS, de a 60 s-os rotacio alatt
  displayManager._rotPos = 0;
  displayManager._rotSinceMs = g_hostMillis;
  displayManager.loop();
  writePng(dir + "/" + name + ".png");
  printf("%s/%s.png\n", dir.c_str(), name);
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  displayManager.begin();

  // 1) Ket keret, normal: 73 % es 12 % (heti: szerver-warning), 3 profil kozul az elso, friss adat.
  wifiManager._state = WifiState::Connected;
  setBrief(3, "xiTech");
  g_pu = ProfileUsage();
  g_pu.hasData = true;
  g_pu.lastOkMs = g_hostMillis - 12000;
  g_pu.data.limits[0] = limit(LimitKind::Session, "SESSION", 27, 2 * 3600 + 17 * 60 + 32);
  g_pu.data.limits[1] = limit(LimitKind::Weekly, "WEEKLY", 88, 3 * 86400 + 4 * 3600 + 12 * 60, Severity::Warning);
  g_pu.data.count = 2;
  g_lk = LastKnownResets();
  shot(dir, "1_ket_keret");

  // 2) Hiba regi adat felett: ERR 429 + "7m OLD", az adat szurke (stale).
  g_pu.lastError = FetchError::RateLimited;
  g_pu.lastHttpStatus = 429;
  g_pu.lastOkMs = g_hostMillis + 1000 - 7 * 60 * 1000;
  shot(dir, "2_hiba_regi_adat");

  // 3) Szelsoertekek: 0 % (piros) es 100 %, hosszu (12 karakteres) profilnev.
  setBrief(2, "Profile-Long");
  g_pu.lastError = FetchError::None;
  g_pu.lastHttpStatus = 0;
  g_pu.lastOkMs = g_hostMillis - 3000;
  g_pu.data.limits[0] = limit(LimitKind::Session, "SESSION", 100, 45 * 60);
  g_pu.data.limits[1] = limit(LimitKind::Weekly, "WEEKLY", 0, 6 * 86400 + 23 * 3600);
  shot(dir, "3_szelsoertekek");

  // 4) Egyetlen keret (Grok CREDITS), reset nelkul.
  setBrief(1, "Grok");
  g_pu.data.limits[0] = limit(LimitKind::Session, "CREDITS", 38, 0);
  g_pu.data.count = 1;
  shot(dir, "4_egy_keret");

  // 5) Ujra-belepes kell.
  setBrief(1, "xiTech");
  g_pu.lastError = FetchError::ReloginRequired;
  shot(dir, "5_relogin");

  // 6) Betoltes (nincs meg adat).
  g_pu = ProfileUsage();
  shot(dir, "6_betoltes");

  // 7) Wi-Fi nelkul: utolso ismert resetek.
  wifiManager._state = WifiState::Connecting;
  g_lk.sessionReset = time(nullptr) + 3600;
  g_lk.weeklyReset = time(nullptr) + 5 * 86400;
  g_lk.savedEpoch = time(nullptr) - 7200;
  shot(dir, "7_utolso_ismert");

  // 8) Setup-AP (fallback): SSID, jelszo, cim.
  wifiManager._state = WifiState::ApFallback;
  wifiManager._apSsid = "ClaudeMonitor-A1B2";
  wifiManager._apPassword = "k7Qx9mPzR2";
  g_ip = "192.168.4.1";
  setBrief(0, "");
  g_lk = LastKnownResets();
  shot(dir, "8_setup_ap");

  // 9) Inditasi ablak.
  displayManager.showBoot(2400);
  writePng(dir + "/9_boot.png");
  printf("%s/9_boot.png\n", dir.c_str());
  return 0;
}
