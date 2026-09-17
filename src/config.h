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
#define MAX_WIFI_PROFILES   20  // projektgazda (2026-09-17): hordozhato eszkoz, sok helyszin. Kulcsok "w19ssid" <= 15 kar.
#define MAX_CLAUDE_PROFILES 5
#define WIFI_SSID_MAX   32   // 802.11
#define WIFI_PASS_MAX   64   // WPA2-PSK
#define CLAUDE_NAME_MAX 12   // a 160 px szeles kijelzore
#define CLAUDE_ORG_MAX  40   // UUID 36 + tartalek
#define CLAUDE_AUTH_MAX 300  // access/refresh token, sessionKey — fejlecbe kerul (⚠ [feltarando] pontos hossz)
#define CLAUDE_SCOPE_MAX 160 // OAuth scope-lista (az alap lista ~100 karakter)

// --- Kijelzo ---
#define ROTATION_MIN_S     1
#define ROTATION_MAX_S     60
#define ROTATION_DEFAULT_S 5

// --- Wi-Fi ---
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_SCAN_TIMEOUT_MS     20000   // sajat scan-korlat; a core 6 s-a keves (vason mert scan: 6,76 s)
#define WIFI_RETRY_FROM_AP_MS    300000  // fallback AP-bol 5 percenkent ujraprobal, ha nincs AP-kliens
#define BOOT_WINDOW_MS           3000    // ennyi ideig figyeli a BOOT gombot inditas utan (ld. main.cpp)
#define BOOT_LONGPRESS_MS        5000    // futas kozben hosszan nyomva -> setup AP

// --- Claude transport ---
// Dontes (projektgazda, 2026-09-16): ELSODLEGES az OAuth (on-device login + auto-refresh), a sessionKey masodlagos.
// Profilonkent valaszthato; a reszletek (host, path, fejlecek) egy helyen: claude_client.cpp kTransports[].
enum class ClaudeTransport : uint8_t {
  OAuth = 0,       // api.anthropic.com + OAuth Bearer — ELSODLEGES; 200-as valasz mert (a koordinator, 2026-09-16)
  WebSession = 1,  // claude.ai web + sessionKey suti — masodlagos; 200-as valasz ezen az uton MEG NEM mert
};
#define CLAUDE_TRANSPORT_COUNT 2
#define CLAUDE_TRANSPORT_DEFAULT ((uint8_t)ClaudeTransport::OAuth)

// --- OAuth (on-device login PKCE + auto-refresh) ---
// Forras: helyi Claude Code 2.1.273 binaris (sha256 953e9880...), PLAN.md 2.8. Vegpontok/CLIENT_ID/PKCE onnan.
// A refresh-logika mintaja: Data/erp-v1/.../Netatmo/NetatmoAccess.cs Refresh() (grant_type=refresh_token, mindharom
// mezot frissiti+menti) — az elvet vettuk at, nem a C# szintaxist (PLAN.md 2.9).
#define OAUTH_TOKEN_HOST     "platform.claude.com"
#define OAUTH_TOKEN_PATH     "/v1/oauth/token"
#define OAUTH_AUTHORIZE_URL  "https://claude.com/cai/oauth/authorize"
#define OAUTH_REDIRECT_URI   "https://platform.claude.com/oauth/code/callback"  // manualis; a callback kiirja a "code#state"-et
#define OAUTH_CLIENT_ID      "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
#define OAUTH_SCOPES         "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload user:plugins"
#define OAUTH_REFRESH_MARGIN_S   300     // 5 perccel lejarat elott (a Claude Code isOAuthTokenExpired-je, PLAN 2.8)
#define OAUTH_REFRESH_RETRY_S    120     // atmeneti frissitesi hiba utan
#define OAUTH_LOGIN_TTL_MS       600000  // fuggoben levo login (verifier/state) elettartama

// --- Claude refresh (spec 14.) ---
// A usage lassan valtozik -> konzervativ, KONFIGURALHATO alap (projektgazda: max uptime, kis labnyom).
#define CLAUDE_REFRESH_DEFAULT_S  180
#define REFRESH_PERIOD_MIN_S      60
#define REFRESH_PERIOD_MAX_S      3600
#define CLAUDE_BACKOFF_MAX_S      1800   // atmeneti hibanal legfeljebb 30 perc
#define CLAUDE_AUTH_BACKOFF_S     600    // 401/403: a szerver x-should-retry: false-t kuld (mert, PLAN.md 2.2)
#define CLAUDE_HTTP_TIMEOUT_MS    10000
#define CLAUDE_MAX_BODY_BYTES     16384

// --- Ido (spec 15.) ---
#define TZ_EUROPE_BUDAPEST "CET-1CEST,M3.5.0,M10.5.0/3"  // alapertelmezes; /usr/share/zoneinfo/Europe/Budapest utolso sora
// Titkositott mentes kulcsszarmaztatasa (backup_crypto). Offline-tores ellen: annyi kor, amennyi a dongle-on ~1-2 s.
// Vason mert (2026-09-17): 100000 kor = 9034 ms (~11 000 kor/s) -> 25000 kor ~2,3 s. A fajlba irodik, kesobb emelheto.
#define BACKUP_KDF_ITERATIONS 25000
#define TZ_POSIX_MAX 47   // POSIX TZ-string (setup-oldalon allithato, NVS "tz")
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
