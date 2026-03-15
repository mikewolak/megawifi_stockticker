# Flash Notes

## Flash the HTTPS firmware (required once per cart)

```sh
PORT=/dev/cu.usbmodem101 ./C3_fw/flash.sh
```

Flashes all four binary artifacts from `C3_fw/` at the correct addresses.
Power-cycle the cart after flashing.

## Firmware binary details

| File | Address | Description |
|------|---------|-------------|
| `bootloader.bin` | 0x00000 | ESP32-C3 second-stage bootloader |
| `partition-table.bin` | 0x08000 | Custom partition table (1.25 MB OTA slots) |
| `ota_data_initial.bin` | 0x0D000 | OTA data partition initial state |
| `mw-fw-rtos.bin` | 0x10000 | MegaWiFi firmware with HTTPS + Mozilla CA bundle (~1.1 MB) |

## Verify the firmware is the HTTPS build

A correct HTTPS-capable build is **≥ 1.1 MB**. An lwIP-only build is **≤ 870 KB**
and will produce `url err=1` when the ROM tries to fetch quotes.

## Rollback to stock v1.5.1 firmware

```sh
PORT=/dev/cu.usbmodem101 ./restore_orig_fw.sh
```

Requires the v1.5.1 release files at
`$HOME/MegaWifi/mw-fw-rtos/release/esp32c3_v1.5.1/`.

## Rebuild firmware from source

See `$HOME/MegaWifi/mw-fw-rtos/` and https://github.com/mikewolak/mw_fw_rtos_c3
for the full build procedure. Critical sdkconfig settings are preserved in
`sdkconfig.defaults` in that repo.
