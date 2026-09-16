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
  uint8_t transport = (uint8_t)ClaudeTransport::WebSession;  // regi NVS-bejegyzesnel is ez az alap
  char orgId[CLAUDE_ORG_MAX + 1] = "";  // csak WebSession-hoz kell
  char auth[CLAUDE_AUTH_MAX + 1] = "";  // TITOK
};

struct DeviceConfig {
  WifiProfile wifi[MAX_WIFI_PROFILES];
  ClaudeProfile claude[MAX_CLAUDE_PROFILES];
  uint8_t rotationSec = ROTATION_DEFAULT_S;
  char apPassword[16] = "";  // egyszer generalt, NVS-ben marad
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

 private:
  void load();
  void writeWifi(int idx);
  void writeClaude(int idx);
  DeviceConfig _cfg;
  volatile uint32_t _version = 1;
  SemaphoreHandle_t _mtx = nullptr;
};

extern ConfigManager configManager;
