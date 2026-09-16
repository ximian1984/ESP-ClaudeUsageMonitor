// Konfiguracio NVS-ben (Preferences). Titkot soha nem logol.
#pragma once
#include <Arduino.h>
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
  char orgId[CLAUDE_ORG_MAX + 1] = "";  // csak WebSession-hoz kell
  char auth[CLAUDE_AUTH_MAX + 1] = "";  // TITOK — WebSession: sessionKey; OAuth: access token
  // --- csak OAuth ---
  char refresh[CLAUDE_AUTH_MAX + 1] = "";  // TITOK — refresh token (rotalodhat, ld. saveOAuthTokens)
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
  char apPassword[16] = "";  // egyszer generalt, NVS-ben marad
};

// A kijelzonek/utemezonek eleg ennyi — nem kell a titkokat is masolni.
struct ClaudeBrief {
  uint8_t count = 0;
  uint8_t idx[MAX_CLAUDE_PROFILES];
  char name[MAX_CLAUDE_PROFILES][CLAUDE_NAME_MAX + 1];
  uint8_t rotationSec = ROTATION_DEFAULT_S;
  uint16_t refreshSec = CLAUDE_REFRESH_DEFAULT_S;
};

class ConfigManager {
 public:
  void begin();  // betolt, szukseg eseten AP-jelszot general

  // Olvasas: masolat, mutex alatt (a fetch-task is olvas).
  DeviceConfig snapshot();
  uint32_t version() const { return _version; }  // minden mentes noveli

  bool saveWifi(int idx, const WifiProfile &p);
  bool deleteWifi(int idx);
  bool saveClaude(int idx, const ClaudeProfile &p);
  bool deleteClaude(int idx);
  bool saveRotation(uint8_t sec);
  bool saveRefreshSec(uint16_t sec);
  ClaudeBrief brief();  // engedelyezett profilok neve+indexe, titok nelkul

  // Automatikus token-frissites eredmenye. Csak akkor ir, ha a profilt kozben a felhasznalo nem
  // modositotta (editSeq egyezik). A (rotalt) refresh tokent ELOSZOR irja NVS-be: ha a szerver rotalt,
  // aramszunet eseten se maradjunk hasznalhatatlan (regi) refresh tokennel.
  bool saveOAuthTokens(int idx, uint32_t editSeq, const char *access, const char *refreshTok, uint32_t expiresAt,
                       const char *scope);

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
