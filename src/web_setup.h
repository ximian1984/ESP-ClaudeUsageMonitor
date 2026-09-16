// Setup weboldal + JSON API (spec 11.). AP- es STA-modban is fut, a 80-as porton.
// Titkot (Wi-Fi jelszo, Claude auth) SOHA nem ad vissza — csak azt, hogy be van-e allitva.
#pragma once

class WebSetup {
 public:
  void begin();
  void loop();
};

extern WebSetup webSetup;
