# MegaWiFi Stock Ticker

A live stock ticker for the **Sega Genesis / Mega Drive** running on a
[MegaWiFi](https://gitlab.com/doragasu/mw) ESP32-C3 cartridge. Fetches real-time
quotes from the [Finnhub](https://finnhub.io) HTTPS API and displays them on the
Genesis VDP — no PC required after the cartridge is flashed.

---

## Features

- **Live quotes** for 6 tickers: ORCL, NVDA, AMZN, IBM, MSFT, GOOGL
- **HTTPS** — TLS handled entirely by the ESP32-C3 firmware using the Mozilla CA bundle (no custom certificate needed)
- **Color-coded delta**: green for gains (PAL1), red for losses (PAL3); company logo colors for ORCL (red) and NVDA (green)
- **Timestamp** per quote showing the CST time the price was fetched (12-hour format)
- **Scrolling marquee** on the background plane with all tickers and prices
- **Countdown timer** to the next API fetch cycle
- **Throttled fetches**: one ticker every 10 seconds (6 calls/min — within Finnhub's 60/min free tier limit)
- **NTP time sync** via `pool.ntp.org`, US Central timezone (`CST6CDT`) with automatic DST

---

## Display Layout

```
[ MegaWiFi Stock Ticker v1.5 MegaWiFi ]
Next Update: 8s
ORCL  $155.23   -1.25(-0.80%)  2:34 PM
NVDA  $900.44   +1.56(+0.17%)  2:34 PM
AMZN  $207.46   -2.01(-0.96%)  2:33 PM
IBM   $255.10   +0.88(+0.35%)  2:33 PM
MSFT  $475.55   -3.21(-0.67%)  2:32 PM
GOOGL $175.22   -1.43(-0.81%)  2:32 PM
... scrolling marquee ...
```

Column layout (40-char screen):
- Col 1–5: Symbol (company color)
- Col 6–15: Price (white, fixed-width)
- Col 16–29: Delta and % change (green/red, fixed-width)
- Col 30–37: Fetch timestamp in CST (white)

---

## Hardware

| Component | Details |
|-----------|---------|
| Console | Sega Genesis / Mega Drive (NTSC or PAL) |
| Cartridge | MegaWiFi ESP32-C3 cart |
| Firmware | Custom build — `C3_fw/mw-fw-rtos.bin` (see below) |

### ESP32-C3 Firmware

The stock firmware shipped with the cart (`v1.5.1`) uses a plain-HTTP lwIP socket
client and **cannot do HTTPS**. This repo includes a custom firmware build in
`C3_fw/` with the following changes:

- `CONFIG_MW_HTTP_USE_ESP=y` — routes HTTP through `esp_http_client` (TLS-capable)
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` — embeds the full Mozilla CA
  bundle; required because `finnhub.io` uses a GTS R4 intermediate not present in
  the common-only bundle

The firmware source with full documentation is at:
[github.com/mikewolak/mw_fw_rtos_c3](https://github.com/mikewolak/mw_fw_rtos_c3)

---

## Building the ROM

### Prerequisites

| Tool | Location |
|------|----------|
| SGDK | `~/sgdk` (with `libmd.a`, `inc/`, `src/`) |
| m68k-elf-gcc | in `PATH` (rosco m68k toolchain v13) |
| Finnhub API token | free account at [finnhub.io](https://finnhub.io) |

### Build

```sh
make FINNHUB_TOKEN=your_token_here
```

Output: `out/stock_ticker.bin`

The build number auto-increments each compile and is displayed in the title bar.

### Run in MAME (optional)

```sh
make run FINNHUB_TOKEN=your_token_here
```

---

## Flashing

### Flash the custom ESP32-C3 firmware (required once)

```sh
PORT=/dev/cu.usbmodem101 ./C3_fw/flash_c3.sh
```

This flashes `C3_fw/mw-fw-rtos.bin` (the pre-built HTTPS-capable firmware).
Power-cycle the cart after flashing.

To rebuild the firmware from source and flash in one step:

```sh
PORT=/dev/cu.usbmodem101 BAUD=921600 ./C3_fw/flash_c3_build.sh
```

> **Warning**: The build script intentionally omits `idf.py set-target`. Running
> `set-target` triggers a full clean that resets `sdkconfig` and loses
> `CONFIG_MW_HTTP_USE_ESP=y`, producing a smaller (~849 KB) build that cannot do
> HTTPS. If this happens accidentally, restore with `cp sdkconfig.old sdkconfig`.

### Flash the stock firmware (rollback)

```sh
PORT=/dev/cu.usbmodem101 ./restore_orig_fw.sh
```

### Load the ROM

Copy `out/stock_ticker.bin` to your flash cart (Everdrive, Mega EverDrive, etc.)
and boot the console normally.

---

## Configuration

Key defines in `src/stock_ticker.c`:

| Define | Default | Description |
|--------|---------|-------------|
| `AP_SSID` | `"YourSSID"` | WiFi SSID |
| `AP_PASS` | `"YourPassword"` | WiFi password |
| `FINNHUB_TOKEN` | *(set at build)* | Finnhub API token |
| `UPDATE_FRAMES` | `FPS * 10` | Seconds between fetches per ticker |
| `MAX_TICKERS` | `6` | Number of tickers |
| `PRICE_START_ROW` | `4` | First ticker tile row |
| `PRICE_ROW_STRIDE` | `4` | Rows between tickers |

---

## Project Structure

```
stock_ticker/
├── src/
│   └── stock_ticker.c      Main ROM source (68k/SGDK)
├── C3_fw/
│   ├── mw-fw-rtos.bin      Pre-built HTTPS firmware for ESP32-C3
│   ├── flash_c3.sh         Flash pre-built firmware
│   ├── flash_c3_build.sh   Rebuild firmware from source and flash
│   └── mw_fw_rtos-v1.5.1.tar.xz  Original v1.5.1 firmware (plain HTTP)
├── out/
│   └── stock_ticker.bin    Built ROM (git-ignored)
├── Makefile
├── restore_orig_fw.sh      Rollback to stock v1.5.1 firmware
└── README.md
```

---

## Related Repositories

- **ESP32-C3 firmware**: [github.com/mikewolak/mw_fw_rtos_c3](https://github.com/mikewolak/mw_fw_rtos_c3)
- **MegaWiFi upstream**: [gitlab.com/doragasu/mw](https://gitlab.com/doragasu/mw)
- **SGDK**: [github.com/Stephane-D/SGDK](https://github.com/Stephane-D/SGDK)
