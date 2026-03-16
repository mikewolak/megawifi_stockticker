# MegaWiFi Stock Ticker

A live stock ticker for the **Sega Genesis / Mega Drive** running on a
[MegaWiFi](https://gitlab.com/doragasu/mw) ESP32-C3 cartridge. Fetches real-time
quotes from the [Finnhub](https://finnhub.io) HTTPS API and displays them on the
Genesis VDP — no PC required after the cartridge is flashed.

---

## Features

- **8 fully editable ticker slots** — defaults: ORCL, NVDA, AMZN, IBM, MSFT, GOOGL, META, AVGO
- **Live HTTPS quotes** — TLS handled entirely by the ESP32-C3 firmware using the full Mozilla CA bundle (no custom certificate needed)
- **On-screen ticker search** — QWERTY popup keyboard with binary search across 7,300+ NASDAQ, NYSE, and AMEX symbols; any slot replaceable at runtime via START button
- **Color-coded deltas** — green for gains, red for losses; company brand colors per symbol (ORCL red, NVDA green, IBM/MSFT cyan, META green, GOOGL per-letter multicolor)
- **Per-ticker share counts** — randomized 1–100,000 on startup (seeded from WiFi connect timing XOR'd with NTP clock); editable at runtime with UP/DOWN/LEFT/RIGHT/A/B
- **Net worth line** — real-time sum of price × shares for all valid tickers, formatted as `$NNN,NNN,NNN,NNN.XX` (up to 999 billion with cents)
- **Scrolling price marquee** — bottom row scrolls all tickers and prices continuously
- **Countdown timer** — shows seconds to next fetch with last-updated timestamp
- **Timestamp per quote** — CST time the price was fetched (12-hour format)
- **Throttled fetches** — one ticker every 10 seconds, round-robin (6 calls/min — within Finnhub's 60/min free tier)
- **NTP time sync** via `pool.ntp.org`, US Central timezone (`CST6CDT`) with automatic DST

---

## Controls

| Button | Normal mode | Share-edit mode | Search popup |
|--------|-------------|-----------------|--------------|
| **UP / DOWN** | Move row selection | ±100 shares | Move cursor / results |
| **LEFT / RIGHT** | — | ±1000 shares | Move cursor / page |
| **A** | Enter share-edit mode | +1 share | Type key / confirm |
| **B** | — | −1 share | Backspace |
| **C** | — | Exit edit mode | Switch KB ↔ results |
| **START** | Open ticker search for selected row | Exit edit mode | Close popup |

---

## Display Layout

```
[ MegaWifi Stock Ticker v1.5 MegaWiFi ]

  (c) March 2026 Mike Wolak

  Next Update: 8s  03/15/26 2:34:01 PM
>ORCL   $155.23  -1.25(-0.80%)   2:34 PM
 NVDA   $900.44  +1.56(+0.17%)   2:34 PM
 AMZN   $207.46  -2.01(-0.96%)   2:33 PM
 IBM    $255.10  +0.88(+0.35%)   2:33 PM
 MSFT   $475.55  -3.21(-0.67%)   2:32 PM
 GOOGL  $175.22  -1.43(-0.81%)   2:32 PM
 META   $512.88  +3.10(+0.61%)   2:31 PM
 AVGO   $168.44  -0.92(-0.54%)   2:31 PM

 Net Worth:  $1,243,876.42
... scrolling marquee ...
```

Column layout (40-char screen):
| Col | Content |
|-----|---------|
| 0 | `>` cursor (selected row) |
| 1–5 | Symbol (company brand color) |
| 6 | Gap |
| 7–15 | Price — `$NNN.NN` (white, 9 chars fixed) |
| 16–29 | Delta + % change (green/red, 14 chars fixed) |
| 30–39 | Fetch timestamp or share count (cyan when editing) |

---

## Ticker Database

The ticker search popup uses a binary-searchable database of 7,300+ symbols embedded
in the ROM. It is **not checked in** — it is generated fresh on first build by fetching
live data from NASDAQ's public FTP server (no API key required):

```sh
python3 conv.py
```

This runs automatically the first time you run `make` if the file is missing.

Sources:
- `ftp.nasdaqtrader.com/SymbolDirectory/nasdaqlisted.txt` — NASDAQ stocks
- `ftp.nasdaqtrader.com/SymbolDirectory/otherlisted.txt` — NYSE, AMEX, NYSE Arca

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

| Tool | Notes |
|------|-------|
| SGDK | Install to `~/sgdk` (needs `libmd.a`, `inc/`, `src/`) |
| m68k-elf-gcc | In `PATH` — rosco m68k toolchain v13 recommended |
| Python 3 | For `conv.py` (ticker database fetch) |
| Finnhub API token | Free account at [finnhub.io](https://finnhub.io) |
| Internet access | Required on first build to fetch ticker database |

### API Token setup

Get a free API key at [finnhub.io/register](https://finnhub.io/register), then:

```sh
cp TOKEN.md.example TOKEN.md
# Edit TOKEN.md and replace the placeholder with your key
```

`TOKEN.md` is git-ignored and never committed. The Makefile reads it automatically.
You can also pass the token directly: `make FINNHUB_TOKEN=yourkey`

### Build

```sh
make
```

On first build, `make` will automatically run `python3 conv.py` to fetch and generate
`nasdaq_tickers.bin` if it is missing. Output: `out/stock_ticker.bin`

The build number auto-increments each compile and is displayed in the title bar.

### Refresh ticker database

```sh
python3 conv.py
```

Re-fetches both NASDAQ and NYSE/AMEX/Arca listings and rebuilds the binary. Useful
when new symbols are listed or you want the latest data.

### Run in MAME (optional)

```sh
make run
```

---

## Flashing

### Flash the custom ESP32-C3 firmware (required once)

```sh
PORT=/dev/cu.usbmodem101 ./C3_fw/flash.sh
```

This flashes `C3_fw/mw-fw-rtos.bin` (the pre-built HTTPS-capable firmware).
Power-cycle the cart after flashing.

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
| `AP_SSID` | `"YourSSID"` | WiFi network name |
| `AP_PASS` | `"YourPassword"` | WiFi password |
| `FINNHUB_TOKEN` | *(set at build time)* | Finnhub API token |
| `UPDATE_FRAMES` | `FPS * 10` | Frames between fetches (10 seconds) |
| `MAX_TICKERS` | `8` | Number of ticker slots |

---

## Display Freeze Fix — `mw_command()` Draw Hook

### Problem

Every time the ROM fetched a stock quote, the display froze for ~0.5–1.5 seconds
while waiting for the ESP32-C3 to complete the HTTPS round-trip. The root cause was
in `megawifi.c` (`mw_command()`):

```c
/* original — blocks the super task for the entire HTTP wait */
TSK_superPend(timeout_frames);
```

`TSK_superPend(timeout_frames)` suspends the SGDK super task for the full timeout,
preventing any VDP writes until the response arrived. The result: a visible freeze
every 10 seconds.

### Fix

`src/megawifi.c` is a local copy of the upstream file with one change to
`mw_command()`: instead of one long pend, it loops with a **1-frame pend** and calls
a registered draw hook each iteration:

```c
do {
    tout = TSK_superPend(1);
    if (mw_draw_hook) mw_draw_hook();
    frames_left--;
} while (tout && frames_left > 0);
```

`stock_ticker.c` registers `fetch_draw_hook()` before WiFi init:

```c
mw_set_draw_hook(fetch_draw_hook);
```

The hook updates the marquee scroll and redraws one ticker row per frame, keeping the
display live during every HTTP round-trip.

> **Note:** `VDP_waitVSync()` is intentionally **not** called inside the hook.
> `TSK_superPend(1)` is already posted by the VBlank ISR, so the hook fires once
> per VBlank naturally. Calling `VDP_waitVSync()` from inside the hook is
> re-entrant into SGDK's task system and causes WiFi init to fail.

---

## Project Structure

```
stock_ticker/
├── src/
│   ├── stock_ticker.c        Main ROM source (68k/SGDK)
│   ├── ticker_search.c       Ticker search popup — QWERTY keyboard + binary search
│   ├── ticker_search.h
│   ├── megawifi.c            Local mw-api copy with per-frame draw hook fix
│   └── config.h              Local config override (MODULE_MEGAWIFI=1)
├── C3_fw/
│   ├── flash.sh              Flash all firmware partitions
│   ├── mw-fw-rtos.bin        HTTPS-capable firmware (~1.1 MB, Mozilla CA bundle)
│   ├── bootloader.bin        Matching bootloader
│   ├── partition-table.bin   Enlarged OTA partitions (1.25 MB each)
│   └── ota_data_initial.bin  OTA data partition
├── out/
│   └── stock_ticker.bin      Built ROM (git-ignored)
├── Makefile
├── conv.py                   Fetches NASDAQ + NYSE/AMEX ticker DB (run on first build)
├── TOKEN.md.example          Template — copy to TOKEN.md and add your Finnhub key
├── TOKEN.md                  Your Finnhub token (git-ignored, never committed)
├── restore_orig_fw.sh        Rollback to stock v1.5.1 firmware
├── BUILD_NOTES.md            Toolchain and build environment notes
├── FLASH_NOTES.md            Firmware flash address map and procedure
└── README.md
```

---

## Related Repositories

- **ESP32-C3 firmware**: [github.com/mikewolak/mw_fw_rtos_c3](https://github.com/mikewolak/mw_fw_rtos_c3)
- **MegaWiFi upstream**: [gitlab.com/doragasu/mw](https://gitlab.com/doragasu/mw)
- **SGDK**: [github.com/Stephane-D/SGDK](https://github.com/Stephane-D/SGDK)
- **Finnhub API**: [finnhub.io](https://finnhub.io)
