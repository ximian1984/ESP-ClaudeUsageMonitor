// Forditasi ideju allandok. Titok NEM lehet itt (spec 6., 19.).
#pragma once

#define FW_VERSION "0.1.0"

// --- Lapka-valasztas: a platformio.ini env-je adja (-DBOARD_CYD). A kijelzo SPI-pinjei ott vannak (TFT_eSPI). ---
#if defined(BOARD_CYD)
// ESP32-2432S028R ("Cheap Yellow Display"), klasszikus ESP32. Forras: witnessmenow/ESP32-Cheap-Yellow-Display
// @ 0564a14 PINS.md es DisplayConfig/User_Setup.h; tervdoksi 2.15. ⚠ Fizikai lapon NEM mert.
#define BOARD_NAME "ESP32-2432S028R (CYD)"
#define PIN_LCD_BL 21     // User_Setup.h:131 TFT_BL; PINS.md "IO21 TFT_BL"
#define LCD_BL_ON  HIGH   // User_Setup.h:132 TFT_BACKLIGHT_ON HIGH
#define PIN_BOOT_BTN 0    // PINS.md "IO0 BOOT"
// RGB-LED, aktiv alacsony (PINS.md "LEDs are active low"): indulaskor kikapcsoljuk, hogy ne vilagitson lebegve.
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define DISPLAY_QUARTER_TURNS 1  // 90 fokos forgatas (allo kep) is: a 320x240-es panelnek van allo elrendezese
#else
#define BOARD_NAME "LILYGO T-Dongle-S3"
// --- Pinek: hivatalos LilyGO forras, ld. tervdoksi 1. ---
#define PIN_LCD_BL 38     // factory_screen.ino:46
#define LCD_BL_ON  LOW    // ⚠ [vason merendo] lcd.ino:96 szerint aktiv alacsony, factory_screen.ino:147 ellentmond
#define PIN_BOOT_BTN 0    // docs Pins Map "Button 0"
#define DISPLAY_QUARTER_TURNS 0  // csak 0/180 fok: 160x80 allva (80 px szeles) nem ad ertelmes kepet
#endif

// --- Korlatok ---
#define MAX_WIFI_PROFILES   20  // projektgazda (2026-09-17): hordozhato eszkoz, sok helyszin. Kulcsok "w19ssid" <= 15 kar.
#define MAX_CLAUDE_PROFILES 5
#define WIFI_SSID_MAX   32   // 802.11
#define WIFI_PASS_MAX   64   // WPA2-PSK
#define CLAUDE_NAME_MAX 12   // a 160 px szeles kijelzore
#define CLAUDE_ORG_MAX  40   // UUID 36 + tartalek
#define CLAUDE_AUTH_MAX 300  // Claude access token (mert: 108 kar.) / sessionKey — NVS-ben
#define CLAUDE_REFRESH_MAX 512  // refresh token (Claude mert: 108; Google/OpenAI/xAI ⚠ [vason merendo]) — NVS-ben
// Az uj szolgaltatok access tokenje CSAK RAM-ban (token_cache): a Google-e akar 2048 bajt, az OpenAI-e JWT;
// 5 profil x 2 KB nem ferne a 0x5000-es NVS-be (tervdoksi 2.13/b). Inditas utan egy refresh potolja.
#define PROVIDER_ACCESS_MAX 2600
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
// 2026-09-17: tovabbi szolgaltatok (projektgazda) — forras es meres: tervdoksi 2.13/b. Az NVS-ben a szam tarolodik,
// ezert a meglevo ertekek (0, 1) nem valtozhatnak.
enum class ClaudeTransport : uint8_t {
  OAuth = 0,       // Claude: api.anthropic.com + OAuth Bearer — 200-as valasz mert (2026-09-16), vason fut (2026-09-17)
  WebSession = 1,  // Claude web + sessionKey suti — masodlagos; 200-as valasz ezen az uton MEG NEM mert
  Gemini = 2,      // Google Code Assist kvota (Gemini CLI login) — ⚠ [vason merendo]
  ChatGpt = 3,     // ChatGPT / Codex keret (Codex CLI eszkozkod-login) — ⚠ [vason merendo]
  Grok = 4,        // Grok CLI credit (xAI eszkozkod-login) — ⚠ [vason merendo]
};
#define CLAUDE_TRANSPORT_COUNT 5
#define CLAUDE_TRANSPORT_DEFAULT ((uint8_t)ClaudeTransport::OAuth)

// --- OAuth (on-device login PKCE + auto-refresh) ---
// Forras: helyi Claude Code 2.1.273 binaris (sha256 953e9880...), tervdoksi 2.8. Vegpontok/CLIENT_ID/PKCE onnan.
// A refresh-logika mintaja: Data/erp-v1/.../Netatmo/NetatmoAccess.cs Refresh() (grant_type=refresh_token, mindharom
// mezot frissiti+menti) — az elvet vettuk at, nem a C# szintaxist (tervdoksi 2.9).
#define OAUTH_TOKEN_HOST     "platform.claude.com"
#define OAUTH_TOKEN_PATH     "/v1/oauth/token"
#define OAUTH_AUTHORIZE_URL  "https://claude.com/cai/oauth/authorize"
#define OAUTH_REDIRECT_URI   "https://platform.claude.com/oauth/code/callback"  // manualis; a callback kiirja a "code#state"-et
#define OAUTH_CLIENT_ID      "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
#define OAUTH_SCOPES         "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload user:plugins"
#define OAUTH_REFRESH_MARGIN_S   300     // 5 perccel lejarat elott (a Claude Code isOAuthTokenExpired-je, tervdoksi 2.8)
#define OAUTH_REFRESH_RETRY_S    120     // atmeneti frissitesi hiba utan
#define OAUTH_LOGIN_TTL_MS       900000  // fuggoben levo login (verifier/state vagy eszkozkod) elettartama (az OpenAI kodja 15 perc)
#define PROVIDER_ACCESS_DEFAULT_TTL_S 3600  // ha a token-valasz nem ad expires_in-t

// --- Gemini (Google Code Assist; forras: google-gemini/gemini-cli @ 6a466a7e) ---
// ⚠ DOKUMENTALT KIVETEL a "titok soha nem kerul commitba" szabaly alol (mérve 2026-09-18):
// A lenti GEMINI_CLIENT_SECRET a NYILVANOS gemini-cli forrasbol valo, szo szerint:
//   https://raw.githubusercontent.com/google-gemini/gemini-cli/main/packages/core/src/code_assist/oauth2.ts
//   (`OAUTH_CLIENT_SECRET`, commit 6a466a7e; a mi ertekunkkel bitre egyezik — ellenorizve).
// Az upstream megjegyzese ott (oauth2.ts:79-85): "It's ok to save this in git because this is an installed
// application ... the client secret is obviously not treated as a secret" (Google installed-app szabalya,
// https://developers.google.com/identity/protocols/oauth2#installed).
// Tehat NEM felhasznaloi titok es NEM a mienk: onmagaban nem ad hozzaferest semmihez (a felhasznalo hozzajarulasa
// es a PKCE-verifier kell mellé). Nem kell forgatni; ha a Google mégis cserelne, innen frissitendo.
// A VALODI titkok (access/refresh token, Wi-Fi- es admin-jelszo) tovabbra is CSAK az eszkoz NVS-eben vannak.
#define GEMINI_CLIENT_ID     "681255809395-oo8ft2oprdrnp9e3aqf6av3hmdib135j.apps.googleusercontent.com"  // oauth2.ts:76
#define GEMINI_CLIENT_SECRET "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"                                          // oauth2.ts:85
#define GEMINI_AUTHORIZE_URL "https://accounts.google.com/o/oauth2/v2/auth"  // mert: 302 a Google-belepesre
#define GEMINI_TOKEN_URL     "https://oauth2.googleapis.com/token"
#define GEMINI_REDIRECT_URI  "https://codeassist.google.com/authcode"        // oauth2.ts:443 (kezi kod)
#define GEMINI_SCOPES        "https://www.googleapis.com/auth/cloud-platform https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/userinfo.profile"  // oauth2.ts:88-92
#define GEMINI_API_BASE      "https://cloudcode-pa.googleapis.com/v1internal"  // server.ts:73-74, 532

// --- ChatGPT / Codex (forras: openai/codex @ 7abf2a3b) ---
#define CHATGPT_CLIENT_ID    "app_EMoamEEZ73f0CkXaXp7hrann"                  // login/src/auth/manager.rs:1732
#define CHATGPT_AUTH_BASE    "https://auth.openai.com"                       // login/src/server.rs:59
#define CHATGPT_USAGE_URL    "https://chatgpt.com/backend-api/wham/usage"    // rate_limit_resets.rs:124-129

// --- Grok (forras: xai-org/grok-build @ 48271133; billing: steipete/CodexBar GrokCreditsProxyFetcher.swift) ---
#define GROK_CLIENT_ID       "b1a00492-073a-47ea-816f-4c329264a828"          // xai-grok-login/src/config.rs:250
#define GROK_AUTH_BASE       "https://auth.x.ai"                             // config.rs:122; discovery mert
#define GROK_SCOPES          "openid profile email offline_access grok-cli:access api:access"  // config.rs:4-26 (a minimalisabb halmaz ⚠)
#define GROK_BILLING_URL     "https://cli-chat-proxy.grok.com/v1/billing?format=credits"

// --- Claude refresh (spec 14.) ---
// A usage lassan valtozik -> konzervativ, KONFIGURALHATO alap (projektgazda: max uptime, kis labnyom).
#define CLAUDE_REFRESH_DEFAULT_S  180
#define REFRESH_PERIOD_MIN_S      60
#define REFRESH_PERIOD_MAX_S      3600
#define CLAUDE_BACKOFF_MAX_S      1800   // atmeneti hibanal legfeljebb 30 perc
#define CLAUDE_AUTH_BACKOFF_S     600    // 401/403: a szerver x-should-retry: false-t kuld (mert, tervdoksi 2.2)
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
