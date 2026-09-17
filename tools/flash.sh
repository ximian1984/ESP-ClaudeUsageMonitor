#!/bin/sh
# ClaudeUsageMonitor — feltoltes PlatformIO NELKUL (pl. host), a mashol leforditott binarisokbol.
# A parameterek a `pio run -t envdump` UPLOADERFLAGS-abol (2026-09-17): esp32s3, 921600, dio, 80m, 16MB,
# 0x0 bootloader / 0x8000 partitions / 0xe000 boot_app0 / 0x10000 firmware. (qio -> dio: platform main.py _get_board_flash_mode)
# Hasznalat:  sh flash.sh                 -> kiirja a soros portokat
#             sh flash.sh /dev/cu.usbmodemXXXX
set -e
cd "$(dirname "$0")"
ESPTOOL=./venv/bin/esptool.py
if [ -z "$1" ]; then
  echo "Soros portok most:"; ls /dev/cu.* 2>/dev/null
  echo; echo "Add meg a dongle portjat: sh flash.sh /dev/cu.usbmodemXXXX"; exit 1
fi
shasum -a 256 -c SHA256SUMS
"$ESPTOOL" --chip esp32s3 --port "$1" --baud 921600 --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
