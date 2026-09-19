#include "display_manager.h"

#include <TFT_eSPI.h>

#include "config.h"
#include "config_manager.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "wifi_manager.h"

DisplayManager displayManager;

static TFT_eSPI tft;
static TFT_eSprite fb(&tft);  // 160x80x16 bit = 25,6 KB framebuffer: villogasmentes ujrarajzolas (CYD-n 2x-esen kiirva)
static bool fbOk = false;

static const int W = 160, H = 80;
static const uint32_t REDRAW_MS = 200;
static const uint32_t IP_PHASE_S = 3;  // felso sor jobb sarka: 3 s IP-cim, 3 s allapot (adat kora / hiba)
static uint32_t g_staleMs = 2UL * CLAUDE_REFRESH_DEFAULT_S * 1000UL;  // a loop() a konfiguralt periodusbol frissiti

// Font 1 = 6x8 GLCD, font 2 = 16 px (platformio.ini: LOAD_GLCD, LOAD_FONT2)
static void text(const String &s, int x, int y, uint16_t color, uint8_t font = 1, uint8_t datum = TL_DATUM) {
  fb.setTextDatum(datum);
  fb.setTextColor(color, TFT_BLACK);
  fb.drawString(s, x, y, font);
}

// TFT_eSPI setRotation: 1 = fekvo (LilyGO examples/TFT_eSPI/TFT_eSPI.ino:35), 3 = ugyanaz 180 fokkal forgatva.
// A framebuffer (sprite) valtozatlan marad, csak a kiirasa fordul — igy nem kell a rajzolo kodhoz nyulni.
// CYD: az 1/3 ott is a ket fekvo helyzet (TFT_Drivers/ILI9341_Rotation.h, ST7789_Rotation.h), de hogy melyik all
// "jol" a lap USB-csatlakozojahoz kepest, az ⚠ [vason merendo] — a setup-oldali forgatas-pipa mindket esetet fedi.
void DisplayManager::applyFlip(bool flip) {
  if (_flip == (int)flip) return;
  _flip = flip;
  tft.setRotation(flip ? 3 : 1);
  tft.fillScreen(TFT_BLACK);
  Serial.printf("[disp] kijelzo-forgatas: %s\n", flip ? "180 fok" : "alap");
}

void DisplayManager::begin() {
#if defined(PIN_LED_R)
  // A CYD RGB-LED-je aktiv alacsony (config.h): HIGH = ki.
  for (int pin : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
#endif
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LCD_BL_ON);
  tft.init();
  applyFlip(configManager.brief().displayFlip);
  tft.fillScreen(TFT_BLACK);
  fbOk = fb.createSprite(W, H) != nullptr;
  if (!fbOk) Serial.println("[disp] framebuffer foglalas sikertelen, kozvetlen rajzolas nincs — ures kijelzo");
}

const uint16_t *DisplayManager::framebuffer(int &w, int &h) const {
  w = W;
  h = H;
  return fbOk ? (const uint16_t *)fb.getPointer() : nullptr;
}

#if DISPLAY_SCALE > 1
// Nagyobb panel (CYD: 320x240): a 160x80-as vaszon DISPLAY_SCALE-szeres egesz-skalazassal, kozepre igazitva.
// Igy a rajzolo kod es a webes tukor (/api/screen) valtozatlan, a sprite 25,6 KB marad (egy 320x240x16 bites
// sprite 150 KB lenne — a klasszikus ESP32 heapjen a TLS-kezfogas mellett nem biztonsagos; becsles, PLAN.md 2.15).
// A sprite mar a vezetekre valo (bajtcserelt) sorrendben tarol, ezert a kiiras swap nelkul megy — ugyanigy tesz
// a TFT_eSprite::pushSprite() is (TFT_eSPI Extensions/Sprite.cpp:665-668).
static void pushScaled() {
  static uint16_t line[W * DISPLAY_SCALE];
  const int ow = W * DISPLAY_SCALE, oh = H * DISPLAY_SCALE;
  const int x0 = (tft.width() - ow) / 2, y0 = (tft.height() - oh) / 2;
  const uint16_t *src = (const uint16_t *)fb.getPointer();
  bool oldSwap = tft.getSwapBytes();
  tft.setSwapBytes(false);
  tft.startWrite();
  tft.setAddrWindow(x0, y0, ow, oh);
  for (int y = 0; y < H; y++) {
    const uint16_t *row = src + y * W;
    for (int x = 0; x < W; x++)
      for (int k = 0; k < DISPLAY_SCALE; k++) line[x * DISPLAY_SCALE + k] = row[x];
    for (int k = 0; k < DISPLAY_SCALE; k++) tft.pushPixels(line, ow);
  }
  tft.endWrite();
  tft.setSwapBytes(oldSwap);
}
#endif

void DisplayManager::push() {
  if (!fbOk) return;
#if DISPLAY_SCALE > 1
  pushScaled();
#else
  fb.pushSprite(0, 0);
#endif
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

// Egy limit ket sora: "LABEL   73% LEFT" + (opcionalis savval) "RST 02:17:32 @09.17 18:40" / "RST 3d04:12:33 09.24 09:00"
// Az ido a beallitott idozonaban (setup-oldal, NVS "tz").
static void drawLimit(const UsageLimit *l, const char *fallbackLabel, int y, bool bar, bool dataStale) {
  uint16_t valColor = dataStale ? TFT_DARKGREY : TFT_WHITE;
  if (!l) {
    text(fallbackLabel, 0, y + 4, TFT_LIGHTGREY);
    text("n/a", W, y, TFT_DARKGREY, 2, TR_DATUM);  // spec 16./25.: "ha az API biztositja"
    return;
  }
  time_t now = timeManager.now();
  bool resetPassed = l->hasReset && timeManager.synced() && l->resetAt <= now;
  // A cimke (SESSION / WEEKLY / modellnev) szine a maradek szerint zoldbol pirosba megy at (projektgazda, 2026-09-18).
  // Regi/lejart adatnal szurke marad: a szin ne sugalljon friss allapotot.
  uint16_t labelColor = TFT_LIGHTGREY;
  if (l->hasUtilization && !dataStale && !resetPassed)
    labelColor = usageColor565(constrain(100.0f - l->utilizationPct, 0.0f, 100.0f));
  text(l->label, 0, y + 4, labelColor);

  if (l->hasUtilization) {
    float left = constrain(100.0f - l->utilizationPct, 0.0f, 100.0f);
    // A szerver severity-je (mert: "warning" 81 %-os hasznalatnal) is sargara szinez; a kuszobok a tartalek alakra.
    bool warn = l->severity == Severity::Warning || left < 30;
    uint16_t c = resetPassed ? TFT_DARKGREY : (left < 10 ? TFT_RED : warn ? TFT_YELLOW : valColor);
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
  } else {
    // Mindig datummal: "RST 02:17:32 @09.17 18:40" (projektgazda, 2026-09-17). NTP nelkul "@... ?", lejartan PASSED.
    uint16_t c = !timeManager.synced() ? TFT_RED : resetPassed ? TFT_DARKGREY : TFT_WHITE;
    text(TimeManager::resetText(l->resetAt, timeManager.synced(), now), 0, ry, c);
  }
}

// Wi-Fi/adat nelkul: az utolso ismert keret-resetek (NVS). Pontos ido nelkul (ujrainditas utan nincs NTP) csak az
// abszolut idopont latszik, visszaszamlalas es "lejart" itelet nem — azt nem tudhatjuk.
static void lkBlock(const char *label, time_t reset, int y) {
  text(label, 0, y, TFT_LIGHTGREY);
  if (!reset) {
    text("n/a", W, y, TFT_DARKGREY, 1, TR_DATUM);
    return;
  }
  // "RST 3d04:12:33 09.24 09:00" a cimke alatt (egy sorba cimkevel nem ferne ki)
  text(TimeManager::resetText(reset, timeManager.synced(), timeManager.now()), 0, y + 9, TFT_WHITE);
}

void DisplayManager::drawLastKnown(const char *name, const LastKnownResets &lk) {
  text(name, 0, 0, TFT_CYAN, 2);
  text(wifiManager.staConnected() ? "NO DATA" : "NO WIFI", W, 0, TFT_RED, 1, TR_DATUM);
  // y: 0 nev | 17 SESSION | 26 reset | 40 WEEKLY | 49 reset | 64 "last known" + mentes ideje
  lkBlock("SESSION (last known)", lk.sessionReset, 17);
  lkBlock("WEEKLY (last known)", lk.weeklyReset, 40);
  String saved = lk.savedEpoch >= 1704067200 ? TimeManager::localDateTime(lk.savedEpoch).substring(5) : String("?");
  text("data from " + saved, 0, 68, TFT_DARKGREY);  // "data from 09-17 15:54" = 21 kar.
}

void DisplayManager::drawProfile(int idx, const char *name) {
  ProfileUsage u = usageCache.get(idx);

  // Beszedes ujra-belepes kepernyo (OAuth refresh token elhalt) — a regi adat felett is (spec 17.).
  if (u.lastError == FetchError::ReloginRequired) {
    text(name, 0, 0, TFT_CYAN, 2);
    text("!", W, 0, TFT_RED, 2, TR_DATUM);
    text("RE-LOGIN NEEDED", W / 2, 24, TFT_RED, 2, TC_DATUM);
    text("open setup page", W / 2, 46, TFT_LIGHTGREY, 1, TC_DATUM);
    text("http://" + wifiManager.ipString(), W / 2, 58, TFT_CYAN, 1, TC_DATUM);
    return;
  }

  uint32_t now = millis();

  // Felso sor: profilnev (mindig lathato, spec 12.) + jobb sarokban FELVALTVA az IP-cim es az allapot
  // (projektgazda, 2026-09-17). Mert szelessegek (TFT_eSPI widtbl_f16 + 6 px/kar.): "192.168.x.x" 90 px,
  // "xiTech" 2-es betuvel 39 px, 12 szeles karakter 120 px -> hosszu nevnel az IP-fazisban a nev kisebb betuvel,
  // szukseg eseten csonkitva jelenik meg.
  String ip = wifiManager.staConnected() ? wifiManager.ipString() : String();
  bool ipPhase = ip.length() > 0 && (now / 1000) % (2 * IP_PHASE_S) < IP_PHASE_S;
  if (ipPhase) {
    int ipW = fb.textWidth(ip, 1);
    if (fb.textWidth(name, 2) + 4 + ipW <= W) {
      text(name, 0, 0, TFT_CYAN, 2);
    } else {
      String n = name;
      while (n.length() && fb.textWidth(n, 1) + 4 + ipW > W) n.remove(n.length() - 1);
      text(n, 0, 4, TFT_CYAN, 1);
    }
    text(ip, W, 0, TFT_LIGHTGREY, 1, TR_DATUM);
  } else {
    text(name, 0, 0, TFT_CYAN, 2);
  }

  // Jobb felso sarok (nem-IP fazis): allapot / adat kora
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
  bool stale = u.hasData && now - u.lastOkMs > g_staleMs;
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
  if (status.length() && !ipPhase) text(status, W, 0, sc, 1, TR_DATUM);

  if (!u.hasData) {
    // Nincs friss adat, de van mentett reset-idopont: azt mutatjuk "waiting WiFi"/hiba helyett.
    LastKnownResets lk = usageCache.lastKnown(idx);
    if (lk.any() && (!wifiManager.staConnected() || u.lastError != FetchError::None || !timeManager.synced())) {
      fb.fillSprite(TFT_BLACK);
      drawLastKnown(name, lk);
      return;
    }
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

  // Layout (y): 0 nev | 16 SESSION (ertek 16..31, sav 32..37, reset 40..47) | 48 WEEKLY (48..63, sav 64..69, reset 72..79).
  // A 80 px-es kijelzo igy pont ket savot fogad (projektgazda, 2026-09-18).
  drawLimit(u.data.find(LimitKind::Session), "SESSION", 16, true, stale);
  // Egyetlen limitet ado szolgaltatonal (pl. Grok CREDITS) ne legyen ures "WEEKLY n/a" blokk.
  if (u.data.find(LimitKind::Weekly) || u.data.count != 1)
    drawLimit(u.data.find(LimitKind::Weekly), "WEEKLY", 48, true, stale);
}

void DisplayManager::loop() {
  uint32_t now = millis();
  if (!fbOk || now - _lastDrawMs < REDRAW_MS) return;
  _lastDrawMs = now;
  fb.fillSprite(TFT_BLACK);

  ClaudeBrief b = configManager.brief();
  applyFlip(b.displayFlip);  // a setup-oldalon barmikor atallithato
  g_staleMs = 2UL * (uint32_t)b.refreshSec * 1000UL;
  int n = b.count;

  // AP-mod: forced setupban csak a setup-kepernyo. Fallbackban (nincs Wi-Fi) a setup-kepernyo es az utolso ismert
  // resetek valtakoznak (projektgazda, 2026-09-17), hogy Wi-Fi nelkul is latszodjon, mikor ujul a keret.
  if (wifiManager.apActive()) {
    bool anyLk = false;
    for (int i = 0; i < n; i++) anyLk = anyLk || usageCache.lastKnown(b.idx[i]).any();
    if (wifiManager.state() == WifiState::ApForced || !anyLk) {
      drawAp();
      push();
      return;
    }
    if (now - _rotSinceMs >= (uint32_t)b.rotationSec * 1000UL) {
      _rotSinceMs = now;
      _rotPos++;
    }
    int slot = _rotPos % (n + 1);
    if (slot == n) drawAp();
    else drawProfile(b.idx[slot], b.name[slot]);
    push();
    return;
  }

  if (n == 0) {
    if (wifiManager.staConnected()) drawNoProfiles();
    else drawWifiWait();
    push();
    return;
  }

  // Rotacio: csak a megjelenitett profilt valtja, lekerest nem indit (spec 13.).
  if (now - _rotSinceMs >= (uint32_t)b.rotationSec * 1000UL) {
    _rotSinceMs = now;
    _rotPos++;
  }
  int slot = _rotPos % n;
  drawProfile(b.idx[slot], b.name[slot]);
  push();
}
