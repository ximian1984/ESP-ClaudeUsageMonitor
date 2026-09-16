// LILYGO T-Dongle-S3 — Claude Usage Monitor
// Fo ciklus millis()-alapu, nem blokkol; a Claude-lekeres kulon FreeRTOS taskban fut (refresh_scheduler).
#include <Arduino.h>

#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "refresh_scheduler.h"
#include "time_manager.h"
#include "usage_cache.h"
#include "web_setup.h"
#include "wifi_manager.h"

// Forced setup (spec 10.). ⚠ A LilyGO doksi szerint a BOOT gombot a TAP BEDUGASAKOR nyomva tartva
// az ESP32-S3 letoltesi modba lep, es a firmware el sem indul (docs/en/t-dongle-s3/REAMDE.MD,
// "Do not press the BOOT button while powering on"). Ezert a gombot INDULAS UTAN, a kijelzon
// jelzett BOOT_WINDOW_MS ablakban kell megnyomni; futas kozben BOOT_LONGPRESS_MS hosszu nyomas is jo.
static bool bootWindow() {
  uint32_t start = millis();
  while (millis() - start < BOOT_WINDOW_MS) {
    displayManager.showBoot(BOOT_WINDOW_MS - (millis() - start));
    if (digitalRead(PIN_BOOT_BTN) == LOW) return true;
    delay(50);  // indulaskor, meg semmi mas nem fut
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[main] %s, firmware %s\n", BOARD_NAME, FW_VERSION);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  configManager.begin();
  usageCache.begin();
  timeManager.begin();
  displayManager.begin();

  bool forced = bootWindow();
  if (forced) Serial.println("[main] BOOT gomb -> forced setup");

  wifiManager.begin(forced);
  webSetup.begin();
  refreshScheduler.begin();
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

  wifiManager.loop();
  timeManager.loop(wifiManager.staConnected());
  webSetup.loop();
  displayManager.loop();
  delay(2);  // a tobbi tasknak (IDLE watchdog)
}
