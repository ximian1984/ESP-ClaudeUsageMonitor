#include "time_manager.h"

#include <esp_sntp.h>

#include "config.h"

TimeManager timeManager;

// 2024-01-01 elotti ido = nincs szinkron (az RTC 1970-rol indul).
static const time_t VALID_EPOCH_MIN = 1704067200;
static const uint32_t NTP_ERROR_AFTER_MS = 60000;

void TimeManager::begin() {
  setenv("TZ", TZ_EUROPE_BUDAPEST, 1);
  tzset();
}

void TimeManager::loop(bool staConnected) {
  if (staConnected && !_started) {
    // configTzTime: TZ + SNTP; az SNTP a hatterben ismetel, nem blokkol.
    configTzTime(TZ_EUROPE_BUDAPEST, NTP_SERVER_1, NTP_SERVER_2);
    _started = true;
    _startedMs = millis();
  }
}

bool TimeManager::synced() const { return time(nullptr) >= VALID_EPOCH_MIN; }

bool TimeManager::ntpError() const { return _started && !synced() && millis() - _startedMs > NTP_ERROR_AFTER_MS; }

String TimeManager::localHHMM(time_t t) {
  if (t < VALID_EPOCH_MIN) return "";
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &lt);
  return buf;
}

String TimeManager::formatRemaining(long s) {
  if (s <= 0) return "--";
  char buf[16];
  long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (d > 0) snprintf(buf, sizeof(buf), "%ldd%02ldh%02ldm", d, h, m);
  else snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", h, m, sec);
  return buf;
}

// Napok 1970-01-01 ota (proleptikus Gergely-naptar), Howard Hinnant days_from_civil algoritmusa.
static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

bool TimeManager::parseIso8601(const char *s, time_t &out) {
  if (!s) return false;
  int Y, M, D, h, m, sec, n = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &m, &sec, &n) != 6 || n == 0) return false;
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60) return false;
  const char *p = s + n;
  if (*p == '.') {  // tort masodperc: eldobjuk
    p++;
    while (*p >= '0' && *p <= '9') p++;
  }
  long offset = 0;
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    int oh, om;
    if (sscanf(p + 1, "%2d:%2d", &oh, &om) != 2) return false;
    offset = (oh * 3600L + om * 60L) * (*p == '+' ? 1 : -1);
    p += 6;
  } else {
    return false;  // idozona nelkuli idot nem talalgatunk
  }
  if (*p != '\0') return false;
  long days = daysFromCivil(Y, (unsigned)M, (unsigned)D);
  out = (time_t)(days * 86400L + h * 3600L + m * 60L + sec - offset);
  return true;
}
