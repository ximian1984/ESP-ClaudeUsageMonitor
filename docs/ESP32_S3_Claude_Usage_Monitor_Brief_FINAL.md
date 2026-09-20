# ESP32-S3 LILYGO T-Dongle-S3 – Claude Usage Monitor
## Development Brief

## 1. Cél
Készíts egy önálló LILYGO T-Dongle-S3 (ESP32-S3) alapú Claude Usage Monitor eszközt. USB-ről kap tápot, Wi-Fi-n közvetlenül kommunikál a Claude webes szolgáltatásával, és a beépített 160×80 pixeles kijelzőn mutatja a usage/kvóta adatokat.

**Nincs Mac Mini proxy, Raspberry Pi, külön szerver vagy egyéb köztes komponens.**

A rendszer:
- több Wi-Fi profilt kezel;
- több Claude előfizetést/fiókprofilt kezel;
- jelszavas AP fallbacket biztosít;
- webesen konfigurálható;
- NVS-ben tárolja a konfigurációt;
- hibák esetén robusztusan működik.

## 2. Hardver
**LILYGO T-Dongle-S3**
- ESP32-S3
- Wi-Fi
- USB
- 0,96" színes LCD
- ST7735
- 160×80 pixel
- BOOT/felhasználói gomb(ok)

A kijelző GPIO-kiosztását a T-Dongle-S3 aktuális hivatalos board definition/dokumentáció alapján kell meghatározni. **Ne feltételezz pinoutot.**

## 3. Több Claude profil
Minimum **5 Claude profil** legyen kezelhető.

Minden profil:
- Name/Alias
- Organization ID
- web session/authentication adat
- Enabled/Disabled

Példa:
- SZILARD
- WORK
- TEST
- BACKUP
- OTHER

Minden profil usage adatait külön kell kezelni és cache-elni.

## 4. Claude usage adatok
Minden profilhoz legalább:
- 5 órás/session utilization %
- 5 órás/session remaining %
- session reset timestamp
- session resetig hátralévő idő
- 7 napos/weekly utilization %
- weekly remaining %
- weekly reset timestamp
- weekly resetig hátralévő idő

Ha a Claude response további limiteket biztosít, az architektúra legyen bővíthető.

## 5. Claude endpoint
A korábban azonosított endpoint mintája:

    GET https://claude.ai/api/organizations/{organization_id}/usage

Ez nem hivatalos, stabil, publikus API-ként kezelendő.

A fejlesztés előtt:
1. ellenőrizd a Claude webes kliens aktuális működését;
2. ellenőrizd az aktuális endpointot;
3. ellenőrizd a tényleges request headereket/cookie-kat;
4. ellenőrizd a tényleges JSON response struktúrát;
5. csak a valós response alapján implementáld a parser-t.

**Ne találj ki JSON mezőket vagy authentication mechanizmust.**

A `claude_client` és `usage_parser` legyen külön modul.

## 6. Claude autentikáció
Minden Claude profilhoz külön authentication/session adat legyen tárolható.

Követelmények:
- ne legyen hard-code-olva;
- ne kerüljön Serial logba;
- ne jelenjen meg normál kijelzőn;
- NVS-ben legyen tárolva;
- setup oldalon password mező legyen.

## 7. Több Wi-Fi profil
Minimum **5 Wi-Fi profil**.

Minden profil:
- SSID
- Password
- Enabled
- Priority

Példa:

    Profile 1: HomeWiFi     Priority 20
    Profile 2: OfficeWiFi   Priority 10
    Profile 3: BackupWiFi   Priority 5

A webes setupban legyen:
- hozzáadás;
- módosítás;
- törlés;
- engedélyezés/tiltás;
- prioritás módosítása;
- Wi-Fi scan;
- scan eredményből SSID kiválasztása.

## 8. Wi-Fi automatikus választás
Indításkor:
1. töltsd be az engedélyezett profilokat;
2. végezz Wi-Fi scan-t;
3. keresd meg az ismert hálózatokat;
4. válaszd a legmagasabb prioritású elérhető profilt;
5. próbálj csatlakozni;
6. sikertelenség esetén próbáld a következő profilt;
7. ha egyik sem működik, AP fallback.

Azonos prioritás esetén opcionálisan RSSI alapján válassz.

## 9. AP fallback
Ha egyetlen Wi-Fi profil sem működik, automatikusan induljon saját AP.

**Az AP kötelezően WPA2-PSK jelszavas.**

Példa:

    SSID: ClaudeMonitor-A3F2
    Password: Setup-A3F2

Az SSID és jelszó lehetőleg a MAC-cím alapján legyen egyedi.

Setup cím:

    http://192.168.4.1

A kijelző AP módban mutassa:
- AP SSID;
- AP password;
- IP cím.

## 10. Forced setup
A BOOT gomb induláskor történő nyomva tartásával azonnal induljon AP setup mód.

Ez akkor is működjön, ha a mentett Wi-Fi egyébként elérhető.

## 11. Setup weboldal
A setup oldal kezelje a Wi-Fi és Claude konfigurációt.

### Wi-Fi
- Wi-Fi scan
- profilok listája
- add/edit/delete
- enabled/disabled
- priority
- SSID
- password

### Claude
- profilok listája
- add/edit/delete
- enabled/disabled
- profile name
- Organization ID
- session/authentication adat

### Display
Legyen konfigurálható:

    Display rotation interval: 1–60 sec

Alapérték például:

    5 sec

### Device
Mutassa:
- board;
- firmware version;
- Wi-Fi state;
- IP;
- RSSI;
- utolsó Claude update.

## 12. Több Claude profil kijelzőn
Az engedélyezett Claude profilok között automatikusan váltson.

Például 1 másodperces beállításnál:

    SZILARD → 1 sec → WORK → 1 sec → TEST → 1 sec → ...

A profil neve mindig legyen látható.

Példa:

    ┌──────────────────────────────┐
    │ SZILARD                      │
    │                              │
    │ SESSION          73% LEFT    │
    │ ███████████████░░░░          │
    │                              │
    │ RESET IN       02:17:32      │
    │                              │
    │ WEEKLY          59% LEFT     │
    └──────────────────────────────┘

## 13. Kritikus: kijelzőváltás ≠ API lekérés
A kijelzőváltás lehet akár **1 másodpercenként**, de ez **nem jelenthet 1 másodperces Claude API lekérést**.

Minden Claude profil saját usage cache-t kap.

Példa:

    SZILARD:
      session = 73%
      weekly = 59%
      last_update = 12:00:00

    WORK:
      session = 41%
      weekly = 82%
      last_update = 12:00:20

A kijelző a cache-elt adatokat mutatja.

## 14. API refresh stratégia
Javasolt profilonként kb. **60 másodperces refresh**.

Több profil esetén a lekéréseket oszd el időben.

Például 3 profil:

    00 sec → Profile 1
    20 sec → Profile 2
    40 sec → Profile 3
    60 sec → Profile 1

A pontos stratégia legyen úgy kialakítva, hogy ne generáljon felesleges API-forgalmat.

Egy profil hibája ne állítsa le a többi profil frissítését.

## 15. NTP / idő
Használj NTP-t.

Alapértelmezett timezone:

    Europe/Budapest

A Claude reset timestampet megfelelően kezeld, beleértve az UTC → helyi idő konverziót.

A countdown az ESP32 szinkronizált órája alapján fusson.

## 16. Kijelző
A 160×80 pixeles kijelzőn legyen:
- profilnév;
- session remaining %;
- session progress bar;
- session reset countdown;
- session reset időpont;
- weekly remaining %;
- weekly reset, ha rendelkezésre áll.

A layoutot optimalizáld a 160×80-as kijelzőre.

## 17. Hibakezelés
Kezeld legalább:
- Wi-Fi connection failure;
- internet unavailable;
- Claude HTTP error;
- HTTP 401/authentication error;
- JSON/parser error;
- NTP error.

Példák:

    CLAUDE AUTH
    ERROR 401

vagy:

    USAGE PARSE
    ERROR

Egy hibás Claude profil ne állítsa le a többi profilt.

## 18. Offline / utolsó ismert adat
Claude endpoint hiba esetén:
- utolsó érvényes usage adat maradjon meg;
- jelenjen meg az adat kora;
- többi profil működjön.

Példa:

    SESSION 73% LEFT
    RESET IN 02:11:05
    DATA: 3m OLD

Ha a reset timestamp lejárt, ne mutass hamis countdown értéket.

## 19. Biztonság
- Wi-Fi password ne kerüljön Serial logba.
- Claude credential/session ne kerüljön Serial logba.
- credential ne legyen hard-code-olva.
- NVS használata.
- AP fallback mindig WPA2-jelszavas.
- Claude felé HTTPS.
- TLS certificate validation.
- `setInsecure()` ne legyen production firmware-ben.
- hibás JSON ne okozzon crash-t.
- HTTP timeout legyen.
- memóriaszivárgást kerülni kell.

## 20. Firmware architektúra
Platform:

**PlatformIO + Arduino framework**

Javasolt:

    src/
    ├── main.cpp
    ├── config.h
    ├── config_manager.cpp
    ├── config_manager.h
    ├── wifi_manager.cpp
    ├── wifi_manager.h
    ├── web_setup.cpp
    ├── web_setup.h
    ├── claude_client.cpp
    ├── claude_client.h
    ├── usage_parser.cpp
    ├── usage_parser.h
    ├── time_manager.cpp
    ├── time_manager.h
    ├── display_manager.cpp
    ├── display_manager.h
    └── secrets.h

A modulok felelőssége legyen elkülönítve.

## 21. Nem blokkoló működés
Kerüld a hosszú blokkoló `delay()` használatát.

Maradjon reszponzív:
- web server;
- countdown;
- Wi-Fi kezelés;
- API lekérés;
- kijelzőváltás.

Használj `millis()` alapú időzítést vagy megfelelő FreeRTOS taskokat.

## 22. Firmware verzió
Legyen verzió, például:

    1.0.0

A setup oldal jelenítse meg.

Firmware update után a konfiguráció maradjon meg.

## 23. README
A projekt README-je tartalmazza:
1. hardver;
2. T-Dongle-S3 board beállítás;
3. PlatformIO telepítés;
4. build;
5. upload;
6. első indítás;
7. AP fallback;
8. több Wi-Fi profil;
9. több Claude profil;
10. display rotation;
11. Claude authentication;
12. endpoint;
13. hibaelhárítás;
14. biztonsági megjegyzések;
15. ismert korlátozások.

## 24. Acceptance criteria
A fejlesztés akkor sikeres, ha:
1. Fordul ESP32-S3 T-Dongle-S3-ra.
2. USB-n feltölthető.
3. Legalább 5 Wi-Fi profil tárolható.
4. Wi-Fi profil hozzáadható.
5. Wi-Fi profil módosítható.
6. Wi-Fi profil törölhető.
7. Wi-Fi profil engedélyezhető/tiltható.
8. Wi-Fi profil prioritása módosítható.
9. Automatikusan kiválasztja az elérhető ismert Wi-Fi-t.
10. Sikertelen Wi-Fi esetén WPA2-jelszavas AP indul.
11. AP fallback esetén 192.168.4.1 setup elérhető.
12. Wi-Fi scan elérhető.
13. Legalább 5 Claude profil tárolható.
14. Claude profil hozzáadható, módosítható és törölhető.
15. Claude profil engedélyezhető/tiltható.
16. Minden Claude profilhoz külön auth/session adat tárolható.
17. Minden Claude profilhoz külön Organization ID tárolható.
18. Claude profilok között 1–60 sec között konfigurálható kijelzőváltás működik.
19. 1 sec rotation esetén sem történik 1 sec-es API lekérés.
20. Minden profil usage adata külön cache-elhető.
21. API refresh profilok között elosztva működik.
22. Session remaining megjelenik.
23. Session reset időpont megjelenik.
24. Session reset countdown megjelenik.
25. Weekly remaining megjelenik, ha az API biztosítja.
26. Weekly reset megjelenik, ha az API biztosítja.
27. NTP alapján pontos countdown működik.
28. Egy profil API hibája nem állítja le a többi profilt.
29. Claude credentials nem kerülnek logba.
30. Kijelző UI használható 160×80 pixelen.
31. Konfiguráció NVS-ben megmarad reboot után.
32. BOOT gombbal forced setup indítható.
33. README elkészül.

## 25. Kritikus fejlesztési szabály
**Ne találj ki Claude API endpointot, JSON mezőket, cookie-kat vagy authentication headereket.**

A tényleges Claude webes működést először vizsgáld meg, majd a valós HTTP request/response alapján implementálj.

Ha a Claude megváltoztatja a webes usage API-t vagy authentication mechanizmust, a módosítást elsősorban a `claude_client` és `usage_parser` modulokra korlátozd.

## 26. Végső cél
Egyetlen USB-ről táplált **LILYGO T-Dongle-S3**, amely közvetlenül kommunikál Claude-dal, több Claude profilt és több Wi-Fi profilt kezel, a Claude usage adatokat cache-eli, és a 160×80-as kijelzőn konfigurálható időközzel váltogatja az előfizetések adatait.

**Külső Mac Mini, Raspberry Pi, Docker konténer vagy egyéb köztes szerver nem megengedett.**
