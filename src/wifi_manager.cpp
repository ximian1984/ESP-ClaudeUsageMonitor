#include "wifi_manager.h"

#include <WiFi.h>
#include <algorithm>

#include "config.h"
#include "config_manager.h"

WifiManager wifiManager;

static bool webScanPending = false;
static const size_t MAX_SCAN_ENTRIES = 20;

void WifiManager::begin(bool forceSetup) {
  _scanMtx = xSemaphoreCreateMutex();
  WiFi.persistent(false);         // az SDK ne irja a sajat NVS-ebe a jelszot — egy helyen legyen a titok
  WiFi.setAutoReconnect(false);   // az ujracsatlakozast ez az allapotgep vezerli

  // AP-azonosito a MAC also ket bajtjabol (spec 9.). A jelszo NEM a MAC-bol jon: a broadcastolt
  // SSID-bol kitalalhato jelszo nem titok. Egyszer generalt, NVS-ben (config_manager), kijelzon lathato.
  uint64_t mac = ESP.getEfuseMac();  // bajtsorrend: mac[0] = legalso bajt
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  _apSsid = String("ClaudeMonitor-") + suffix;
  _apPassword = configManager.snapshot().apPassword;

  if (forceSetup) {
    startAp(true);
  } else {
    startSelect();
  }
}

void WifiManager::enterForcedSetup() {
  if (_state == WifiState::ApForced) return;
  WiFi.disconnect();
  _connectedSsid = "";
  startAp(true);
}

void WifiManager::requestReselect() { _reselectRequested = true; }

void WifiManager::requestScan() { webScanPending = true; }

bool WifiManager::scanRunning() const { return webScanPending || _scanPurpose != ScanPurpose::None; }

std::vector<ScanEntry> WifiManager::scanResults() {
  xSemaphoreTake(_scanMtx, portMAX_DELAY);
  std::vector<ScanEntry> copy = _scan;
  xSemaphoreGive(_scanMtx);
  return copy;
}

void WifiManager::startSelect() {
  WiFi.mode(apActive() ? WIFI_AP_STA : WIFI_STA);
  if (_state != WifiState::ApFallback) _state = WifiState::Scanning;
  _stateSinceMs = millis();
  _candidates.clear();
  _candIdx = 0;
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    _scanPurpose = ScanPurpose::None;
    Serial.println("[wifi] scan inditas sikertelen");
    if (!apActive()) startAp(false);
    return;
  }
  _scanPurpose = ScanPurpose::Select;
}

void WifiManager::onScanDone(int n) {
  // Web-listahoz: SSID-enkent a legerosebb, legfeljebb MAX_SCAN_ENTRIES.
  std::vector<ScanEntry> entries;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    int32_t rssi = WiFi.RSSI(i);
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const ScanEntry &e) { return e.ssid == ssid; });
    if (it == entries.end()) {
      entries.push_back({ssid, rssi, secure});
    } else if (rssi > it->rssi) {
      it->rssi = rssi;
    }
  }
  std::sort(entries.begin(), entries.end(), [](const ScanEntry &a, const ScanEntry &b) { return a.rssi > b.rssi; });
  if (entries.size() > MAX_SCAN_ENTRIES) entries.resize(MAX_SCAN_ENTRIES);

  xSemaphoreTake(_scanMtx, portMAX_DELAY);
  _scan = entries;
  xSemaphoreGive(_scanMtx);

  ScanPurpose purpose = _scanPurpose;
  _scanPurpose = ScanPurpose::None;
  WiFi.scanDelete();
  if (purpose != ScanPurpose::Select) return;

  // Spec 8.: engedelyezett profilok, amelyek a scanben latszanak; prioritas csokkeno, azon belul RSSI.
  DeviceConfig cfg = configManager.snapshot();
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    const WifiProfile &w = cfg.wifi[i];
    if (!w.used || !w.enabled || w.ssid[0] == '\0') continue;
    for (const ScanEntry &e : entries) {
      if (e.ssid == w.ssid) {
        _candidates.push_back({i, w.priority, e.rssi});
        break;
      }
    }
  }
  std::sort(_candidates.begin(), _candidates.end(), [](const Candidate &a, const Candidate &b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    return a.rssi > b.rssi;
  });
  Serial.printf("[wifi] scan: %d halozat, %u ismert jelolt\n", n, (unsigned)_candidates.size());
  _candIdx = 0;
  tryNextCandidate();
}

void WifiManager::tryNextCandidate() {
  if (_candIdx >= _candidates.size()) {
    Serial.println("[wifi] nincs mukodo profil -> AP fallback");
    WiFi.disconnect();
    if (_state != WifiState::ApFallback) startAp(false);
    else _stateSinceMs = millis();  // mar AP-ban: a kovetkezo ujraprobalasig var
    return;
  }
  DeviceConfig cfg = configManager.snapshot();
  const WifiProfile &w = cfg.wifi[_candidates[_candIdx].profileIdx];
  // Csak az SSID es a profil-index kerul logba, a jelszo soha.
  Serial.printf("[wifi] csatlakozas: profil %d, SSID '%s'\n", _candidates[_candIdx].profileIdx, w.ssid);
  WiFi.disconnect();
  WiFi.begin(w.ssid, w.password[0] ? w.password : nullptr);
  _connectedSsid = w.ssid;
  if (_state != WifiState::ApFallback) _state = WifiState::Connecting;
  _stateSinceMs = millis();
}

void WifiManager::startAp(bool forced) {
  WiFi.mode(WIFI_AP_STA);  // AP mellett STA is kell, kulonben a web-scan nem megy
  // WPA2-PSK kotelezo (spec 9.): a softAP jelszoval WPA2-t inditja; 8 karakter alatt megtagadna.
  bool ok = WiFi.softAP(_apSsid.c_str(), _apPassword.c_str());
  Serial.printf("[wifi] AP %s: %s, SSID %s, IP %s\n", forced ? "(forced)" : "(fallback)", ok ? "fut" : "HIBA",
                _apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  _state = forced ? WifiState::ApForced : WifiState::ApFallback;
  _stateSinceMs = millis();
}

void WifiManager::stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void WifiManager::loop() {
  // Scan kesz?
  if (_scanPurpose != ScanPurpose::None) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      onScanDone(n);
    } else if (n == WIFI_SCAN_FAILED) {
      _scanPurpose = ScanPurpose::None;
      if (_state == WifiState::Scanning) startAp(false);
    }
    return;
  }

  // Web-scan csak stabil allapotban (csatlakozas kozben megzavarna).
  if (webScanPending && (_state == WifiState::Connected || apActive())) {
    webScanPending = false;
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true) != WIFI_SCAN_FAILED) _scanPurpose = ScanPurpose::Web;
    return;
  }

  uint32_t now = millis();
  switch (_state) {
    case WifiState::Idle:
    case WifiState::Scanning:
      break;

    case WifiState::Connecting:
    case WifiState::ApFallback:
      if (!_candidates.empty() && _candIdx < _candidates.size()) {
        // folyamatban levo csatlakozasi kiserlet (AP-fallbackbol is)
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("[wifi] csatlakozva: '%s', IP %s\n", _connectedSsid.c_str(), WiFi.localIP().toString().c_str());
          if (apActive()) stopAp();
          _state = WifiState::Connected;
          _stateSinceMs = now;
          _lostSinceMs = 0;
          _candidates.clear();
        } else if (now - _stateSinceMs > WIFI_CONNECT_TIMEOUT_MS) {
          Serial.println("[wifi] idotullepes, kovetkezo jelolt");
          _candIdx++;
          tryNextCandidate();
        }
        break;
      }
      if (_state == WifiState::ApFallback) {
        _candidates.clear();
        bool retryDue = now - _stateSinceMs > WIFI_RETRY_FROM_AP_MS;
        // Kifejezett keres (profil mentve a setup oldalon) AP-kliens mellett is probal; az idozitett
        // ujraprobalas csak akkor, ha senki nincs az AP-n (ne dobja le a beallito telefont).
        if (_reselectRequested || (retryDue && WiFi.softAPgetStationNum() == 0)) {
          _reselectRequested = false;
          startSelect();
        }
      }
      break;

    case WifiState::Connected:
      if (_reselectRequested) {
        _reselectRequested = false;
        startSelect();
        break;
      }
      if (WiFi.status() != WL_CONNECTED) {
        if (_lostSinceMs == 0) _lostSinceMs = now;
        if (now - _lostSinceMs > 5000) {
          Serial.println("[wifi] kapcsolat elveszett -> ujravalasztas");
          _connectedSsid = "";
          _state = WifiState::Scanning;
          startSelect();
        }
      } else {
        _lostSinceMs = 0;
      }
      break;

    case WifiState::ApForced:
      break;  // kilepes csak ujrainditassal (web: Restart)
  }
}

String WifiManager::ipString() const {
  if (_state == WifiState::Connected) return WiFi.localIP().toString();
  if (apActive()) return WiFi.softAPIP().toString();
  return "";
}

int32_t WifiManager::rssi() const { return _state == WifiState::Connected ? WiFi.RSSI() : 0; }

const char *WifiManager::stateName() const {
  switch (_state) {
    case WifiState::Idle: return "idle";
    case WifiState::Scanning: return "scanning";
    case WifiState::Connecting: return "connecting";
    case WifiState::Connected: return "connected";
    case WifiState::ApFallback: return "ap-fallback";
    case WifiState::ApForced: return "ap-setup";
  }
  return "?";
}
