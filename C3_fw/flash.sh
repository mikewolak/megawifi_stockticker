#!/usr/bin/env bash
# flash.sh — Flash the MegaWiFi ESP32-C3 HTTPS firmware onto the cart.
#
# Usage:
#   ./C3_fw/flash.sh                         # use defaults
#   PORT=/dev/cu.usbmodem101 ./C3_fw/flash.sh
#   PORT=/dev/cu.usbmodem101 BAUD=921600 ./C3_fw/flash.sh
#
# Defaults: PORT=/dev/ttyUSB0 (Linux) or /dev/cu.usbmodem101 (macOS)
#           BAUD=921600
#
# Requires: esptool.py in PATH  (pip install esptool)

set -euo pipefail

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-921600}"
CHIP="esp32c3"

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

boot="${here}/bootloader.bin"
part="${here}/partition-table.bin"
ota="${here}/ota_data_initial.bin"
app="${here}/mw-fw-rtos.bin"

for f in "$boot" "$part" "$ota" "$app"; do
    [[ -f "$f" ]] || { echo "ERROR: missing $(basename "$f") in $(dirname "$f")"; exit 1; }
done

command -v esptool.py >/dev/null 2>&1 || {
    echo "ERROR: esptool.py not found. Install with: pip install esptool"
    exit 1
}

echo "Flashing MegaWiFi ESP32-C3 HTTPS firmware"
echo "  Port : $PORT"
echo "  Baud : $BAUD"
echo "  App  : $(basename "$app")  ($(du -h "$app" | cut -f1))"
echo ""

esptool.py \
    --chip  "$CHIP" \
    --port  "$PORT" \
    --baud  "$BAUD" \
    --before default_reset \
    --after  hard_reset \
    write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
        0x00000 "$boot" \
        0x08000 "$part" \
        0x0D000 "$ota"  \
        0x10000 "$app"

echo ""
echo "Done. Power-cycle the cart before running the ROM."
