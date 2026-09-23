// Claude-lekeresek utemezese kulon FreeRTOS taskban (spec 13., 14., 21.).
// - profilonkent CLAUDE_REFRESH_PERIOD_S, a profilok egyenletesen eltolva (N profil -> period/N lepes);
// - egyszerre egy keres; egy profil hibaja csak a sajat backoffjat noveli;
// - a kijelzovaltas ettol fuggetlen, 1 s-os rotacio sem okoz 1 s-os lekerest.
#pragma once
#include <Arduino.h>

class RefreshScheduler {
 public:
  // holdOffMs: indulas utani varakozas az ELSO lekeresig (panikhurok-or, main.cpp).
  void begin(uint32_t holdOffMs = 0);
  uint32_t fetchCount() const { return _fetchCount; }  // diagnosztika (acceptance 19.)

 private:
  static void taskEntry(void *arg);
  void run();
  volatile uint32_t _fetchCount = 0;
  uint32_t _holdOffMs = 0;
};

extern RefreshScheduler refreshScheduler;
