# Build Notes (stable toolchain setup)

Absolute paths (copy/paste):
- Toolchain prefix: `/Users/MWOLAK/homebrew/Cellar/rosco-m68k-toolchain@13/20241103161658/bin/m68k-elf-rosco-`
- SGDK root: `/Users/MWOLAK/sgdk`
- Working NTP binary (current): `/Users/MWOLAK/MegaWifi/megawifi_ntp/out/megawifi_ntp.bin`
- Backup NTP binary (also good): `/Users/MWOLAK/MegaWifi/megawifi_ntp_backup_20260313/out/megawifi_ntp.bin`
- ESP-IDF root: `/Users/MWOLAK/esp/esp-idf`
- Project root: `/Users/MWOLAK/MegaWifi/stock_ticker`

These notes capture the exact combination that produced a **booting** MegaWiFi build after recent breakage. Use this as the repeatable baseline.

## Toolchain
- 68k cross: `/Users/MWOLAK/homebrew/Cellar/rosco-m68k-toolchain@13/20241103161658/bin/m68k-elf-rosco-`
- SGDK: `/Users/MWOLAK/sgdk` (the local SGDK install you already have; do **not** swap in marsdev or other SDKs for this project).

## Environment
```sh
export PREFIX=/Users/MWOLAK/homebrew/Cellar/rosco-m68k-toolchain@13/20241103161658/bin/m68k-elf-rosco-
export GDK=$HOME/sgdk
```

## Rebuild (genesis side)
From this repository root:
```sh
PREFIX=$PREFIX GDK=$GDK make clean all
```

## Known-good firmware & API pairing
- Keep using the MegaWiFi API that is already vendored in your tree; do **not** replace it with experimental gitlab/mars versions.
- Verified-good NTP binary (current): `/Users/MWOLAK/MegaWifi/megawifi_ntp/out/megawifi_ntp.bin` (≈81 KB).
- Backup reference binary: `/Users/MWOLAK/MegaWifi/megawifi_ntp_backup_20260313/out/megawifi_ntp.bin` (≈81 KB) if you need to compare sizes/behavior.

## Things that break the build/firmware
- Switching to the marsdev toolchain or swapping SGDK versions.
- Replacing mw-api with other branches or external zips.
- Building without `PREFIX`/`GDK` set exactly as above.

## Flashing C3 firmware (reminder)
- Use the ESP-IDF v5.5.1 environment already installed in `~/esp/esp-idf`.
- Typical command: `IDF_COMPONENT_MANAGER=0 . "$HOME/esp/esp-idf/export.sh" >/dev/null && idf.py -p /dev/cu.usbmodem2101 flash`

## Quick checklist before changing anything
1. Confirm `PREFIX` and `GDK` env vars match above.
2. Avoid touching mw-api or SGDK versions.
3. Keep the working NTP binary as a sanity check.
4. Rebuild with `make clean all` and compare binary size (~81 KB).
5. If firmware boots to MegaWiFi loader and LED turns on, you're on the right track.
