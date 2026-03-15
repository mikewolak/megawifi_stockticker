# Firmware Flash Notes

- Current flashed image: `/Users/MWOLAK/MegaWifi/stock_ticker/C3_fw/mw-fw-rtos.bin` (flashed via `flash_c3.sh`).
- Flash command used (2026-03-14): `PORT=/dev/cu.usbmodem2101 ./flash_c3.sh` (baud 921600). esptool output showed full erase/write and hash verification for bootloader, partition table, ota_data_initial, and app.
- We inspected `mw-fw-rtos.map` in `/tmp/mw/src/mw-fw-rtos/build` and confirmed `esp_http_client` is linked (HTTPS path present).
- Note: earlier build attempts were blocked by macOS psutil/SIP during CMake; flashing succeeded by running the provided script from `C3_fw` without rerunning CMake.
- Lesson learned: when the script `C3_fw/flash_c3.sh` exists, use it first; avoid assuming the firmware is outdated if the script has been run and hashes verify.
