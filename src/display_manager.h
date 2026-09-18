// 160x80 ST7735 megjelenites. Csak a cache-bol es az allapotokbol rajzol, API-t soha nem hiv (spec 13.).
#pragma once
#include "usage_cache.h"
#include <Arduino.h>

class DisplayManager {
 public:
  void begin();
  // A pillanatnyi framebuffer (RGB565, W*H uint16) a webes kijelzo-tukorhoz; nullptr, ha nincs sprite.
  // Ugyanabban a taskban (loopTask) fut, mint a rajzolas, ezert nem kell zar.
  const uint16_t *framebuffer(int &w, int &h) const;
  void showBoot(uint32_t msLeft);  // inditasi ablak: "BOOT = setup"
  void loop();                     // ~5 Hz ujrarajzolas + profil-rotacio

 private:
  void drawAp();
  void drawWifiWait();
  void drawNoProfiles();
  void drawProfile(int idx, const char *name);
  void drawLastKnown(const char *name, const LastKnownResets &lk);
  void push();
  void applyFlip(bool flip);

  int _rotPos = 0;
  int _flip = -1;  // -1 = meg nincs beallitva; kulonben 0/1 (180 fok)
  uint32_t _rotSinceMs = 0;
  uint32_t _lastDrawMs = 0;
};

extern DisplayManager displayManager;
