// Forditasi ideju allandok. Titok NEM lehet itt (spec 6., 19.).
#pragma once

#define FW_VERSION "0.1.0"
#define BOARD_NAME "LILYGO T-Dongle-S3"

// --- Pinek: hivatalos LilyGO forras, ld. PLAN.md 1. ---
// A kijelzo SPI-pinjei a platformio.ini build_flags-ben vannak (TFT_eSPI).
#define PIN_LCD_BL 38     // factory_screen.ino:46
#define LCD_BL_ON  LOW    // ⚠ [vason merendo] lcd.ino:96 szerint aktiv alacsony, factory_screen.ino:147 ellentmond
#define PIN_BOOT_BTN 0    // docs Pins Map "Button 0"

// --- Korlatok ---
#define MAX_WIFI_PROFILES   5
#define MAX_CLAUDE_PROFILES 5
#define WIFI_SSID_MAX   32   // 802.11
#define WIFI_PASS_MAX   64   // WPA2-PSK
#define CLAUDE_NAME_MAX 12   // a 160 px szeles kijelzore
#define CLAUDE_ORG_MAX  40   // UUID 36 + tartalek
#define CLAUDE_AUTH_MAX 256  // ⚠ [feltarando] a valos session-/token-ertek hossza

// --- Kijelzo ---
#define ROTATION_MIN_S     1
#define ROTATION_MAX_S     60
#define ROTATION_DEFAULT_S 5

// --- Wi-Fi ---
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_RETRY_FROM_AP_MS    300000  // fallback AP-bol 5 percenkent ujraprobal, ha nincs AP-kliens
#define BOOT_WINDOW_MS           3000    // ennyi ideig figyeli a BOOT gombot inditas utan (ld. main.cpp)
#define BOOT_LONGPRESS_MS        5000    // futas kozben hosszan nyomva -> setup AP

// --- Claude transport ---
// ⚠ NYITOTT DONTES (projektgazda): melyik uton kerjuk az adatot. Profilonkent valaszthato, egyik sincs beegetve.
// A reszletek (host, path, fejlecek) egy helyen: claude_client.cpp kTransports[].
enum class ClaudeTransport : uint8_t {
  WebSession = 0,  // claude.ai web + sessionKey suti — 200-as valasz ezen az uton MEG NEM mert
  OAuth = 1,       // api.anthropic.com + OAuth Bearer — 200-as valasz mert (a koordinator, 2026-09-16)
};
#define CLAUDE_TRANSPORT_COUNT 2

// --- Claude refresh (spec 14.) ---
#define CLAUDE_REFRESH_PERIOD_S   60
#define CLAUDE_BACKOFF_MAX_S      900    // atmeneti hibanal legfeljebb 15 perc
#define CLAUDE_AUTH_BACKOFF_S     600    // 401/403: a szerver x-should-retry: false-t kuld (mert, PLAN.md 2.2)
#define CLAUDE_HTTP_TIMEOUT_MS    10000
#define CLAUDE_MAX_BODY_BYTES     16384

// --- Ido (spec 15.) ---
#define TZ_EUROPE_BUDAPEST "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
