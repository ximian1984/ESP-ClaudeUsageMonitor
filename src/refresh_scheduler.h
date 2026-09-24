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
  // Azonnali lekeres keresé (sikeres be-/ujrabejelentkezes utan). Enelkul a hibabol jovo backoff (pl. RE-LOGIN:
  // 10 perc) akkor is kivarodik, ha a felhasznalo kozben megadta az uj tokent — kifelé ugy latszik, mintha az
  // eszkoz nem is probalkozna (projektgazda, 2026-09-24).
  void requestNow(int idx);

 private:
  static void taskEntry(void *arg);
  void run();
  volatile uint32_t _fetchCount = 0;
  uint32_t _holdOffMs = 0;
  volatile uint32_t _kick = 0;  // bitmaszk: melyik profil kert azonnali lekerest
};

extern RefreshScheduler refreshScheduler;
