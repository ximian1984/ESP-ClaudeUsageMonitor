#!/bin/sh
# A CYD nativ 320x240-es elrendezese PNG-be, gepen (host), a valodi src/display_cyd.cpp-vel. Vas nem kell hozza;
# a panel (driver, szinek) nem latszik benne, csak az elrendezes geometriaja. Futtatas: sh test/host/render_cyd.sh [mappa]
# A TFT_eSPI font-tablait a PlatformIO letoltesebol veszi, ezert elobb egy `pio run -e esp32-2432s028r` kell.
set -e
cd "$(dirname "$0")"
OUTDIR="${1:-${TMPDIR:-/tmp}/cmon_cyd_render}"
mkdir -p "$OUTDIR"
OUT="${TMPDIR:-/tmp}/cmon_render_cyd"
TFT=../../.pio/libdeps/esp32-2432s028r/TFT_eSPI
[ -d "$TFT/Fonts" ] || { echo "hianyzik $TFT — futtasd elobb: pio run -e esp32-2432s028r"; exit 1; }
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -DBOARD_CYD=1 -Irender_stub -Istub -I../../src -I"$TFT" \
  render_cyd.cpp ../../src/display_cyd.cpp ../../src/time_manager.cpp ../../src/usage_types.cpp -lz -o "$OUT"
"$OUT" "$OUTDIR"
