// Claude Usage Monitor — LILYGO T-Dongle-S3 es ESP32-2432S028R (CYD); a lapkat a platformio.ini env-je valasztja (config.h)
// Fo ciklus millis()-alapu, nem blokkol; a Claude-lekeres kulon FreeRTOS taskban fut (refresh_scheduler).
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "admin_auth.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "refresh_scheduler.h"
#include "time_manager.h"
#include "token_cache.h"
#include "usage_cache.h"
#include "web_setup.h"
#include "wifi_manager.h"

// loopTask stack: az alap 8 KB (core main.cpp:14) KEVES. A configManager.snapshot() egy DeviceConfig-masolat
// (~4,7 KB: 5 Claude-profil x access+refresh+scope) a stacken; a scan utani tryNextCandidate-ben ez
// "Stack canary watchpoint triggered (loopTask)" boot-loopot okozott (vason mert, 2026-09-17, tervdoksi 2.11).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Forced setup (spec 10.). ⚠ A LilyGO doksi szerint a BOOT gombot a TAP BEDUGASAKOR nyomva tartva
// az ESP32-S3 letoltesi modba lep, es a firmware el sem indul (docs/en/t-dongle-s3/REAMDE.MD,
// "Do not press the BOOT button while powering on"). Ezert a gombot INDULAS UTAN, a kijelzon
// jelzett BOOT_WINDOW_MS ablakban kell megnyomni; futas kozben BOOT_LONGPRESS_MS hosszu nyomas is jo.
// A CYD-n ugyanez all: a BOOT gomb ott is az IO0 (witnessmenow PINS.md), a klasszikus ESP32 strapping-labe.
static bool bootWindow() {
  uint32_t start = millis();
  while (millis() - start < BOOT_WINDOW_MS) {
    displayManager.showBoot(BOOT_WINDOW_MS - (millis() - start));
    if (digitalRead(PIN_BOOT_BTN) == LOW) return true;
    delay(50);  // indulaskor, meg semmi mas nem fut
  }
  return false;
}

// ⛔ Panikhurok-or (mérve 2026-09-23). Egy panik/watchdog utan a lapka ujraindul, es INDULASKOR AZONNAL
// lekerdez — ha a panikot epp a lekeres okozza, ez vegtelen bootloop. Az NVS-ben szamoljuk az egymast koveto
// rendellenes ujrainditasokat: BOOT_PANIC_LIMIT utan az elso lekeres var, hogy a setup-oldal es a kijelzo
// elerheto maradjon. BOOT_HEALTHY_MS zavartalan uzem utan a szamlalo nullazodik (bootGuardHealthy).
static const char *BOOT_NS = "bootguard";

static uint8_t bootGuardBegin() {
  esp_reset_reason_t r = esp_reset_reason();
  bool abnormal = r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT;
  Preferences p;
  p.begin(BOOT_NS, false);
  uint8_t n = p.getUChar("panic", 0);
  n = abnormal ? (n < 255 ? (uint8_t)(n + 1) : n) : 0;
  p.putUChar("panic", n);
  p.end();
  Serial.printf("[boot] reset-ok %d%s, egymas utani panik-ujrainditas: %u\n", (int)r,
                abnormal ? " (RENDELLENES)" : "", (unsigned)n);
  return n;
}

// A szamlalot csak HOSSZU, zavartalan uzem nullazza — enelkul minden ujraindulas tiszta lappal indulna,
// es a hurkot sosem ismernenk fel.
static void bootGuardHealthy() {
  static bool cleared = false;
  if (cleared || millis() < BOOT_HEALTHY_MS) return;
  cleared = true;
  Preferences p;
  p.begin(BOOT_NS, false);
  if (p.getUChar("panic", 0)) {
    p.putUChar("panic", 0);
    Serial.println("[boot] zavartalan uzem -> panik-szamlalo nullazva");
  }
  p.end();
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[main] %s, firmware %s\n", BOARD_NAME, FW_VERSION);
  uint8_t panics = bootGuardBegin();
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  configManager.begin();
  adminAuth.begin();
  usageCache.begin();
  tokenCache.begin();
  timeManager.begin(configManager.heapSnapshot()->tz);
  displayManager.begin();

  bool forced = bootWindow();
  if (forced) Serial.println("[main] BOOT gomb -> forced setup");

  wifiManager.begin(forced);
  webSetup.begin();
  refreshScheduler.begin(panics >= BOOT_PANIC_LIMIT ? BOOT_PANIC_HOLDOFF_MS : 0);
}

void loop() {
  static uint32_t btnDownMs = 0;
  if (digitalRead(PIN_BOOT_BTN) == LOW) {
    if (btnDownMs == 0) btnDownMs = millis();
    if (millis() - btnDownMs > BOOT_LONGPRESS_MS && wifiManager.state() != WifiState::ApForced) {
      Serial.println("[main] BOOT hosszu nyomas -> forced setup");
      wifiManager.enterForcedSetup();
    }
  } else {
    btnDownMs = 0;
  }

#ifdef BOOTGUARD_TEST  // csak a panikhurok-or MERESEHEZ: szandekos panik indulas utan (soha nem kerul kiadasba)
  if (millis() > 8000) abort();
#endif
  wifiManager.loop();
  timeManager.loop(wifiManager.staConnected());
  webSetup.loop();
  displayManager.loop();
  bootGuardHealthy();
  delay(2);  // a tobbi tasknak (IDLE watchdog)
}
