#!/usr/bin/env bash
# Flash MegaWiFi ESP32-C3 firmware v1.5.1 (HTTPS-capable) onto the cart.
# Usage:
#   PORT=/dev/cu.usbserial-XXXX BAUD=921600 ./flash_c3.sh
# Defaults: PORT=/dev/ttyUSB0 (Linux), BAUD=921600.
# Requires: esptool.py in PATH.

set -euo pipefail

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-921600}"
CHIP="esp32c3"

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

boot="${here}/bootloader/bootloader.bin"
# prefer freshly built partition-table.bin but fall back to partitions.bin if present
part="${here}/partition-table.bin"
[[ -f "${here}/partitions.bin" ]] && part="${here}/partitions.bin"
ota="${here}/ota_data_initial.bin"
app="${here}/mw-fw-rtos.bin"

for f in "$boot" "$part" "$ota" "$app"; do
  [[ -f "$f" ]] || { echo "Missing $f"; exit 1; }
done

command -v esptool.py >/dev/null 2>&1 || {
  echo "esptool.py not found in PATH. Install via pip: pip install esptool"
  exit 1
}

echo "Flashing MegaWiFi firmware (ESP32-C3)..."
echo "  PORT: $PORT"
echo "  BAUD: $BAUD"

esptool.py \
  --chip "$CHIP" \
  --port "$PORT" \
  --baud "$BAUD" \
  --before default_reset \
  --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
    0x0000 "$boot" \
    0x8000 "$part" \
    0xD000 "$ota" \
    0x10000 "$app"

echo "Flash complete. Power-cycle the cart."
