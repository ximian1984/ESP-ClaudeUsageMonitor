// NTP + Europe/Budapest idozona. A visszaszamlalas az ESP32 szinkronizalt orajabol fut (spec 15.).
#pragma once
#include <Arduino.h>
#include <time.h>

#include "config.h"

class TimeManager {
 public:
  void begin(const char *posixTz);  // TZ beallitas; az NTP csak STA-kapcsolattal indul
  void setTimezone(const char *posixTz);  // futas kozben (setup-oldal); a kijelzo azonnal az uj helyi idot mutatja
  const char *timezone() const { return _tz; }
  // POSIX TZ-string formai ellenorzese (nem teljes szintaxis): 3-47 karakter, betu/szam/+-,.:/<>
  static bool validPosixTz(const char *s);
  // Helyi datum+ido a naplohoz/status-hoz: "2026-09-17 15:40"; ures, ha nincs ido.
  static String localDateTime(time_t t);
  void loop(bool staConnected);
  bool synced() const;    // van-e ervenyes (NTP-bol jott) ido
  time_t now() const { return time(nullptr); }
  bool ntpError() const;  // STA el, de hosszu ideje nincs szinkron

  // "HH:MM" helyi idoben (Europe/Budapest); ures, ha nincs ido.
  static String localHHMM(time_t t);
  // "3d04h" / "02:17:32" formatum; "--" ha nem pozitiv.
  static String formatRemaining(long seconds);

  // ISO-8601 UTC idobelyeg -> epoch. Elfogad: "YYYY-MM-DDTHH:MM:SS[.fff](Z|+HH:MM|-HH:MM)".
  // Altalanos segedfuggveny; hogy a Claude-valasz ilyen formatumot hasznal-e, az ⚠ [feltarando].
  static bool parseIso8601(const char *s, time_t &out);

 private:
  char _tz[TZ_POSIX_MAX + 1] = TZ_EUROPE_BUDAPEST;
  bool _started = false;
  uint32_t _startedMs = 0;
};

extern TimeManager timeManager;
