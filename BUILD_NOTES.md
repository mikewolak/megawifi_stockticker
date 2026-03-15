# Build Notes

Repeatable build setup for the MegaWiFi Stock Ticker Genesis ROM.

## Toolchain

| Component | Location |
|-----------|----------|
| m68k-elf-gcc | `$HOME/homebrew/Cellar/rosco-m68k-toolchain@13/.../bin/` (in PATH) |
| SGDK | `$HOME/sgdk` |
| ESP-IDF | `$HOME/esp/esp-idf` (v5.x, for firmware rebuilds only) |

The Makefile uses `m68k-elf-gcc` from PATH and `GDK=$HOME/sgdk` by default.
No environment variables need to be set for a normal ROM build.

## Build the ROM

```sh
# First time: create your token file
cp TOKEN.md.example TOKEN.md
# Edit TOKEN.md and fill in your Finnhub API key

make
```

Output: `out/stock_ticker.bin`

The build number auto-increments each compile (`out/build_num`) and is shown
in the ROM title bar.

## Clean build

```sh
make clean && make
```

## Things that break the build

- Swapping the SGDK version or switching to a different SDK (marsdev, etc.)
- Replacing `src/ext/mw` with a different mw-api branch
- Running `idf.py set-target esp32c3` in the firmware source tree — this wipes
  `sdkconfig` and loses `CONFIG_MW_HTTP_USE_ESP=y` (see firmware repo README)

## Flash the ROM

Copy `out/stock_ticker.bin` to your flash cart (Everdrive, Mega EverDrive, etc.)
and boot the console.

## Flash the ESP32-C3 firmware (required once)

```sh
PORT=/dev/cu.usbmodem101 ./C3_fw/flash.sh
```

See `C3_fw/` and the firmware repo for details:
https://github.com/mikewolak/mw_fw_rtos_c3
