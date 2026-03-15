# MegaWiFi ESP32-C3 Firmware (mw-fw-rtos)

This folder holds the ESP32-C3 firmware artifacts and helper scripts for the MegaWiFi cart.

## Files
- `mw_fw_rtos-v1.5.1.tar.xz` – original upstream package (ESP8266 image, kept for reference).
- `mw-fw-rtos.bin` – current ESP32-C3 app image (rebuilt locally).
- `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` – matching bootloader/partitions.
- `flash_c3.sh` – flash the already-built images in this folder to the cart.
- `flash_c3_build.sh` – rebuild firmware from `/tmp/mw/src/mw-fw-rtos` using your local ESP-IDF, copy artifacts here, then flash.

## Flashing an existing build
```bash
# defaults: PORT=/dev/ttyUSB0 BAUD=921600
PORT=/dev/cu.usbmodem101 ./flash_c3.sh
```
Requires `esptool.py` in PATH.

## Rebuild then flash (local ESP-IDF)
```bash
PORT=/dev/cu.usbmodem101 BAUD=921600 ./flash_c3_build.sh
```
Assumes:
- ESP-IDF installed at `~/esp/esp-idf`
- Source checked out at `/tmp/mw/src/mw-fw-rtos` (from gitlab.com/doragasu/mw, with submodules).

The script:
1) exports ESP-IDF env,
2) builds target `esp32c3`,
3) copies new artifacts here,
4) flashes them to the cart with `esptool.py`.

After flashing, power-cycle the cart.
