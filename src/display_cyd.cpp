// ESP32-2432S028R (CYD) nativ 320x240-es (fekvo) megjelenites. A dongle 160x80-as utja: display_manager.cpp.
// Csak a cache-bol es az allapotokbol rajzol, API-t soha nem hiv (spec 13.).
//
// Memoria (tervdoksi 2.16): NINCS teljes kepernyos sprite (320x240x16 bit = 153,6 KB). Egyetlen 320x40-es
// sav-sprite (25,6 KB, annyi, mint a dongle framebuffere) jarja vegig a hat savot: savonkent a TELJES elrendezes
// ujrarajzolodik a sav eltolasaval, a sprite a kilogo reszt levagja (a karakterrajzolas vagott agon pixelenkent
// rajzol: TFT_eSPI Extensions/Sprite.cpp drawChar). Villogasmentes, es a webes tukor ugyanigy, savonkent kapja a kepet.
// Allo (240x320-as) elrendezes is van, a 90/270 fokos forgatashoz (projektgazda, 2026-09-21): a meret (W, H) futasidoben
// valt, a sav-sprite ujra letrejon 240x40-esre (19,2 KB), es 8 sav megy. A kepernyok a P (allo) szerint rendeznek.
// Vason merve (2026-09-21, fekvo): a szinek (az -inv env-vel), a forgatas es a hatterfeny. Az allo kep vason NEM mert.
#include "display_manager.h"

#if defined(BOARD_CYD)

#include <TFT_eSPI.h>

#include "config.h"
#include "config_manager.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "vendor/qrcodegen.h"
#include "wifi_manager.h"

DisplayManager displayManager;

static TFT_eSPI tft;
static TFT_eSprite band(&tft);
static bool bandOk = false;

static int W = 320, H = 240;             // allo kepnel 240x320 (applyRot)
static bool P = false;                   // allo (portrait) elrendezes
static const int BAND_H = 40;            // fekvo: 6 sav, 320x40x2 B = 25 600 B; allo: 8 sav, 240x40x2 B = 19 200 B
static int R = W - 6;                    // jobb margo (jobbra igazitott szovegek)
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
  int32_t apRetryMs = 0;
  bool apClients = false;
  time_t now = 0;
  uint32_t nowMs = 0;
  ProfileUsage u;
  LastKnownResets lk;
};

static Frame g_frame;
static Model g_m;

static void prepareApQr(const String &ssid, const String &pass);

static void prepare(const Frame &f) {
  g_m.f = f;
  g_m.sta = wifiManager.staConnected();
  g_m.apForced = wifiManager.state() == WifiState::ApForced;
  g_m.ip = wifiManager.ipString();
  g_m.apSsid = wifiManager.apSsid();
  g_m.apPass = wifiManager.apPassword();
  g_m.staSsid = wifiManager.staSsid();
  g_m.stateName = wifiManager.stateName();
  if (f.screen == Screen::Ap) {
    g_m.apRetryMs = wifiManager.apRetryInMs();
    g_m.apClients = wifiManager.apHasClients();
    prepareApQr(g_m.apSsid, g_m.apPass);
  }
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
// A kozepre igazitott egyszeru kepernyok a fekvo 240 px-es magassagra keszultek; allva (320) ennyivel lejjebb kerulnek.
static int dy() { return (H - 240) / 2; }

// Kozepre igazitott sor: 4-es font, ha kifer, kulonben 2-es (allva a 240 px-es szelesseg a hosszabb sorokat levagna).
static void textC(const String &s, int y, uint16_t color) { textFit(s, W / 2, y, color, W - 12, TC_DATUM); }

static void drawBoot(const Model &m) {
  const int d = dy();
  textC("CLAUDE MONITOR", 44 + d, TFT_ORANGE);
  text("v" FW_VERSION "  -  " BOARD_NAME, W / 2, 78 + d, TFT_DARKGREY, 2, TC_DATUM);
  textC("Press BOOT now", 124 + d, TFT_WHITE);
  textC("for setup mode (" + String((m.f.bootMsLeft + 999) / 1000) + ")", 158 + d, TFT_WHITE);
}

// Setup-AP QR-kod a telefonnak, a szokasos "WIFI:T:WPA;S:<ssid>;P:<jelszo>;;" alakban (ZXing "Wi-Fi Network config");
// a \ ; , : " elojelet kap. 132 px-es dobozba kerul; ha ott 3 px/modulnal kisebb lenne (5-os verzio felett), nincs QR,
// csak a szoveg. Az AP SSID-je es jelszava ASCII (wifi_manager.cpp:33, config_manager.cpp:26-33), ECI nem kell.
static const int QR_BOX = 132, QR_QUIET = 2, QR_MIN_PX = 3, QR_MAX_VER = 5;
static uint8_t g_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
static bool g_qrOk = false;
static String g_qrKey;  // a kod csak SSID/jelszo-valtaskor keszul ujra, nem kepenkent

static void appendEscaped(String &out, const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
    out += c;
  }
}

static void prepareApQr(const String &ssid, const String &pass) {
  String key = ssid + "\n" + pass;
  if (key == g_qrKey) return;
  g_qrKey = key;
  String payload = "WIFI:T:WPA;S:";
  appendEscaped(payload, ssid);
  payload += ";P:";
  appendEscaped(payload, pass);
  payload += ";;";
  uint8_t tmp[sizeof(g_qr)];
  g_qrOk = qrcodegen_encodeText(payload.c_str(), tmp, g_qr, qrcodegen_Ecc_LOW, 1, QR_MAX_VER, qrcodegen_Mask_AUTO,
                                true) &&
           QR_BOX / (qrcodegen_getSize(g_qr) + 2 * QR_QUIET) >= QR_MIN_PX;
}

// Feher alap (csendes zona) + fekete modulok, soronkent osszevont futamokkal; (x, y) a doboz bal felso sarka.
static void drawQr(int x, int y) {
  int n = qrcodegen_getSize(g_qr);
  int px = QR_BOX / (n + 2 * QR_QUIET);
  int side = (n + 2 * QR_QUIET) * px;
  x += (QR_BOX - side) / 2;
  y += (QR_BOX - side) / 2;
  fillRectS(x, y, side, side, TFT_WHITE);
  int ox = x + QR_QUIET * px, oy = y + QR_QUIET * px;
  for (int r = 0; r < n; r++) {
    if (outside(oy + r * px, px)) continue;
    for (int c = 0; c < n;) {
      if (!qrcodegen_getModule(g_qr, c, r)) { c++; continue; }
      int c0 = c;
      while (c < n && qrcodegen_getModule(g_qr, c, r)) c++;
      fillRectS(ox + c0 * px, oy + r * px, (c - c0) * px, px, TFT_BLACK);
    }
  }
}

// Az ujraprobalas allapota (wifi_manager.cpp: 5 percenkent, de csak ha senki nincs az AP-n).
static String apRetryLine(const Model &m) {
  if (m.apRetryMs < 0) return "searching Wi-Fi...";
  if (m.apRetryMs == 0) return m.apClients ? "retry waits: phone on AP" : "retrying Wi-Fi...";
  uint32_t s = ((uint32_t)m.apRetryMs + 999) / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "retrying Wi-Fi in %u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return buf;
}

// Allo setup-kepernyo: a QR felul kozepen, alatta harom sor "cimke  ertek" (jobbra igazitva), legalul az ujraprobalas.
static void drawApPortrait(const Model &m) {
  if (g_qrOk) drawQr((W - QR_BOX) / 2, 46);
  const char *labels[3] = {"Wi-Fi", "Pass", "Open"};
  const String values[3] = {m.apSsid, m.apPass, "http://" + m.ip};
  const uint16_t colors[3] = {TFT_WHITE, TFT_YELLOW, TFT_CYAN};
  for (int i = 0; i < 3; i++) {
    int y = 186 + i * 32;
    int lw = band.textWidth(labels[i], 2);
    text(labels[i], 6, y + 5, TFT_LIGHTGREY, 2);
    textFit(values[i], R, y, colors[i], R - 6 - lw - 8, TR_DATUM);
  }
  if (!m.apForced) text(apRetryLine(m), W / 2, H - 22, TFT_DARKGREY, 2, TC_DATUM);
}

static void drawAp(const Model &m) {
  textC(m.apForced ? "SETUP MODE" : "NO WIFI - SETUP", 8, m.apForced ? TFT_ORANGE : TFT_RED);
  fillRectS(6, 40, W - 12, 1, TFT_DARKGREY);
  if (P) return drawApPortrait(m);
  // A QR jobbra kerul; a szoveg (SSID, jelszo, cim) mindig marad (projektgazda), QR mellett keskenyebb oszlopban.
  const int qrX = R - QR_BOX;
  const int maxW = g_qrOk ? qrX - 6 - 6 : W - 12;
  if (g_qrOk) drawQr(qrX, 48);
  text("Wi-Fi name", 6, 50, TFT_LIGHTGREY, 2);
  textFit(m.apSsid, 6, 68, TFT_WHITE, maxW);
  text("Password", 6, 102, TFT_LIGHTGREY, 2);
  textFit(m.apPass, 6, 120, TFT_YELLOW, maxW);  // AP-jelszo: a spec 9. szerint kijelzendo
  text("Then open", 6, 154, TFT_LIGHTGREY, 2);
  textFit("http://" + m.ip, 6, 172, TFT_CYAN, maxW);
  if (!m.apForced) text(apRetryLine(m), W / 2, 218, TFT_DARKGREY, 2, TC_DATUM);
}

static void drawWifiWait(const Model &m) {
  const int d = dy();
  textC("WIFI", 60 + d, TFT_ORANGE);
  textC(m.stateName, 104 + d, TFT_WHITE);
  if (m.staSsid.length()) text(m.staSsid, W / 2, 148 + d, TFT_CYAN, 2, TC_DATUM);
}

static void drawNoProfiles(const Model &m) {
  const int d = dy();
  textC("NO CLAUDE PROFILE", 60 + d, TFT_ORANGE);
  text("Add one on the setup page:", W / 2, 112 + d, TFT_LIGHTGREY, 2, TC_DATUM);
  textC("http://" + m.ip, 136 + d, TFT_CYAN);
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
  if (m.sta) text("http://" + m.ip, 6, H - 17, TFT_DARKGREY, 2);
  if (m.f.count > 1) text(String(m.f.slot + 1) + "/" + String(m.f.count), R, H - 17, TFT_DARKGREY, 2, TR_DATUM);
}

// A reset sora (y .. y+26): a visszaszamlalas nagy (4-es), a datum kicsi, jobbra: "RST  02:17:32     @09.20 00:02".
// Allva (240 px) a datum nem fer melle: a kovetkezo sorba kerul, jobbra (y+28 .. y+44), ld. RESET_H.
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
  text(when, R, P ? y + 28 : y + 7, TFT_LIGHTGREY, 2, TR_DATUM);
}

static int resetH() { return P ? 44 : 26; }

// Egy limit blokkja (y .. y+92; allva y+110, mert a reset datuma kulon sorba kerul):
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
    // A csik a cimke folyamatos skalajat kapja (projektgazda, 2026-09-21); regi/lejart adatnal szurke, mint a cimke.
    if (fill > 0) fillRectS(7, y + 53, fill, 10, labelColor);
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
  const int step = 30 + resetH() + 8;  // cimke + reset sor + koz: fekvo 64, allo 82
  lkBlock(m, "SESSION", m.lk.sessionReset, 60);
  lkBlock(m, "WEEKLY", m.lk.weeklyReset, 60 + step);
  String saved = m.lk.savedEpoch >= 1704067200 ? TimeManager::localDateTime(m.lk.savedEpoch).substring(5) : String("?");
  text("data from " + saved, 6, 60 + 2 * step + 8, TFT_DARKGREY, 2);
  drawFooter(m);
}

static void drawProfile(const Model &m) {
  const ProfileUsage &u = m.u;

  // Beszedes ujra-belepes kepernyo (OAuth refresh token elhalt) — a regi adat felett is (spec 17.).
  if (u.lastError == FetchError::ReloginRequired) {
    drawHeader(m, "", TFT_RED, "");
    text("!", R, 2, TFT_RED, 4, TR_DATUM);
    textC("RE-LOGIN NEEDED", 76 + dy(), TFT_RED);
    text("open the setup page:", W / 2, 118 + dy(), TFT_LIGHTGREY, 2, TC_DATUM);
    textC("http://" + m.ip, 140 + dy(), TFT_CYAN);
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
      textC(m.sta ? (m.synced ? "loading..." : "waiting NTP") : "waiting WiFi", 104 + dy(), TFT_LIGHTGREY);
    } else {
      textC(fetchErrorTitle(u.lastError), 88 + dy(), TFT_RED);
      textC(u.lastHttpStatus > 0 ? "ERROR " + String(u.lastHttpStatus) : String("ERROR"), 124 + dy(), TFT_RED);
    }
    drawFooter(m);
    return;
  }

  drawHeader(m, status, sc, age);
  // Layout (y): fekvo: 0 fejlec | 32 SESSION (..124) | 127 WEEKLY (..219) | 223 lablec;
  //             allo:  0 fejlec | 36 SESSION (..146) | 160 WEEKLY (..270) | 303 lablec.
  // Egyetlen limitet ado szolgaltatonal (pl. Grok CREDITS) a blokk kozepre kerul, ures "WEEKLY n/a" nelkul.
  const UsageLimit *weekly = u.data.find(LimitKind::Weekly);
  if (weekly || u.data.count != 1) {
    drawLimit(m, u.data.find(LimitKind::Session), "SESSION", P ? 36 : 32, stale);
    drawLimit(m, weekly, "WEEKLY", P ? 160 : 127, stale);
  } else {
    const UsageLimit *only = u.data.find(LimitKind::Session);
    drawLimit(m, only ? only : &u.data.limits[0], "SESSION", P ? 104 : 80, stale);
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

// TFT_eSPI setRotation: 1 = fekvo, 3 = ugyanaz 180 fokkal, 0 es 2 = allo (TFT_Drivers/ILI9341_Rotation.h,
// ST7789_Rotation.h). A 90 fok (displayRot 1) a 2-es, a 270 a 0-s: hogy ez valoban az oramutato jarasa szerinti
// negyedfordulat-e, az ⚠ [vason merendo] — a setup-oldal mindkettot kinalja. Allo/fekvo valtasnal a sav-sprite ujra
// letrejon az uj szelesseggel (a loop() es a webes tukor ugyanabban a taskban fut, ld. streamScreen).
static const uint8_t TFT_ROTATION[4] = {1, 2, 3, 0};

void DisplayManager::applyRot(uint8_t rot) {
  if (_rot == (int)rot) return;
  _rot = rot;
  tft.setRotation(TFT_ROTATION[rot & 3]);
  bool portrait = rot & 1;
  int w = portrait ? 240 : 320;
  if (w != W || !bandOk) {
    W = w;
    H = portrait ? 320 : 240;
    R = W - 6;
    P = portrait;
    if (bandOk) band.deleteSprite();
    bandOk = band.createSprite(W, BAND_H) != nullptr;
  }
  tft.fillScreen(TFT_BLACK);
  Serial.printf("[disp] kijelzo-forgatas: %d fok (%s)\n", rot * 90, portrait ? "allo" : "fekvo");
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
  applyRot(configManager.brief().displayRot);  // a sav-sprite-ot is letrehozza
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
  applyRot(b.displayRot);  // a setup-oldalon barmikor atallithato
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
