# Third-party code — QR Code generator (Nayuki)

| | |
|---|---|
| What | `qrcodegen.c`, `qrcodegen.h` |
| Author | Project Nayuki, <https://www.nayuki.io/page/qr-code-generator-library> |
| License | **MIT** (the notice is in the head of both files) |
| Imported | 2026-09-21, byte-identical to the copy already used and verified in the sibling WifiSmartScreen project |
| SHA-256 (`qrcodegen.c`) | `6a2b9cc65176f2345dde260c74b6d352627e8a0a6385d086ae0e9c5d0913c70c` |
| SHA-256 (`qrcodegen.h`) | `e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589` |

## Why it is here

The CYD's setup-AP screen shows a Wi-Fi QR code (`WIFI:T:WPA;S:…;P:…;;`) so a phone can join the setup network
without typing (`src/display_cyd.cpp`, `prepareApQr` / `drawQr`).

Why not the ESP-IDF `esp_qrcode` that ships with the Arduino core: it is only a prebuilt `libqrcode.a` there, so the
host renderer (`test/host/render_cyd.sh`) could not compile it on the Mac, and the rendered QR could not be checked.
This one is plain ANSI C and builds on both.

## How it is verified

Not by trusting the source, by measurement: `zbarimg` decodes the QR from the host render byte-exact, in both
orientations (2026-09-21). ⚠ A phone scan from the physical panel is not yet measured.

It is C, not C++: the host script compiles it with `cc`, PlatformIO compiles `.c` files as C on its own.
