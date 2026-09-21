// Kijelzo: dongle 160x80 ST7735 (display_manager.cpp), CYD nativ 320x240 (display_cyd.cpp). Csak a cache-bol es az allapotokbol rajzol, API-t soha nem hiv (spec 13.).
#pragma once
#include "usage_cache.h"
#include <Arduino.h>

class DisplayManager {
 public:
  void begin();
  // A pillanatnyi framebuffer (RGB565, W*H uint16) a webes kijelzo-tukorhoz; nullptr, ha nincs sprite.
  // Ugyanabban a taskban (loopTask) fut, mint a rajzolas, ezert nem kell zar.
  const uint16_t *framebuffer(int &w, int &h) const;  // CYD-n nincs teljes framebuffer -> streamScreen()
#if defined(BOARD_CYD)
  // CYD: a 320x240-es kepnek nincs teljes framebuffere (153,6 KB); a webes tukorhoz ugyanaz a rajzolo kod savonkent
  // ujrarajzolja az aktualis kepet, es minden savot (bajtcserelt RGB565, sorfolytonos) atad a sinknek.
  typedef void (*ScreenSink)(const uint8_t *data, size_t len);
  bool screenSize(int &w, int &h) const;  // false, ha nincs sav-sprite
  void streamScreen(ScreenSink sink);
#endif
  void showBoot(uint32_t msLeft);  // inditasi ablak: "BOOT = setup"
  void loop();                     // ujrarajzolas (dongle ~5 Hz, CYD ~2 Hz) + profil-rotacio

 private:
  void drawAp();
  void drawWifiWait();
  void drawNoProfiles();
  void drawProfile(int idx, const char *name);
  void drawLastKnown(const char *name, const LastKnownResets &lk);
  void push();
  void applyRot(uint8_t rot);  // negyedfordulat, ConfigManager displayRot

  int _rotPos = 0;
  int _rot = -1;  // -1 = meg nincs beallitva; kulonben 0-3 (negyedfordulat)
  uint32_t _rotSinceMs = 0;
  uint32_t _lastDrawMs = 0;
};

extern DisplayManager displayManager;
