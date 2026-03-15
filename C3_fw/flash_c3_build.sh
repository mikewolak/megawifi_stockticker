#!/usr/bin/env bash
# Build mw-fw-rtos for ESP32-C3 with local ESP-IDF and flash to the MegaWiFi cart.
# Usage:
#   PORT=/dev/cu.usbmodem101 BAUD=921600 ./flash_c3_build.sh
# Defaults: PORT=/dev/ttyUSB0, BAUD=921600.
# Requires: ESP-IDF installed at ~/esp/esp-idf and esptool.py in PATH.

set -euo pipefail

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-921600}"
CHIP="esp32c3"

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
src_root="$HOME/MegaWifi/mw-fw-rtos"

if [ ! -d "$src_root" ]; then
  echo "Expected source at $src_root"
  echo "Clone it with: cd ~/MegaWifi && git clone https://gitlab.com/doragasu/mw mw-tmp && cp -r mw-tmp/src/mw-fw-rtos mw-fw-rtos && rm -rf mw-tmp"
  exit 1
fi

echo "[1/4] Exporting ESP-IDF..."
source ~/esp/esp-idf/export.sh >/dev/null

echo "[2/4] Building firmware for ESP32-C3..."
cd "$src_root"
IDF_COMPONENT_MANAGER=0 idf.py build

echo "[3/4] Copying artifacts into $(basename "$here")..."
cp build/mw-fw-rtos.bin "$here"/mw-fw-rtos.bin
cp build/bootloader/bootloader.bin "$here"/bootloader.bin
cp build/partition_table/partition-table.bin "$here"/partition-table.bin
cp build/ota_data_initial.bin "$here"/ota_data_initial.bin

echo "[4/4] Flashing to $PORT ..."
esptool.py \
  --chip "$CHIP" \
  --port "$PORT" \
  --baud "$BAUD" \
  --before default_reset \
  --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
    0x0000  "$here/bootloader.bin" \
    0x8000  "$here/partition-table.bin" \
    0xD000  "$here/ota_data_initial.bin" \
    0x10000 "$here/mw-fw-rtos.bin"

echo "Done. Power-cycle the cart."
