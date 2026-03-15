#!/usr/bin/env bash
set -euo pipefail
PORT="${PORT:-/dev/cu.usbmodem101}"
BAUD="${BAUD:-921600}"
D="/Users/MWOLAK/MegaWifi/mw-fw-rtos/release/esp32c3_v1.5.1"
esptool.py --chip esp32c3 --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0000 "$D/bootloader.bin" 0x8000 "$D/partition-table.bin" 0xD000 "$D/ota_data_initial.bin" 0x10000 "$D/mw-fw-rtos.bin"
echo "Done. Power-cycle the cart."
