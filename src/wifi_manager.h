// Nem blokkolo Wi-Fi allapotgep: scan -> ismert halozatok prioritas/RSSI szerint -> sorban probal -> AP fallback.
#pragma once
#include <Arduino.h>
#include <vector>

enum class WifiState : uint8_t {
  Idle,        // meg nem indult
  Scanning,    // automatikus valasztashoz scannel
  Connecting,  // egy jeloltre csatlakozik
  Connected,   // STA kapcsolat el
  ApFallback,  // egyik profil sem ment -> sajat AP (idonkent ujraprobal)
  ApForced,    // BOOT gomb -> sajat AP, nem lep ki magatol
};

struct ScanEntry {
  String ssid;
  int32_t rssi;
  bool secure;
};

class WifiManager {
 public:
  void begin(bool forceSetup);
  void loop();
  void enterForcedSetup();       // futas kozbeni hosszu BOOT-nyomas
  void requestReselect();        // konfig valtozott -> ujravalasztas (AP-forced modban nem)

  // Web-scan (a setup oldalrol). A scan aszinkron; a lista az utolso kesz scan eredmenye.
  void requestScan();
  bool scanRunning() const;  // kerve vagy folyamatban
  std::vector<ScanEntry> scanResults();

  WifiState state() const { return _state; }
  bool staConnected() const { return _state == WifiState::Connected; }
  bool apActive() const { return _state == WifiState::ApFallback || _state == WifiState::ApForced; }
  String staSsid() const { return _connectedSsid; }
  String apSsid() const { return _apSsid; }
  String apPassword() const { return _apPassword; }
  String ipString() const;
  int32_t rssi() const;
  const char *stateName() const;

 private:
  enum class ScanPurpose : uint8_t { None, Select, Web };
  struct Candidate {
    int profileIdx;
    int16_t priority;
    int32_t rssi;
  };

  void startSelect();
  void onScanDone(int n);
  void tryNextCandidate();
  void startAp(bool forced);
  void stopAp();

  WifiState _state = WifiState::Idle;
  ScanPurpose _scanPurpose = ScanPurpose::None;
  std::vector<Candidate> _candidates;
  size_t _candIdx = 0;
  uint32_t _stateSinceMs = 0;
  uint32_t _lostSinceMs = 0;
  String _connectedSsid;
  String _apSsid, _apPassword;
  std::vector<ScanEntry> _scan;
  bool _reselectRequested = false;
  SemaphoreHandle_t _scanMtx = nullptr;
};

extern WifiManager wifiManager;
