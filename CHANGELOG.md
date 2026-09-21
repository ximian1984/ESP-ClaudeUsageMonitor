# Changelog

A [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) formátumot követi. A firmware-verzió (`FW_VERSION`) még
`0.1.0`: az alábbi dátumozott bejegyzések a fejlesztés menetét rögzítik, kiadás még nem volt.

**Olvasási kulcs:** `[vason mérve]` = a dongle-on futott és megnéztük; `[forrásból]` = hivatalos kód/doksi alapján,
hardveren még nem igazolt. A méréseket a belső tervdokumentáció részletezi.

---

## [Nem kiadott] – 2026-09-21

### Mérve
- **A CYD először futott valódi lapon** (USB-C + micro-USB-s lap, ESP32-D0WD-V3 rev 3.1, 4 MB, CH340). `[vason mérve]`:
  - driver: `ILI9341_2_DRIVER` jó; háttérfény: GPIO21, aktív HIGH, teljes fényerő;
  - invertálás: a sima `esp32-2432s028r` env-vel fehér háttér + kék (cián) felirat → `-DTFT_INVERSION_ON=1` kell;
    a színsorrend jó (nincs RGB/BGR-csere);
  - forgatás: a fekvő (0°) és a **90°-os álló** kép is jól olvasható; a 180/270° a CYD-n nincs megnézve;
  - **TLS-heap:** induláskor 237 424 B szabad, legnagyobb blokk 110 580 B; az **első HTTPS-hívás `HTTP 200`**, a usage
    feldolgozva (3 limit); utána 153 780 B szabad, mélypont 94 540 B. A klasszikus ESP32-nek elég.
- Zsákutca: a „két csatlakozó = ST7789" szabály (közösségi forrásokból) erre a lapra nem állt; ILI9341 volt.

### Hozzáadva
- `esp32-2432s028r-inv` env: ILI9341 + invertálás, a fenti lapra.
- **CYD setup-AP: Wi-Fi QR-kód** (`WIFI:T:WPA;…`) a szöveg mellett, hogy a telefon gépelés nélkül csatlakozzon
  (projektgazda). Nayuki `qrcodegen` (MIT, `src/vendor/`). Csak akkor rajzolja ki, ha legalább 3 px/modullal befér.
  A gépi renderről `zbarimg` bájtra pontosan visszaolvassa; telefonnal a valódi panelről ⚠ még nem mért.
- **CYD setup-AP: visszaszámláló** az újrapróbálásig (`retrying Wi-Fi in 4:32` / `searching Wi-Fi...` /
  `retry waits: phone on AP`) (projektgazda). `WifiManager::apRetryInMs()`, `apHasClients()`.
- **90°/270°-os forgatás (álló kép) a CYD-n** (projektgazda): a setup-oldalon választó 0/90/180/270°-kal (a dongle-on
  csak 0/180°). Minden CYD-képernyőnek van 240×320-as elrendezése; a sáv-sprite forgatáskor 240×40-esre jön létre újra.
  A host-renderer mindkét tájolást kirajzolja (`allo_*.png`).

### Módosítva
- **Keret-csíkok új színezése** (projektgazda), a dongle-on és a CYD-n is. Régi/lejárt adatnál szürke, mint a címke:
  - **SESSION:** a **fogyást** mutatja, balról tölt, színátmenettel: a bal széle zöld, a jobb széle (100 % elfogyott) piros;
  - **WEEKLY** (és a többi keret): a **maradékot** mutatja, egyetlen tónussal, ami a fogyással folyamatosan vált:
    tele zöld, 50 %-nál sárga, elfogyva piros (ugyanaz, mint a címke).
  - Eddig mindkettő a maradékot mutatta, és 30 % maradékig tiszta zöld volt, ezért nem sárgult.
  - Közbülső próbálkozások (elvetve): mindkét csík egy tónussal; majd mindkettő helyzet szerinti átmenettel. A
    projektgazda a két keretet eltérően olvassa: a session a fogyás, a heti egy fogyó érték.
- **A kijelzett szám a fogyás** („69 % used"), a maradék helyett, mindkét lapon (projektgazda): így egyezik a
  claude.ai Usage oldalával. Előtte a projektgazda a „31 % left"-et fogyásnak olvasta; az adat jó volt (claude.ai:
  session 7 % / heti 69 % used = kijelző 93 % / 31 % left). A szám színe továbbra is a maradékból jön.
- Konfig: `displayFlip` (bool) → `displayRot` (negyedfordulat, NVS `drot`). A régi `flip` kulcs és a régi mentés
  `displayFlip` mezője 180°-ként töltődik be; a `flip` a 180°-hoz továbbra is íródik (visszaálláshoz). Az API
  (`/api/config`, `/api/display`, export/import) a `displayRot`-ot is adja/fogadja, a régi `flip`/`displayFlip`-et is.

## [Nem kiadott] – 2026-09-19 (este)

### Módosítva
- **CYD: natív 320×240-es elrendezés** a 2×-es nagyítás helyett (projektgazda). Nagy szám a maradék %-ra, sáv, és
  **nagy betűs visszaszámlálás** a reset dátumával. Fejléc: név + állapot, lábléc: setup-cím + profil-sorszám.
  Teljes képernyős puffer nincs: egyetlen 320×40-es (25,6 KB) sáv-sprite rajzol hat menetben. A dongle kijelző-kódja
  változatlan. `[forrásból]` + gépi render (`test/host/render_cyd.sh`), **vason nem futott**.
- **Webes tükör a CYD-n: a teljes 320×240-es kép**, sávonként újrarajzolva és küldve (~150 KB/kép). A lap a méretet
  az `X-Screen-Size` fejlécből veszi, a dongle-on marad a 160×80.

### Hozzáadva
- `/api/status` → `maxAllocHeap` (legnagyobb szabad heap-blokk), mindkét lapon. A CYD percenként naplózza a képidőt és
  a heap-számokat is (a vason mérendő TLS-tartalékhoz).
- `test/host/render_cyd.sh`: a CYD-elrendezés PNG-be a Mac-en, a valódi rajzoló kóddal és a TFT_eSPI font-tábláival.
  A panelt nem helyettesíti.

## [Nem kiadott] – 2026-09-19

### Hozzáadva
- **Második lapka: ESP32-2432S028R („Cheap Yellow Display", CYD)**, két új PlatformIO env-vel:
  `esp32-2432s028r` (ILI9341, az eredeti micro-USB-s lap) és `esp32-2432s028r-st7789` (a kétportos „CYD2USB"/„Rv3").
  A Wi-Fi/HTTPS/OAuth/usage kód változatlan. A 160×80-as kép 2×-es nagyításban, a 320×240-es panel közepén jelenik meg.
  A webes tükör ugyanaz. Partíciók: `min_spiffs.csv` (4 MB flash). Mindkét env tisztán fordul, 0 warning.
  `[forrásból]`: **fizikai CYD-n nem futott.** A driver, a színsorrend, az invertálás és a forgatás `[vason mérendő]`
  (README 2/b.).

### Módosítva
- `config.h`: a lapkafüggő állandók (`BOARD_NAME`, háttérfény-pin és -szint, `DISPLAY_SCALE`) a `BOARD_CYD` flag
  szerint választódnak. A dongle-build kódja **bájtra azonos** a korábbival (`cmp`: csak az ELF-SHA és a kép-ellenőrzőösszeg tér el).

## [Nem kiadott] – 2026-09-18

### Hozzáadva
- **Webes kijelző-tükör** (`/screen`, külön ablakban is): a dongle a **valódi framebuffert** adja ki
  (`GET /api/screen`, 160×80 RGB565, 25 600 B), a böngésző másodpercenként, felnagyítva rajzolja. Belépés kell hozzá;
  token nélkül `401`, az oldal ilyenkor jelszót kér. `[vason mérve]`
- **Kijelző 180°-os forgatása** a setup-oldalról (NVS `flip`), futásidőben érvényes. A rajzoló kód változatlan, csak a
  kép kiírása fordul. A ST7735-ofszet forgatott helyessége `[vason mérendő]`.
- **Sáv a heti kerethez is**: az elrendezés 2 pixellel feljebb tolva, a 80 pixeles kijelzőn mindkét sáv kifér. `[vason mérve]`

### Módosítva
- **A keret-címke színe a maradék szerint** megy zöldből sárgán át pirosba (`usageColor565`, host-teszttel).
  Régi vagy lejárt adatnál szürke. `[vason mérve]`

### Javítva
- A kijelző-tükör először fordított színekkel jött: a TFT_eSPI sprite **bájtcserélt** RGB565-öt tárol; a kliens fordítja vissza.

## [Nem kiadott] – 2026-09-17 (este)

### Hozzáadva
- **További AI-szolgáltatók**: **ChatGPT** (Codex-keret, OpenAI eszközkódos login), **Gemini** (Code Assist kvóta,
  Google-bejelentkezés kódmásolással), **Grok** (Grok CLI credit, RFC 8628 eszközkód). Új modulok: `provider_auth`,
  `https_util`, `token_cache`, `jwt_util`; parserek és a setup-oldali kezelőfelület.
  Fordul, host-teszt zöld, **vason még nem futott** `[forrásból]`.
- **GTS Root R1** a tanúsítványcsomagba (googleapis.com); mind a 9 használt hoszt `0 (ok)`. `[mérve]`
- Az új szolgáltatóknál az access token **csak RAM-ban** (`token_cache`), az NVS-ben a refresh token és az
  account-/project-azonosító. Indulás után egy refresh pótolja.
- Kutatás és forrás-ellenőrzés a belső tervdokumentációban: 2.13 (terv), 2.13/b (forrás + mérés hamis tokennel), 2.12 (USB tethering),
  2.14 (MikroTik USB-porton át távoli flash — félretéve).

### Javítva
- `DeviceConfig` a heapre (`heapSnapshot`): a loopTask legkisebb szabad stackje **1580 → 12 508 B**. `[vason mérve]`

## [Nem kiadott] – 2026-09-17 (délután)

### Hozzáadva
- **Beállítások exportja/importja.** Titok nélkül olvasható JSON; *include secrets* esetén a fájlt a **dongle titkosítja**
  az admin-jelszóval (PBKDF2-HMAC-SHA256 25 000 kör → AES-256-GCM). Importnál az exportkori jelszó kell. Dongle nélküli
  visszafejtő: `tools/decrypt_backup.py`. `[vason mérve]` (PBKDF2 2258 ms; független Python-visszafejtés OK)
- **Playwright E2E-tesztek** (`test/e2e/`): setup-felület 25/25, mentés-visszatöltés 18/18. `[vason mérve]`
- **Lezárt setup-oldal**: admin-jelszó esetén az **olvasás is** belépéshez kötött; a `/` egy semleges login-héj, a felület
  a `/api/ui`-ról jön, a zárolt `/api/status` semmit nem árul el az eszközről. `[vason mérve]`
- **IP-cím a kijelző felső sorában**, 3 másodpercenként váltakozva az állapottal.
- **Utolsó ismert reset-időpontok NVS-ben**: Wi-Fi nélkül és újraindítás után is látszanak.
- **Állítható időzóna** (POSIX TZ, lista + egyedi), a reset-idők ebben jelennek meg. `[vason mérve]` (JST, +04, IST, EDT)

### Módosítva
- A reset-sor mindig dátumot is mutat, a visszaszámláló másodpercre jár: `RST 02:03:46 @09.18 12:30`.
- 20 Wi-Fi-profil (5 helyett), üres profilnévnél automatikus `Profile-XX`.
- A setup-oldal mobilbarát: *Authenticate now* gomb, görgetés közben is látszó üzenetsor, oldalra görgethető táblázatok.

### Javítva
- **Transport-tábla sorrendje** nem egyezett az enummal: az „oauth" forrás web-sessionnek számított
  (`sessionKey value required`), és a Claude-hívás rossz hosztra ment volna. Most azonosító szerint keres.
- A profil-cache **nem nullázódik** token-frissítéskor (az azonosító már nem az access tokent hasheli).
- **Rossz Wi-Fi-jelszó**: a bontási okból 5,3 s alatt felismeri, és lép a következő profilra (nem vár 15 s-ot).
- Wi-Fi-profil mentése nem bontja a nem érintett kapcsolatot; a setup-oldal újrapróbál, ha az eszköz épp újracsatlakozik.

## [Nem kiadott] – 2026-09-17 (délelőtt, első vas)

### Hozzáadva
- **Első sikeres flash és futás vason** (T-Dongle-S3): port `303A:1001`, 16 MB flash, PSRAM nincs.
- `tools/flash.sh` (PlatformIO nélküli feltöltés), `tools/serlog.py` (soros napló, újracsatlakozással).
- **Claude OAuth-login és usage-lekérés vason**: `HTTP 200`, 3 limit a kijelzőn; a tokenek újraindítás után is megvannak.
  `[vason mérve]`

### Javítva
- **Üres kijelző**: a TFT_eSPI ESP32-S3-on 0-s SPI-portot választ, aminek a regisztercíme 0 → a `tft.init()` a 0-s címre
  írt. Megoldás: `-DUSE_FSPI_PORT=1`. A fordítás ezt nem jelezte.
- **Boot-loop mentett Wi-Fi-profillal**: a `loopTask` 8 KB-os stackje kevés a beállítás-másolathoz → 16 KB.
- **A Wi-Fi-keresés nem adott eredményt**: a keresés 6,4–6,8 s, az Arduino-core 6 s után hibának veszi. Saját, 20 s-os korlát.
- A build 0 figyelmeztetéssel fordul (`-DDISABLE_ALL_LIBRARY_WARNINGS`), a saját forrásokra `-Wall -Wextra` mellett is.

## [Nem kiadott] – 2026-09-16 (alapok, hardver nélkül)

### Hozzáadva
- Mért alapok: kijelző-pinout a hivatalos LilyGO forrásból, endpoint-hibaválaszok, TLS-láncok; fordítható PlatformIO-váz.
- NVS-konfiguráció, Wi-Fi-állapotgép AP-fallbackkel, setup-webszerver, NTP + időkezelés, kijelző-elrendezés,
  Claude-kliens (TLS + CA), ütemező, profilonkénti cache.
- `usage_parser` a **valós** Claude-válaszra (`limits[]` + tartalék), host-tesztekkel és kapuval.
- **OAuth on-device login (PKCE) + automatikus tokenfrissítés**, dedikált eszköz-tokennel.
- Opcionális **admin-jelszó** (PBKDF2-HMAC-SHA256, memóriában tartott token, kizárás hibás próbálkozásoknál).

### Megjegyzés
- A Cloudflare-kockázat a másodlagos `sessionKey` (claude.ai) útra szűkítve: az elsődleges OAuth-út hosztjai nem adnak kihívást.
