# Claude Usage Monitor — LILYGO T-Dongle-S3

Önálló, USB-ről táplált kütyü. Wi-Fi-n közvetlenül a claude.ai-tól kéri le a Claude-használati kvótát, és a
beépített 160×80-as kijelzőn váltogatja több Claude-fiók adatait. Nincs közbülső szerver.

Spec: [`../ESP32_S3_Claude_Usage_Monitor_Brief_FINAL.md`](../ESP32_S3_Claude_Usage_Monitor_Brief_FINAL.md) ·
Mért alapok, döntések: [`PLAN.md`](PLAN.md)

> **Állapot (2026-09-16):** a firmware fordul, de **vason még nem futott**. A usage-válasz feldolgozása
> (`usage_parser`) **még nincs kész**: a claude.ai valós válaszára vár, addig a kijelző `PARSER TODO`-t
> mutat. Amit itt `⚠ [vason mérendő]` jelöl, az a forrásból következik, nem mérésből.

---

## 1. Hardver

**LILYGO T-Dongle-S3** — a sima változat, nem a -Plus (projektgazda, 2026-09-16).

| | |
|---|---|
| SoC | ESP32-S3, 16 MB flash, **PSRAM nincs** |
| Kijelző | 0,96" ST7735, 160×80, SPI |
| Gomb | BOOT (GPIO 0) |
| Egyéb | APA102 RGB LED, microSD-foglalat, QWIIC — a firmware ezeket nem használja |

Kijelző-pinek (hivatalos LilyGO forrás, fájl:sor a [`PLAN.md`](PLAN.md) 1. pontjában):
MOSI 3, SCLK 5, CS 4, DC 2, RST 1, háttérfény 38.

## 2. T-Dongle-S3 board beállítás

A board-definíció a hivatalos LilyGO repóból van bemásolva: [`boards/dongles3.json`](boards/dongles3.json)
(`Xinyuan-LilyGO/T-Dongle-S3` @ `bb76546`). A [`platformio.ini`](platformio.ini) rögzíti:

- `platform = espressif32@6.12.0` (ugyanaz, mint a LilyGO-nál) → Arduino-ESP32 **2.0.17**;
- `board = dongles3`, partíciók `default_16MB.csv`;
- `TFT_eSPI 2.5.43`, a LilyGO `Setup209_LilyGo_T_Dongle_S3.h` értékeivel, build-flagként;
- `ArduinoJson 7`.

## 3. PlatformIO telepítés

macOS-en mérve (2026-09-16): `brew install platformio` → `PlatformIO Core, version 6.2.0`.
Másik út: VS Code + PlatformIO IDE bővítmény. Az első build letölti a platformot és a könyvtárakat.

## 4. Build

```sh
cd ClaudeUsageMonitor
pio run
```

Mérve: `SUCCESS`, RAM 15,3 %, Flash 15,2 %.

⚠ **Zsákutca:** `pio run -v` (bőbeszédű mód) tiszta buildnél `FAILED`-et ad a `firmware.bin` lépésnél:
`TypeError: unsupported operand type(s) for +: '_Null' and 'str'`. Ez a PlatformIO 6.2.0 kiírásának
hibája, nem a kódé: ugyanaz a tiszta build `-v` nélkül `SUCCESS`, és elkészül a `firmware.bin`.

Gépi teszt a hardverfüggetlen részekre (idő-parszolás, formázás, usage-parser kapuval és anélkül).
Az ArduinoJson-t a PlatformIO letöltéséből veszi, ezért előbb egy `pio run` kell:

```sh
sh test/host/run.sh     # fails=0, parser fails=0, parser fails=0
```

## 5. Upload

```sh
pio run -t upload
pio device monitor       # soros napló, 115200 baud
```

`⚠ [vason mérendő]` A LilyGO leírása szerint (`docs/en/t-dongle-s3/REAMDE.MD`): ha a feltöltés nem
megy, **bedugás közben tartsd nyomva a BOOT gombot** → letöltési mód. Feltöltés után húzd ki, és dugd
vissza **gomb nélkül**, különben letöltési módban marad.

## 6. Első indítás

1. Bedugás után 3 másodpercig a kijelzőn: `Press BOOT now for setup mode` (lásd 7.).
2. Ha még nincs Wi-Fi-profil, a lapka nem talál ismert hálózatot → **AP fallback** indul.
3. A kijelzőn megjelenik az AP neve, a jelszava és a cím.
4. Csatlakozz az AP-ra, nyisd meg a `http://192.168.4.1` címet, vegyél fel Wi-Fi- és Claude-profilt.
5. Wi-Fi-profil mentése után a lapka rögtön megpróbál csatlakozni. Siker esetén az AP leáll, és a
   setup-oldal a lapka új, helyi IP-címén érhető el (a kijelző kiírja, ha nincs Claude-profil).

A konfiguráció NVS-ben van, újraindítás és firmware-frissítés után is megmarad.

## 7. AP fallback és forced setup

- **SSID:** `ClaudeMonitor-XXXX`, ahol `XXXX` a MAC-cím utolsó két bájtja.
- **Jelszó:** első induláskor generált, 10 karakteres véletlen jelszó (WPA2-PSK), NVS-ben tárolva.
  AP-módban a kijelzőn látszik.
  *Eltérés a spectől:* a spec `Setup-A3F2` mintájában a jelszó a sugárzott SSID-ből kiolvasható, tehát nem titok.
- **Cím:** `http://192.168.4.1`
- **Fallback:** ha egyik Wi-Fi-profil sem működik. 5 percenként újrapróbál, ha nincs kliens az AP-n.
  Profil mentésekor azonnal újrapróbál.
- **Forced setup:** a BOOT gombot **bedugás után**, a 3 s-os ablakban kell megnyomni, vagy futás közben
  5 s-ig nyomva tartani. Ilyenkor az AP akkor is elindul, ha a mentett Wi-Fi elérhető. Kilépni
  a setup-oldal **Restart** gombjával lehet.
  *Eltérés a spectől:* a spec „induláskor nyomva tartást" ír, de a bedugáskor nyomott BOOT a lapkát
  letöltési módba teszi, és a firmware el sem indul (LilyGO doksi, lásd 5.).

## 8. Több Wi-Fi profil

Legfeljebb **5** profil: SSID, jelszó, engedélyezve, prioritás (-1000…1000, a nagyobb az előnyösebb).
A setup-oldalon felvehető, szerkeszthető, törölhető, tiltható, a **Scan** gombbal pedig a látható
hálózatok közül választható SSID.

Választás induláskor és kapcsolatvesztéskor:

1. scan;
2. az engedélyezett profilok közül azok, amelyek látszanak;
3. prioritás szerint csökkenő sorrend, azonos prioritásnál az erősebb jel (RSSI);
4. sorban próbálja, jelöltenként 15 s-ig;
5. ha egyik sem megy → AP fallback.

Szerkesztésnél az üresen hagyott jelszómező megtartja a tároltat. Nyílt hálózathoz pipáld be a
„clear stored password" jelölőt.

## 9. Több Claude profil

Legfeljebb **5** profil: név (max. 12 karakter, a kijelzőn mindig látszik), Organization ID (UUID),
session/auth érték (jelszómező), engedélyezve.

- Minden profil saját cache-t kap. Hiba esetén az utolsó érvényes adat megmarad, és a kijelző mutatja a korát.
- Profilonként **60 s**-onként frissít, a profilok egyenletesen eltolva (3 profil: 0 / 20 / 40 s).
  Egyszerre legfeljebb egy kérés megy.
- Hibánál visszalép: átmeneti hibánál duplázva, legfeljebb 15 percig; hitelesítési hibánál 10 percig
  (a szerver `x-should-retry: false`-t küld).
- Egy profil hibája a többit nem állítja meg.

## 10. Display rotation

A setup-oldal **Display** részén 1–60 s (alapérték 5 s). A váltás **csak a megjelenített profilt**
cseréli, lekérést nem indít: 1 s-os rotációnál is profilonként 60 s marad a frissítés. A setup-oldal
„Claude requests since boot" számlálója ezt mutatja.

Kijelző-elrendezés:

```
SZILARD                    12s
SESSION             73% LEFT
[██████████████░░░░░░░░░░░░░]
RESET 02:17:32 @14:30
WEEKLY              59% LEFT
RESET 3d04h12m @Mon 09:00
```

A jobb felső sarokban az adat kora (`3m OLD` sárgán, ha régebbi 2 percnél), illetve `NO WIFI`,
`NTP ERR` vagy `ERR 403`. Ha a reset ideje lejárt, `RESET PASSED` jelenik meg, nem hamis visszaszámlálás.

## 11. Claude authentication

⚠ **[feltárandó]** A hitelesítés pontos módja még **nincs igazolva**.

- Amit mértünk (hamis értékekkel, [`PLAN.md`](PLAN.md) 2.3): a szerver a `sessionKey=sk-ant-sid01-…`
  sütit külön ágon kezeli.
- A firmware jelenleg **ideiglenesen** `Cookie: sessionKey=<a beírt érték>`-et küld
  (`src/claude_client.cpp`, `applyAuth()`).
- Egy közösségi macOS-app forrása (`linuxlewis/claude-usage`, 2026-02) ugyanezt a sütit küldi, plusz egy
  `anthropic-client-platform: web_claude_ai` fejlécet. Ez utóbbit a firmware most nem küldi ([`PLAN.md`](PLAN.md) 2.5).
- A végleges alakot és azt, hogy honnan kell kimásolni az értéket, a projektgazda valós mintája dönti el.
  Addig ez a szakasz szándékosan nem ad lépéseket.

A tárolt érték soha nem jelenik meg újra: a setup-oldal csak `set` / `missing` jelzést mutat.
Szerkesztésnél az üresen hagyott mező megtartja a tárolt értéket.

## 12. Endpoint

```
GET https://claude.ai/api/organizations/{organization_uuid}/usage
```

Nem hivatalos, nem stabil API. Mérve, hitelesítés nélkül ([`PLAN.md`](PLAN.md) 2.):

- **HTTP/1.1**-en (ezt használja az ESP32 `HTTPClient`) az origin JSON-t válaszol. **HTTP/2**-n
  a Cloudflare kihívást ad (`403`, `cf-mitigated: challenge`).
- Rossz alakú UUID: `400 invalid_request_error`. Hiányzó vagy rossz session: `403 permission_error`,
  `error_code: account_session_invalid`.
- A sikeres (`200`) válasz szerkezete **még nincs mérve**. A parser két közösségi forrásból ismert alakot
  kezel (legfelső szintű `five_hour`/`seven_day` `utilization`+`resets_at`, illetve `raw_limits` tokenekkel),
  de **kapuzott**: alapbuildben `PARSER TODO`-t ad. Vason-próbához:
  `PLATFORMIO_BUILD_FLAGS="-DUSAGE_PARSER_ENABLE=1" pio run -t upload`. Részletek: [`PLAN.md`](PLAN.md) 2.5.

TLS: tanúsítvány-ellenőrzés az ISRG Root X1 és X2 gyökérrel (`src/ca_certs.h`). A lánc 2026-09-16-án:
Let's Encrypt YE2 → ISRG Root YE → X2 → X1.

## 13. Hibaelhárítás

| Kijelző | Jelentés | Teendő |
|---|---|---|
| `NO WIFI - SETUP` + SSID/PASS | egyik Wi-Fi-profil sem működik | csatlakozz az AP-ra, ellenőrizd a profilokat |
| `NO WIFI` a sarokban + `waiting WiFi` (Claude-profil nélkül: `WIFI scanning/connecting`) | csatlakozás folyamatban vagy megszakadt | várj (jelöltenként max. 15 s); ha nem jön, AP fallback indul |
| `waiting NTP` / `NTP ERR` / `RESET ? (NO TIME)` | nincs pontos idő (TLS-hez is kell) | internetkapcsolat, UDP 123 engedélyezése |
| `NO INTERNET` | DNS/TCP/TLS hiba | hálózat; ha tartós: tanúsítványlánc-váltás (12.) |
| `TIMEOUT` | 10 s alatt nem jött válasz | automatikusan újrapróbál |
| `CLOUDFLARE` / `ERROR 403` | Cloudflare-kihívás, nem jutott el a Claude-ig | ⚠ [vason mérendő] a fő kockázat, lásd PLAN 2.1 |
| `CLAUDE AUTH` / `ERROR 403` vagy `401` | lejárt vagy rossz session | új session-érték a profilba |
| `RATE LIMIT` / `ERROR 429` | túl sok kérés | automatikus visszalépés |
| `CLAUDE HTTP` / `ERROR nnn` | egyéb HTTP-hiba | a soros napló a státuszkódot kiírja |
| `USAGE PARSE` | a válasz nem JSON, vagy nincs benne sem `five_hour`, sem `seven_day` | a Claude API változhatott → `usage_parser` |
| `PARSER TODO` | a válasz felismert alakú, de a parser kapuja zárva | valós mintára vár (README 12.) |
| `NOT SET UP` | hiányzik az Organization ID vagy az auth | setup-oldal |

Elfelejtett admin-jelszó: forced setup (7.), abban a módban nem kell jelszó.

## 14. Biztonsági megjegyzések

- Titok (Wi-Fi-jelszó, Claude session) csak NVS-ben van. Nincs a kódban, nincs Serial-logban, nincs
  a kijelzőn, és a setup-oldal sem küldi vissza.
- Claude felé csak HTTPS, tanúsítvány-ellenőrzéssel. `setInsecure()` nincs. HTTP-timeout 10 s,
  a válasz legfeljebb 16 KB.
- Az AP mindig WPA2-PSK, véletlen jelszóval.
- **Admin-jelszó (opcionális):**
  - Ha be van állítva, minden módosító kéréshez (mentés, törlés, scan, restart) belépés kell.
  - NVS-ben csak só + PBKDF2-HMAC-SHA256 (4096 iteráció) kivonat van, a jelszó maga nincs.
  - Belépés után memóriában tartott token jár, 30 perc tétlenség után lejár.
  - 5 hibás próbálkozás után 60 s zárolás.
  - Forced setup módban nem kell jelszó (fizikai hozzáférés = helyreállítás).
- A módosító kérések `X-CMon: 1` fejlécet kívánnak (CSRF ellen). A dinamikus adat `textContent`-tel
  kerül az oldalba (a scannelt SSID idegen adat).
- ⚠ Az eszköz **sima HTTP**-t szolgál ki: az admin-jelszó és a beírt session-érték titkosítatlanul
  megy át a helyi hálózaton vagy az AP-n. Olvasni (`/api/status`, `/api/config`: SSID-k,
  profilnevek, org-ID-k) jelszó nélkül is lehet, titkot ezek nem tartalmaznak.
- A hibás JSON nem okoz összeomlást: az ArduinoJson hibakódot ad, a firmware `USAGE PARSE`-t mutat.

## 15. Ismert korlátozások

- **Vason még semmi nem futott.** Nincs mérve: kijelző-orientáció, háttérfény-polaritás (a LilyGO
  források ellentmondanak), TLS-kézfogás heap- és stackigénye, Wi-Fi-állapotgép, webszerver.
- **A Cloudflare dönthet úgy, hogy az ESP32-t nem engedi át** (más TLS-ujjlenyomat, mint a curl-é).
  Ez a projekt fő kockázata.
- A `usage_parser` és a hitelesítés végleges alakja a valós claude.ai-mintára vár.
- Nem hivatalos API: a Claude bármikor megváltoztathatja. A javítás helye a `claude_client` és a `usage_parser`.
- Rejtett (nem sugárzott) SSID nem támogatott.
- A tanúsítványlánc gyökere változhat (Cloudflare kiadót válthat) → új gyökér a `src/ca_certs.h`-ba.
- `time_t` 32 bites (Arduino-ESP32 2.0.17) → 2038-ig.
- Nincs OTA-frissítés: firmware csak USB-n.
