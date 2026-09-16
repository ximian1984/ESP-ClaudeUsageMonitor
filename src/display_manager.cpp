#include "display_manager.h"

#include <TFT_eSPI.h>

#include "config.h"
#include "config_manager.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "wifi_manager.h"

DisplayManager displayManager;

static TFT_eSPI tft;
static TFT_eSprite fb(&tft);  // 160x80x16 bit = 25,6 KB framebuffer: villogasmentes ujrarajzolas
static bool fbOk = false;

static const int W = 160, H = 80;
static const uint32_t REDRAW_MS = 200;
static const uint32_t STALE_AFTER_MS = 2UL * CLAUDE_REFRESH_PERIOD_S * 1000UL;

// Font 1 = 6x8 GLCD, font 2 = 16 px (platformio.ini: LOAD_GLCD, LOAD_FONT2)
static void text(const String &s, int x, int y, uint16_t color, uint8_t font = 1, uint8_t datum = TL_DATUM) {
  fb.setTextDatum(datum);
  fb.setTextColor(color, TFT_BLACK);
  fb.drawString(s, x, y, font);
}

void DisplayManager::begin() {
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LCD_BL_ON);
  tft.init();
  tft.setRotation(1);  // fekvo 160x80 — LilyGO examples/TFT_eSPI/TFT_eSPI.ino:35
  tft.fillScreen(TFT_BLACK);
  fbOk = fb.createSprite(W, H) != nullptr;
  if (!fbOk) Serial.println("[disp] framebuffer foglalas sikertelen, kozvetlen rajzolas nincs — ures kijelzo");
}

void DisplayManager::push() {
  if (fbOk) fb.pushSprite(0, 0);
}

void DisplayManager::showBoot(uint32_t msLeft) {
  if (!fbOk) return;
  fb.fillSprite(TFT_BLACK);
  text("CLAUDE MONITOR", W / 2, 6, TFT_ORANGE, 2, TC_DATUM);
  text("v" FW_VERSION, W / 2, 26, TFT_DARKGREY, 1, TC_DATUM);
  text("Press BOOT now", W / 2, 44, TFT_WHITE, 1, TC_DATUM);
  text("for setup mode (" + String((msLeft + 999) / 1000) + ")", W / 2, 56, TFT_WHITE, 1, TC_DATUM);
  push();
}

void DisplayManager::drawAp() {
  bool forced = wifiManager.state() == WifiState::ApForced;
  text(forced ? "SETUP MODE" : "NO WIFI - SETUP", 0, 0, forced ? TFT_ORANGE : TFT_RED, 2);
  text("SSID " + wifiManager.apSsid(), 0, 22, TFT_WHITE);
  text("PASS " + wifiManager.apPassword(), 0, 36, TFT_YELLOW);  // AP-jelszo: a spec 9. szerint kijelzendo
  text("http://" + wifiManager.ipString(), 0, 52, TFT_CYAN);
  if (!forced) text("retry every 5 min", 0, 70, TFT_DARKGREY);
}

void DisplayManager::drawWifiWait() {
  text("WIFI", 0, 0, TFT_ORANGE, 2);
  text(wifiManager.stateName(), 0, 24, TFT_WHITE, 2);
  String ssid = wifiManager.staSsid();
  if (ssid.length()) text(ssid, 0, 46, TFT_CYAN);
}

void DisplayManager::drawNoProfiles() {
  text("NO CLAUDE", 0, 0, TFT_ORANGE, 2);
  text("PROFILE", 0, 18, TFT_ORANGE, 2);
  text("Setup:", 0, 44, TFT_WHITE);
  text("http://" + wifiManager.ipString(), 0, 56, TFT_CYAN);
}

// Egy limit ket sora: "LABEL   73% LEFT" + (opcionalis savval) "RESET 02:17:32 @14:30"
static void drawLimit(const UsageLimit *l, const char *fallbackLabel, int y, bool bar, bool dataStale) {
  uint16_t valColor = dataStale ? TFT_DARKGREY : TFT_WHITE;
  if (!l) {
    text(fallbackLabel, 0, y + 4, TFT_LIGHTGREY);
    text("n/a", W, y, TFT_DARKGREY, 2, TR_DATUM);  // spec 16./25.: "ha az API biztositja"
    return;
  }
  text(l->label, 0, y + 4, TFT_LIGHTGREY);
  time_t now = timeManager.now();
  bool resetPassed = l->hasReset && timeManager.synced() && l->resetAt <= now;

  if (l->hasUtilization) {
    float left = constrain(100.0f - l->utilizationPct, 0.0f, 100.0f);
    uint16_t c = resetPassed ? TFT_DARKGREY : (left < 10 ? TFT_RED : left < 30 ? TFT_YELLOW : valColor);
    text(String((int)(left + 0.5f)) + "% LEFT", W, y, c, 2, TR_DATUM);
    if (bar) {
      int by = y + 16;
      fb.drawRect(0, by, W, 6, TFT_DARKGREY);
      int fill = (int)((W - 2) * left / 100.0f);
      if (fill > 0) fb.fillRect(1, by + 1, fill, 4, c == valColor ? TFT_GREEN : c);
    }
  } else {
    text("?", W, y, TFT_DARKGREY, 2, TR_DATUM);
  }

  int ry = y + (bar ? 24 : 17);
  if (!l->hasReset) {
    text("RESET n/a", 0, ry, TFT_DARKGREY);
  } else if (!timeManager.synced()) {
    text("RESET ? (NO TIME)", 0, ry, TFT_RED);  // NTP nelkul nincs hiteles visszaszamlalas
  } else if (resetPassed) {
    text("RESET PASSED - wait data", 0, ry, TFT_DARKGREY);  // spec 18.: nincs hamis countdown
  } else {
    long secs = (long)(l->resetAt - now);
    String when;
    if (secs < 86400) {
      when = TimeManager::localHHMM(l->resetAt);
    } else {
      struct tm lt;
      time_t t = l->resetAt;
      localtime_r(&t, &lt);
      char buf[12];
      strftime(buf, sizeof(buf), "%a %H:%M", &lt);
      when = buf;
    }
    text("RESET " + TimeManager::formatRemaining(secs) + " @" + when, 0, ry, TFT_WHITE);
  }
}

void DisplayManager::drawProfile(int idx, const char *name) {
  ProfileUsage u = usageCache.get(idx);
  text(name, 0, 0, TFT_CYAN, 2);  // a profilnev mindig lathato (spec 12.)

  // Jobb felso sarok: allapot / adat kora
  uint32_t now = millis();
  String status;
  uint16_t sc = TFT_DARKGREY;
  if (!wifiManager.staConnected()) {
    status = "NO WIFI";
    sc = TFT_RED;
  } else if (timeManager.ntpError()) {
    status = "NTP ERR";
    sc = TFT_RED;
  } else if (u.hasData && u.lastError != FetchError::None) {
    status = String("ERR ") + (u.lastHttpStatus > 0 ? String(u.lastHttpStatus) : String(fetchErrorTitle(u.lastError)));
    sc = TFT_RED;
  }
  bool stale = u.hasData && now - u.lastOkMs > STALE_AFTER_MS;
  if (u.hasData) {
    uint32_t ageS = (now - u.lastOkMs) / 1000;
    String age = ageS < 60 ? String(ageS) + "s" : ageS < 3600 ? String(ageS / 60) + "m OLD" : String(ageS / 3600) + "h OLD";
    if (status.isEmpty()) {
      status = age;
      sc = stale ? TFT_YELLOW : TFT_DARKGREY;
    } else {
      text(age, W, 9, TFT_YELLOW, 1, TR_DATUM);  // hiba mellett is latszodjon az adat kora (spec 18.)
    }
  }
  if (status.length()) text(status, W, 0, sc, 1, TR_DATUM);

  if (!u.hasData) {
    // Nincs meg ervenyes adat: hibakepernyo (spec 17.) vagy varakozas.
    if (u.lastError == FetchError::None) {
      text(wifiManager.staConnected() ? (timeManager.synced() ? "loading..." : "waiting NTP") : "waiting WiFi", W / 2, 38,
           TFT_LIGHTGREY, 2, TC_DATUM);
    } else {
      text(fetchErrorTitle(u.lastError), W / 2, 26, TFT_RED, 2, TC_DATUM);
      String line2 = u.lastHttpStatus > 0 ? "ERROR " + String(u.lastHttpStatus) : "ERROR";
      text(line2, W / 2, 46, TFT_RED, 2, TC_DATUM);
    }
    return;
  }

  // Layout (y): 0 nev | 16 SESSION + sav 32..37 | 40 reset | 50 WEEKLY | 67 reset
  drawLimit(u.data.find(LimitKind::Session), "SESSION", 16, true, stale);
  drawLimit(u.data.find(LimitKind::Weekly), "WEEKLY", 50, false, stale);
}

void DisplayManager::loop() {
  uint32_t now = millis();
  if (!fbOk || now - _lastDrawMs < REDRAW_MS) return;
  _lastDrawMs = now;
  fb.fillSprite(TFT_BLACK);

  if (wifiManager.apActive()) {
    drawAp();
    push();
    return;
  }

  DeviceConfig cfg = configManager.snapshot();
  int enabled[MAX_CLAUDE_PROFILES];
  int n = 0;
  for (int i = 0; i < MAX_CLAUDE_PROFILES; i++)
    if (cfg.claude[i].used && cfg.claude[i].enabled) enabled[n++] = i;

  if (n == 0) {
    if (wifiManager.staConnected()) drawNoProfiles();
    else drawWifiWait();
    push();
    return;
  }

  // Rotacio: csak a megjelenitett profilt valtja, lekerest nem indit (spec 13.).
  if (now - _rotSinceMs >= (uint32_t)cfg.rotationSec * 1000UL) {
    _rotSinceMs = now;
    _rotPos++;
  }
  int idx = enabled[_rotPos % n];
  drawProfile(idx, cfg.claude[idx].name);
  push();
}
