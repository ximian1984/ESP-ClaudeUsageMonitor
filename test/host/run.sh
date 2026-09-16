#!/bin/sh
# Gepi (host) teszt a hardverfuggetlen fuggvenyekre. Futtatas: sh test/host/run.sh
# Az ArduinoJson a PlatformIO libdeps-bol jon, ezert elobb egy `pio run` kell.
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/cmon_host"
AJ=../../.pio/libdeps/t-dongle-s3/ArduinoJson/src
[ -d "$AJ" ] || { echo "hianyzik $AJ — futtasd elobb: pio run"; exit 1; }
CXX="g++ -std=c++17 -Wall -Wextra -Istub -I../../src -I$AJ"
$CXX test_time.cpp ../../src/time_manager.cpp -o "$OUT.time"
"$OUT.time"
$CXX test_parser.cpp ../../src/usage_parser.cpp ../../src/usage_types.cpp ../../src/time_manager.cpp -o "$OUT.parser"
"$OUT.parser"
$CXX -DUSAGE_PARSER_ENABLE=0 -DEXPECT_GATE_CLOSED=1 test_parser.cpp ../../src/usage_parser.cpp ../../src/usage_types.cpp ../../src/time_manager.cpp -o "$OUT.parser_off"
"$OUT.parser_off"
