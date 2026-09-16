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
  CHECK(TimeManager::formatRemaining(8252)=="02:17:32");
  CHECK(TimeManager::formatRemaining(3*86400+4*3600+12*60)=="3d04h12m");
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
  CHECK(TimeManager::localHHMM(1789579157)=="19:19");   // nyari ido: UTC+2
  CHECK(TimeManager::localHHMM(1768000000)==std::string("00:06")); // 2026-01-09T23:06:40Z -> telen UTC+1
  printf("fails=%d\n",fails); return fails;
}
