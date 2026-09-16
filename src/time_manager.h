// NTP + Europe/Budapest idozona. A visszaszamlalas az ESP32 szinkronizalt orajabol fut (spec 15.).
#pragma once
#include <Arduino.h>
#include <time.h>

class TimeManager {
 public:
  void begin();           // TZ beallitas; az NTP csak STA-kapcsolattal indul
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
  bool _started = false;
  uint32_t _startedMs = 0;
};

extern TimeManager timeManager;
