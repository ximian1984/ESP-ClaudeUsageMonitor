#!/bin/sh
# Gepi (host) teszt a hardverfuggetlen fuggvenyekre. Futtatas: sh test/host/run.sh
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/cmon_test_time"
g++ -std=c++17 -Wall -Istub -I../../src test_time.cpp ../../src/time_manager.cpp -o "$OUT"
"$OUT"
