// ESP32-2432S028R (CYD) nativ 320x240-es (fekvo) megjelenites. A dongle 160x80-as utja: display_manager.cpp.
// Csak a cache-bol es az allapotokbol rajzol, API-t soha nem hiv (spec 13.).
//
// Memoria (tervdoksi 2.16): NINCS teljes kepernyos sprite (320x240x16 bit = 153,6 KB). Egyetlen 320x40-es
// sav-sprite (25,6 KB, annyi, mint a dongle framebuffere) jarja vegig a hat savot: savonkent a TELJES elrendezes
// ujrarajzolodik a sav eltolasaval, a sprite a kilogo reszt levagja (a karakterrajzolas vagott agon pixelenkent
// rajzol: TFT_eSPI Extensions/Sprite.cpp drawChar). Villogasmentes, es a webes tukor ugyanigy, savonkent kapja a kepet.
// ⚠ Az elrendezes aranyai es olvashatosaga fizikai lapon NEM ellenorzott.
#include "display_manager.h"

#if defined(BOARD_CYD)

#include <TFT_eSPI.h>

#include "config.h"
#include "config_manager.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "wifi_manager.h"

DisplayManager displayManager;

static TFT_eSPI tft;
static TFT_eSprite band(&tft);
static bool bandOk = false;

static const int W = 320, H = 240;
static const int BAND_H = 40;           // 6 sav; 320x40x2 B = 25 600 B
static const int R = W - 6;             // jobb margo (jobbra igazitott szovegek)
static const uint32_t REDRAW_MS = 500;  // egy kep 153,6 KB az SPI-n; a masodperces visszaszamlalashoz ennyi eleg
static const uint32_t STATS_LOG_MS = 60000;
static uint32_t g_staleMs = 2UL * CLAUDE_REFRESH_DEFAULT_S * 1000UL;  // a loop() a konfiguralt periodusbol frissiti
static int g_by = 0;                    // az eppen rajzolt sav teteje, kepernyo-koordinataban

// Fontok (platformio.ini [cyd_common]): 2 = 16 px, 4 = 26 px, 6 = 48 px (csak szamjegyek es ":-.apm").

// --- Rajzolo primitivek kepernyo-koordinataban; a sav eltolasat itt vonjuk le, a savon kivulit kihagyjuk. ---
static bool outside(int y, int h) { return y >= g_by + BAND_H || y + h <= g_by; }

// Csak felso datum (TL/TC/TR): y a szoveg teteje. Hatter nelkul (atlatszo): a sav minden kepnel feketere torlodik.
static void text(const String &s, int x, int y, uint16_t color, uint8_t font, uint8_t datum = TL_DATUM) {
  if (outside(y, band.fontHeight(font))) return;
  band.setTextDatum(datum);
  band.setTextColor(color);
  band.drawString(s, x, y - g_by, font);
}

// A 4-es fontot hasznalja, ha a szoveg kifer a maxW szelessegbe, kulonben a 2-eset. A valasztott fontot adja vissza.
static uint8_t fitFont(const String &s, int maxW) { return band.textWidth(s, 4) <= maxW ? 4 : 2; }

static void textFit(const String &s, int x, int y, uint16_t color, int maxW, uint8_t datum = TL_DATUM) {
  uint8_t f = fitFont(s, maxW);
  text(s, x, f == 4 ? y : y + 5, color, f, datum);  // a 16 px-es sor a 26 px-es hely kozepere
}

static void fillRectS(int x, int y, int w, int h, uint16_t c) {
  if (!outside(y, h)) band.fillRect(x, y - g_by, w, h, c);
}

static void drawRectS(int x, int y, int w, int h, uint16_t c) {
  if (!outside(y, h)) band.drawRect(x, y - g_by, w, h, c);
}

// --- Allapot -------------------------------------------------------------------------------------------------------
// A loop() donti el, mi latszik (Frame); egy kep elott egyszer osszegyujtjuk a hozza kello adatot (Model), es a hat
// sav ebbol rajzol — igy a cache-masolat/mutex kepenkent egyszer fut, nem savonkent.
enum class Screen : uint8_t { Boot, Ap, WifiWait, NoProfiles, Profile };

struct Frame {
  Screen screen = Screen::Boot;
  uint32_t bootMsLeft = 0;
  int idx = 0;    // profil-index (config)
  int slot = 0;   // hanyadik a rotacioban (0..count-1)
  int count = 0;  // hany profil forog
  char name[CLAUDE_NAME_MAX + 1] = "";
};

struct Model {
  Frame f;
  bool sta = false, apForced = false, synced = false, ntpErr = false;
  String ip, apSsid, apPass, staSsid, stateName;
  time_t now = 0;
  uint32_t nowMs = 0;
  ProfileUsage u;
  LastKnownResets lk;
};

static Frame g_frame;
static Model g_m;

static void prepare(const Frame &f) {
  g_m.f = f;
  g_m.sta = wifiManager.staConnected();
  g_m.apForced = wifiManager.state() == WifiState::ApForced;
  g_m.ip = wifiManager.ipString();
  g_m.apSsid = wifiManager.apSsid();
  g_m.apPass = wifiManager.apPassword();
  g_m.staSsid = wifiManager.staSsid();
  g_m.stateName = wifiManager.stateName();
  g_m.synced = timeManager.synced();
  g_m.ntpErr = timeManager.ntpError();
  g_m.now = timeManager.now();
  g_m.nowMs = millis();
  if (f.screen == Screen::Profile) {
    g_m.u = usageCache.get(f.idx);
    g_m.lk = usageCache.lastKnown(f.idx);
  }
}

// --- Kepernyok -----------------------------------------------------------------------------------------------------
static void drawBoot(const Model &m) {
  text("CLAUDE MONITOR", W / 2, 44, TFT_ORANGE, 4, TC_DATUM);
  text("v" FW_VERSION "  -  " BOARD_NAME, W / 2, 78, TFT_DARKGREY, 2, TC_DATUM);
  text("Press BOOT now", W / 2, 124, TFT_WHITE, 4, TC_DATUM);
  text("for setup mode (" + String((m.f.bootMsLeft + 999) / 1000) + ")", W / 2, 158, TFT_WHITE, 4, TC_DATUM);
}

static void drawAp(const Model &m) {
  text(m.apForced ? "SETUP MODE" : "NO WIFI - SETUP", W / 2, 8, m.apForced ? TFT_ORANGE : TFT_RED, 4, TC_DATUM);
  fillRectS(6, 40, W - 12, 1, TFT_DARKGREY);
  text("Wi-Fi name", 6, 50, TFT_LIGHTGREY, 2);
  textFit(m.apSsid, 6, 68, TFT_WHITE, W - 12);
  text("Password", 6, 102, TFT_LIGHTGREY, 2);
  textFit(m.apPass, 6, 120, TFT_YELLOW, W - 12);  // AP-jelszo: a spec 9. szerint kijelzendo
  text("Then open", 6, 154, TFT_LIGHTGREY, 2);
  textFit("http://" + m.ip, 6, 172, TFT_CYAN, W - 12);
  if (!m.apForced) text("retrying Wi-Fi every 5 min", W / 2, 218, TFT_DARKGREY, 2, TC_DATUM);
}

static void drawWifiWait(const Model &m) {
  text("WIFI", W / 2, 60, TFT_ORANGE, 4, TC_DATUM);
  text(m.stateName, W / 2, 104, TFT_WHITE, 4, TC_DATUM);
  if (m.staSsid.length()) text(m.staSsid, W / 2, 148, TFT_CYAN, 2, TC_DATUM);
}

static void drawNoProfiles(const Model &m) {
  text("NO CLAUDE PROFILE", W / 2, 60, TFT_ORANGE, 4, TC_DATUM);
  text("Add one on the setup page:", W / 2, 112, TFT_LIGHTGREY, 2, TC_DATUM);
  textFit("http://" + m.ip, W / 2, 136, TFT_CYAN, W - 12, TC_DATUM);
}

// Felso sor: profilnev balra, jobbra az allapot (adat kora / hiba). A dongle 3 s-onkent valtogatta az IP-cimmel;
// itt az IP a lablecben allandoan latszik.
static void drawHeader(const Model &m, const String &right, uint16_t rightColor, const String &right2) {
  int rw = right.length() ? band.textWidth(right, 2) : 0;
  if (right2.length()) rw = max(rw, (int)band.textWidth(right2, 2));
  String n = m.f.name;
  int avail = R - 6 - (rw ? rw + 8 : 0);
  if (band.textWidth(n, 4) <= avail) {
    text(n, 6, 2, TFT_CYAN, 4);
  } else {
    while (n.length() && band.textWidth(n, 2) > avail) n.remove(n.length() - 1);
    text(n, 6, 7, TFT_CYAN, 2);
  }
  if (right2.length()) {  // hiba + adat kora ket sorban (spec 18.)
    text(right, R, 0, rightColor, 2, TR_DATUM);
    text(right2, R, 14, TFT_YELLOW, 2, TR_DATUM);
  } else if (right.length()) {
    text(right, R, 7, rightColor, 2, TR_DATUM);
  }
  fillRectS(0, 29, W, 1, TFT_DARKGREY);
}

static void drawFooter(const Model &m) {
  if (m.sta) text("http://" + m.ip, 6, 223, TFT_DARKGREY, 2);
  if (m.f.count > 1) text(String(m.f.slot + 1) + "/" + String(m.f.count), R, 223, TFT_DARKGREY, 2, TR_DATUM);
}

// A reset sora (y .. y+26): a visszaszamlalas nagy (4-es), a datum kicsi, jobbra: "RST  02:17:32     @09.20 00:02".
// Ora nelkul vagy lejartan a TimeManager::resetText() szovege megy (ugyanaz, mint a dongle-on), mert ott nincs mit
// visszaszamolni. A datum formaja a resetText-e ("%m.%d %H:%M", helyi idoben).
static void resetRow(const Model &m, time_t resetAt, int y, uint16_t color) {
  if (!m.synced || resetAt <= m.now) {
    textFit(TimeManager::resetText(resetAt, m.synced, m.now), 6, y, color, W - 12);
    return;
  }
  struct tm lt;
  localtime_r(&resetAt, &lt);
  char when[16];
  strftime(when, sizeof(when), "@%m.%d %H:%M", &lt);
  text("RST", 6, y + 7, TFT_DARKGREY, 2);
  text(TimeManager::formatRemaining((long)(resetAt - m.now)), 40, y, color, 4);
  text(when, R, y + 7, TFT_LIGHTGREY, 2, TR_DATUM);
}

// Egy limit blokkja (y .. y+92):
//   cimke (4-es, a maradek szerinti szinnel) + alatta "left"      |  jobbra a nagy szazalek (6-os szam + 4-es "%")
//   sav (y+52, 12 px magas)
//   reset: resetRow() (y+66)
static void drawLimit(const Model &m, const UsageLimit *l, const char *fallbackLabel, int y, bool dataStale) {
  if (!l) {
    text(fallbackLabel, 6, y + 12, TFT_LIGHTGREY, 4);
    text("n/a", R, y + 12, TFT_DARKGREY, 4, TR_DATUM);  // spec 16./25.: "ha az API biztositja"
    return;
  }
  uint16_t valColor = dataStale ? TFT_DARKGREY : TFT_WHITE;
  bool resetPassed = l->hasReset && m.synced && l->resetAt <= m.now;
  // A cimke szine a maradek szerint zoldbol pirosba megy at (projektgazda, 2026-09-18); regi/lejart adatnal szurke.
  uint16_t labelColor = TFT_LIGHTGREY;
  if (l->hasUtilization && !dataStale && !resetPassed)
    labelColor = usageColor565(constrain(100.0f - l->utilizationPct, 0.0f, 100.0f));
  text(l->label, 6, y + 2, labelColor, 4);

  if (l->hasUtilization) {
    float left = constrain(100.0f - l->utilizationPct, 0.0f, 100.0f);
    // A szerver severity-je (mert: "warning" 81 %-os hasznalatnal) is sargara szinez (ugyanaz, mint a dongle-on).
    bool warn = l->severity == Severity::Warning || left < 30;
    uint16_t c = resetPassed ? TFT_DARKGREY : (left < 10 ? TFT_RED : warn ? TFT_YELLOW : valColor);
    text("left", 6, y + 30, TFT_DARKGREY, 2);
    int pw = band.textWidth("%", 4);
    text("%", R, y + 20, c, 4, TR_DATUM);  // a 26 px-es "%" alja a 48 px-es szamok aljahoz
    text(String((int)(left + 0.5f)), R - pw - 2, y, c, 6, TR_DATUM);
    drawRectS(6, y + 52, W - 12, 12, TFT_DARKGREY);
    int fill = (int)((W - 14) * left / 100.0f);
    if (fill > 0) fillRectS(7, y + 53, fill, 10, c == valColor ? TFT_GREEN : c);
  } else {
    text("?", R, y + 12, TFT_DARKGREY, 4, TR_DATUM);
  }

  if (!l->hasReset) {
    text("RESET n/a", 6, y + 71, TFT_DARKGREY, 2);
  } else {
    // Mindig datummal (projektgazda, 2026-09-17). NTP nelkul piros, lejartan szurke.
    uint16_t c = !m.synced ? TFT_RED : resetPassed ? TFT_DARKGREY : TFT_WHITE;
    resetRow(m, l->resetAt, y + 66, c);
  }
}

// Wi-Fi/adat nelkul: az utolso ismert keret-resetek (NVS). Pontos ido nelkul (ujrainditas utan nincs NTP) csak az
// abszolut idopont latszik, visszaszamlalas es "lejart" itelet nem — azt nem tudhatjuk.
static void lkBlock(const Model &m, const char *label, time_t reset, int y) {
  text(label, 6, y, TFT_LIGHTGREY, 4);
  if (!reset) {
    text("n/a", R, y, TFT_DARKGREY, 4, TR_DATUM);
    return;
  }
  resetRow(m, reset, y + 30, TFT_WHITE);
}

static void drawLastKnown(const Model &m) {
  drawHeader(m, m.sta ? "NO DATA" : "NO WIFI", TFT_RED, "");
  text("last known resets", 6, 38, TFT_DARKGREY, 2);
  lkBlock(m, "SESSION", m.lk.sessionReset, 60);
  lkBlock(m, "WEEKLY", m.lk.weeklyReset, 124);
  String saved = m.lk.savedEpoch >= 1704067200 ? TimeManager::localDateTime(m.lk.savedEpoch).substring(5) : String("?");
  text("data from " + saved, 6, 196, TFT_DARKGREY, 2);
  drawFooter(m);
}

static void drawProfile(const Model &m) {
  const ProfileUsage &u = m.u;

  // Beszedes ujra-belepes kepernyo (OAuth refresh token elhalt) — a regi adat felett is (spec 17.).
  if (u.lastError == FetchError::ReloginRequired) {
    drawHeader(m, "", TFT_RED, "");
    text("!", R, 2, TFT_RED, 4, TR_DATUM);
    text("RE-LOGIN NEEDED", W / 2, 76, TFT_RED, 4, TC_DATUM);
    text("open the setup page:", W / 2, 118, TFT_LIGHTGREY, 2, TC_DATUM);
    textFit("http://" + m.ip, W / 2, 140, TFT_CYAN, W - 12, TC_DATUM);
    return;
  }

  // Jobb felso sarok: allapot / adat kora (ugyanaz a logika, mint a dongle-on)
  String status, age;
  uint16_t sc = TFT_DARKGREY;
  if (!m.sta) {
    status = "NO WIFI";
    sc = TFT_RED;
  } else if (m.ntpErr) {
    status = "NTP ERR";
    sc = TFT_RED;
  } else if (u.hasData && u.lastError != FetchError::None) {
    status = String("ERR ") + (u.lastHttpStatus > 0 ? String(u.lastHttpStatus) : String(fetchErrorTitle(u.lastError)));
    sc = TFT_RED;
  }
  bool stale = u.hasData && m.nowMs - u.lastOkMs > g_staleMs;
  if (u.hasData) {
    uint32_t ageS = (m.nowMs - u.lastOkMs) / 1000;
    age = ageS < 60 ? String(ageS) + "s" : ageS < 3600 ? String(ageS / 60) + "m OLD" : String(ageS / 3600) + "h OLD";
    if (status.isEmpty()) {
      status = age;
      sc = stale ? TFT_YELLOW : TFT_DARKGREY;
      age = "";
    }
  }

  if (!u.hasData) {
    // Nincs friss adat, de van mentett reset-idopont: azt mutatjuk "waiting WiFi"/hiba helyett.
    if (m.lk.any() && (!m.sta || u.lastError != FetchError::None || !m.synced)) {
      drawLastKnown(m);
      return;
    }
    drawHeader(m, status, sc, "");
    if (u.lastError == FetchError::None) {
      text(m.sta ? (m.synced ? "loading..." : "waiting NTP") : "waiting WiFi", W / 2, 104, TFT_LIGHTGREY, 4, TC_DATUM);
    } else {
      text(fetchErrorTitle(u.lastError), W / 2, 88, TFT_RED, 4, TC_DATUM);
      text(u.lastHttpStatus > 0 ? "ERROR " + String(u.lastHttpStatus) : String("ERROR"), W / 2, 124, TFT_RED, 4, TC_DATUM);
    }
    drawFooter(m);
    return;
  }

  drawHeader(m, status, sc, age);
  // Layout (y): 0 fejlec | 32 SESSION (..124) | 127 WEEKLY (..219) | 223 lablec. Egyetlen limitet ado szolgaltatonal
  // (pl. Grok CREDITS) a blokk kozepre kerul, ures "WEEKLY n/a" nelkul.
  const UsageLimit *weekly = u.data.find(LimitKind::Weekly);
  if (weekly || u.data.count != 1) {
    drawLimit(m, u.data.find(LimitKind::Session), "SESSION", 32, stale);
    drawLimit(m, weekly, "WEEKLY", 127, stale);
  } else {
    const UsageLimit *only = u.data.find(LimitKind::Session);
    drawLimit(m, only ? only : &u.data.limits[0], "SESSION", 80, stale);
  }
  drawFooter(m);
}

static void drawModel(const Model &m) {
  switch (m.f.screen) {
    case Screen::Boot: drawBoot(m); break;
    case Screen::Ap: drawAp(m); break;
    case Screen::WifiWait: drawWifiWait(m); break;
    case Screen::NoProfiles: drawNoProfiles(m); break;
    case Screen::Profile: drawProfile(m); break;
  }
}

// --- Kiiras --------------------------------------------------------------------------------------------------------
static uint32_t g_frameUsMax = 0;

static void render() {
  if (!bandOk) return;
  uint32_t t0 = micros();
  prepare(g_frame);
  for (g_by = 0; g_by < H; g_by += BAND_H) {
    band.fillSprite(TFT_BLACK);
    drawModel(g_m);
    band.pushSprite(0, g_by);
  }
  uint32_t dt = micros() - t0;
  if (dt > g_frameUsMax) g_frameUsMax = dt;
}

// TFT_eSPI setRotation: 1 = fekvo, 3 = ugyanaz 180 fokkal (TFT_Drivers/ILI9341_Rotation.h, ST7789_Rotation.h).
// Hogy melyik all "jol" a lap USB-csatlakozojahoz kepest, az ⚠ [vason merendo] — a setup-oldali pipa mindkettot fedi.
void DisplayManager::applyFlip(bool flip) {
  if (_flip == (int)flip) return;
  _flip = flip;
  tft.setRotation(flip ? 3 : 1);
  Serial.printf("[disp] kijelzo-forgatas: %s\n", flip ? "180 fok" : "alap");
}

void DisplayManager::begin() {
  // A CYD RGB-LED-je aktiv alacsony (config.h): HIGH = ki.
  for (int pin : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LCD_BL_ON);
  tft.init();
  applyFlip(configManager.brief().displayFlip);
  tft.fillScreen(TFT_BLACK);
  bandOk = band.createSprite(W, BAND_H) != nullptr;
  // A heap-szamok a vason merendo TLS-tartalekhoz (tervdoksi 2.16): a legnagyobb szabad blokk a lenyeges.
  Serial.printf("[disp] CYD %dx%d, sav-sprite %dx%d %s; heap szabad %u B, legnagyobb blokk %u B\n", W, H, W, BAND_H,
                bandOk ? "ok" : "SIKERTELEN", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

const uint16_t *DisplayManager::framebuffer(int &w, int &h) const {
  w = W;
  h = H;
  return nullptr;  // nincs teljes framebuffer: streamScreen()
}

bool DisplayManager::screenSize(int &w, int &h) const {
  w = W;
  h = H;
  return bandOk;
}

// A webes tukor: ugyanaz a kep, savonkent ujrarajzolva. Az aktualis Frame-et hasznalja, a rotaciot nem lepteti.
// Ugyanabban a taskban (loopTask) fut, mint a loop(), ezert a kozos sav-sprite-hoz nem kell zar.
void DisplayManager::streamScreen(ScreenSink sink) {
  if (!bandOk) return;
  prepare(g_frame);
  for (g_by = 0; g_by < H; g_by += BAND_H) {
    band.fillSprite(TFT_BLACK);
    drawModel(g_m);
    sink((const uint8_t *)band.getPointer(), (size_t)W * BAND_H * 2);
  }
}

void DisplayManager::showBoot(uint32_t msLeft) {
  g_frame = Frame();
  g_frame.screen = Screen::Boot;
  g_frame.bootMsLeft = msLeft;
  render();
}

static void setProfileFrame(const ClaudeBrief &b, int slot, int count) {
  g_frame = Frame();
  g_frame.screen = Screen::Profile;
  g_frame.idx = b.idx[slot];
  g_frame.slot = slot;
  g_frame.count = count;
  strlcpy(g_frame.name, b.name[slot], sizeof(g_frame.name));
}

static void setFrame(Screen s) {
  g_frame = Frame();
  g_frame.screen = s;
}

void DisplayManager::loop() {
  uint32_t now = millis();
  if (!bandOk || now - _lastDrawMs < REDRAW_MS) return;
  _lastDrawMs = now;

  ClaudeBrief b = configManager.brief();
  applyFlip(b.displayFlip);  // a setup-oldalon barmikor atallithato
  g_staleMs = 2UL * (uint32_t)b.refreshSec * 1000UL;
  int n = b.count;

  if (now - _rotSinceMs >= (uint32_t)b.rotationSec * 1000UL) {
    _rotSinceMs = now;
    _rotPos++;
  }

  // Ugyanaz a dontes, mint a dongle-on (display_manager.cpp): AP-modban forced setupnal csak a setup-kepernyo,
  // fallbackban a setup-kepernyo es az utolso ismert resetek valtakoznak; kulonben profil-rotacio (lekerest nem indit).
  if (wifiManager.apActive()) {
    bool anyLk = false;
    for (int i = 0; i < n; i++) anyLk = anyLk || usageCache.lastKnown(b.idx[i]).any();
    if (wifiManager.state() == WifiState::ApForced || !anyLk) {
      setFrame(Screen::Ap);
    } else {
      int slot = _rotPos % (n + 1);
      if (slot == n) setFrame(Screen::Ap);
      else setProfileFrame(b, slot, n);
    }
  } else if (n == 0) {
    setFrame(wifiManager.staConnected() ? Screen::NoProfiles : Screen::WifiWait);
  } else {
    setProfileFrame(b, _rotPos % n, n);
  }
  render();

  static uint32_t lastStatsMs = 0;
  if (now - lastStatsMs >= STATS_LOG_MS) {
    lastStatsMs = now;
    Serial.printf("[disp] kep max %u us; heap szabad %u B (min %u B), legnagyobb blokk %u B\n", (unsigned)g_frameUsMax,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    g_frameUsMax = 0;
  }
}

#endif  // BOARD_CYD
