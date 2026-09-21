// Konfiguracio NVS-ben (Preferences). Titkot soha nem logol.
#pragma once
#include <Arduino.h>

#include <memory>
#include "config.h"

struct WifiProfile {
  bool used = false;
  bool enabled = true;
  int16_t priority = 0;
  char ssid[WIFI_SSID_MAX + 1] = "";
  char password[WIFI_PASS_MAX + 1] = "";  // TITOK
};

struct ClaudeProfile {
  bool used = false;
  bool enabled = true;
  char name[CLAUDE_NAME_MAX + 1] = "";
  uint8_t transport = CLAUDE_TRANSPORT_DEFAULT;
  char orgId[CLAUDE_ORG_MAX + 1] = "";  // WebSession: org-UUID; Gemini: Cloud project-ID; ChatGPT: account-ID (login adja)
  char auth[CLAUDE_AUTH_MAX + 1] = "";  // TITOK — WebSession: sessionKey; OAuth: access token
  // --- csak OAuth ---
  char refresh[CLAUDE_REFRESH_MAX + 1] = "";  // TITOK — refresh token (rotalodhat, ld. saveOAuthTokens)
  char scope[CLAUDE_SCOPE_MAX + 1] = "";   // a legutobb kapott scope; ures = OAUTH_SCOPES
  uint32_t expiresAt = 0;                   // access token lejarata (UTC epoch); 0 = ismeretlen
  // Csak RAM, minden felhasznaloi mentesnel uj: a token-frissites NEM valtoztatja, igy egy automatikus
  // frissites nem nullazza a cache-t/utemezest, es kesobbi mentes utan az elavult frissites nem ir felul.
  uint32_t editSeq = 0;
};

struct DeviceConfig {
  WifiProfile wifi[MAX_WIFI_PROFILES];
  ClaudeProfile claude[MAX_CLAUDE_PROFILES];
  uint8_t rotationSec = ROTATION_DEFAULT_S;
  uint16_t refreshSec = CLAUDE_REFRESH_DEFAULT_S;  // profilonkenti API-frissites (spec 14.)
  // Kijelzo-forgatas negyedfordulatban (0 = alap, 1 = 90, 2 = 180, 3 = 270 fok). 180: projektgazda, 2026-09-18 (ahogy
  // a dongle all); 90/270 csak a CYD-n (DISPLAY_QUARTER_TURNS), projektgazda, 2026-09-21. NVS "drot"; a regi "flip"
  // bool-t betolteskor 180 fokka forditjuk.
  uint8_t displayRot = 0;
  char apPassword[16] = "";  // egyszer generalt, NVS-ben marad
  char tz[TZ_POSIX_MAX + 1] = TZ_EUROPE_BUDAPEST;  // POSIX TZ (a kijelzett helyi idohoz)
};

// A kijelzonek/utemezonek eleg ennyi — nem kell a titkokat is masolni.
struct ClaudeBrief {
  uint8_t count = 0;
  uint8_t idx[MAX_CLAUDE_PROFILES];
  char name[MAX_CLAUDE_PROFILES][CLAUDE_NAME_MAX + 1];
  uint8_t rotationSec = ROTATION_DEFAULT_S;
  uint16_t refreshSec = CLAUDE_REFRESH_DEFAULT_S;
  uint8_t displayRot = 0;
};

class ConfigManager {
 public:
  void begin();  // betolt, szukseg eseten AP-jelszot general

  // Olvasas: masolat, mutex alatt (a fetch-task is olvas).
  DeviceConfig snapshot();
  // Heapen levo masolat. A DeviceConfig 20 Wi-Fi-profillal ~6,2 KB: stacken tobb helyen masolva a loopTask
  // tartaleka 1580 B-ra fogyott (vason mert, 2026-09-17) -> minden task-kodban ezt hasznaljuk.
  std::unique_ptr<DeviceConfig> heapSnapshot();
  // Masolat a hivo altal adott (pl. heapen levo) peldanyba — a ~6 KB-os DeviceConfig ne a stacken legyen.
  void copyTo(DeviceConfig &out);
  // Import: az osszes Wi-Fi- es Claude-slot + rotacio/frissites/TZ cserje (az AP-jelszo marad). A hivo validal.
  bool importAll(const DeviceConfig &in);
  uint32_t version() const { return _version; }  // minden mentes noveli

  bool saveWifi(int idx, const WifiProfile &p);
  bool deleteWifi(int idx);
  bool saveClaude(int idx, const ClaudeProfile &p);
  bool deleteClaude(int idx);
  bool saveRotation(uint8_t sec);
  bool saveDisplayRot(uint8_t rot);  // 0-3; a lapka altal nem tamogatottat (90/270 a dongle-on) elutasitja
  static bool displayRotAllowed(int rot);
  bool saveRefreshSec(uint16_t sec);
  bool saveTimezone(const char *posixTz);
  ClaudeBrief brief();  // engedelyezett profilok neve+indexe, titok nelkul

  // Automatikus token-frissites eredmenye. Csak akkor ir, ha a profilt kozben a felhasznalo nem
  // modositotta (editSeq egyezik). A (rotalt) refresh tokent ELOSZOR irja NVS-be: ha a szerver rotalt,
  // aramszunet eseten se maradjunk hasznalhatatlan (regi) refresh tokennel.
  bool saveOAuthTokens(int idx, uint32_t editSeq, const char *access, const char *refreshTok, uint32_t expiresAt,
                       const char *scope);
  // Uj szolgaltatok (Gemini/ChatGPT/Grok): az access token RAM-ban marad (token_cache), ide csak a refresh token,
  // az account-/project-ID (orgId; nullptr = marad) es a scope (nullptr/ures = marad) kerul.
  bool saveProviderLogin(int idx, uint32_t editSeq, const char *refreshTok, const char *orgId, const char *scope);

 private:
  void load();
  void writeWifi(int idx);
  void writeClaude(int idx);
  DeviceConfig _cfg;
  volatile uint32_t _version = 1;
  uint32_t _editCounter = 0;
  SemaphoreHandle_t _mtx = nullptr;
};

extern ConfigManager configManager;
