// Host-teszt: TimeManager ISO-8601 / formazo fuggvenyek. Elvart ertekek python datetime-mal szamolva.
#include "time_manager.h"
#include <cassert>
int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
int main(){
  time_t t;
  CHECK(TimeManager::parseIso8601("1970-01-01T00:00:00Z",t) && t==0);
  CHECK(TimeManager::parseIso8601("2026-09-16T17:19:17Z",t) && t==1789579157);
  CHECK(TimeManager::parseIso8601("2026-09-16T17:19:17.123456Z",t) && t==1789579157);
  CHECK(TimeManager::parseIso8601("2026-09-16T19:19:17+02:00",t) && t==1789579157);
  CHECK(TimeManager::parseIso8601("2026-09-16T15:19:17-02:00",t) && t==1789579157);
  CHECK(TimeManager::parseIso8601("2024-02-29T12:00:00Z",t) && t==1709208000);
  CHECK(!TimeManager::parseIso8601("2026-09-16T17:19:17",t));
  CHECK(!TimeManager::parseIso8601("garbage",t));
  CHECK(!TimeManager::parseIso8601("",t));
  CHECK(!TimeManager::parseIso8601(nullptr,t));
  CHECK(!TimeManager::parseIso8601("2026-13-16T17:19:17Z",t));
  CHECK(!TimeManager::parseIso8601("2026-09-16T17:19:17Zx",t));
  CHECK(TimeManager::formatRemaining(0)=="--");
  CHECK(TimeManager::formatRemaining(-5)=="--");
  CHECK(TimeManager::formatRemaining(8252)=="2h17m");
  CHECK(TimeManager::formatRemaining(1052)=="17m32s");
  CHECK(TimeManager::formatRemaining(3*86400+4*3600+12*60)=="3d04h");
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
  CHECK(TimeManager::localHHMM(1789579157)=="19:19");   // nyari ido: UTC+2
  CHECK(TimeManager::localHHMM(1768000000)==std::string("00:06")); // 2026-01-09T23:06:40Z -> telen UTC+1
  // Idozonak: elvart ertek python zoneinfo-bol (IANA), a POSIX-string a /usr/share/zoneinfo/<zona> utolso sora.
  setenv("TZ","JST-9",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-17 02:19");  // Asia/Tokyo
  setenv("TZ","JST-9",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-10 08:06");  // Asia/Tokyo
  setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-16 13:19");  // America/New_York
  setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-09 18:06");  // America/New_York
  setenv("TZ","IST-5:30",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-16 22:49");  // Asia/Kolkata
  setenv("TZ","IST-5:30",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-10 04:36");  // Asia/Kolkata
  setenv("TZ","<+04>-4",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-16 21:19");  // Asia/Dubai
  setenv("TZ","<+04>-4",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-10 03:06");  // Asia/Dubai
  setenv("TZ","AEST-10AEDT,M10.1.0,M4.1.0/3",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-17 03:19");  // Australia/Sydney
  setenv("TZ","AEST-10AEDT,M10.1.0,M4.1.0/3",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-10 10:06");  // Australia/Sydney
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1); tzset(); CHECK(TimeManager::localDateTime(1789579157)=="2026-09-16 18:19");  // Europe/London
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1); tzset(); CHECK(TimeManager::localDateTime(1768000000)=="2026-01-09 23:06");  // Europe/London
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
  // 1789579157 = 2026-09-16T17:19:17Z = 19:19 helyi (CEST)
  CHECK(TimeManager::resetText(1789579157, true, 1789579157-8252)=="RESET 2h17m @09.16 19:19");
  CHECK(TimeManager::resetText(1789579157, true, 1789579157-(3*86400+4*3600))=="RESET 3d04h @09.16 19:19");
  CHECK(TimeManager::resetText(1789579157, true, 1789579157+5)=="RESET PASSED 09.16 19:19");
  CHECK(TimeManager::resetText(1789579157, false, 0)=="RESET @09.16 19:19 ?");
  CHECK(TimeManager::validPosixTz("CET-1CEST,M3.5.0,M10.5.0/3"));
  CHECK(TimeManager::validPosixTz("<+04>-4"));
  CHECK(!TimeManager::validPosixTz("CE"));
  CHECK(!TimeManager::validPosixTz("CET -1"));
  CHECK(!TimeManager::validPosixTz("CET\"-1"));
  CHECK(!TimeManager::validPosixTz(nullptr));
  printf("fails=%d\n",fails); return fails;
}
