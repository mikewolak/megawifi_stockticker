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

Output: `out/stock_ticker.bin`

The build number auto-increments each compile and is displayed in the title bar.

### Run in MAME (optional)

```sh
make run
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

## Display Freeze Fix — `mw_command()` Draw Hook

### Problem

Every time the ROM fetched a stock quote, the display froze for ~0.5–1.5 seconds
while waiting for the ESP32-C3 to complete the HTTPS round-trip. The root cause was
in `megawifi.c` (`mw_command()`):

```c
/* original — blocks the super task for the entire HTTP wait */
TSK_superPend(timeout_frames);
```

`TSK_superPend(timeout_frames)` suspends the SGDK super task for the full timeout
(up to several hundred frames), preventing any VDP writes — including scroll register
updates and tile redraws — until the response arrived. The result was a visible freeze
every 10 seconds.

### Fix

`src/megawifi.c` is a local copy of the upstream `megawifi.c` with one change to
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

The hook updates the marquee scroll and redraws one ticker row per frame:

```c
static void fetch_draw_hook(void)
{
    static u16 hook_frame = 0;
    update_marquee_scroll();
    draw_price_row((u8)(hook_frame % MAX_TICKERS));
    hook_frame++;
}
```

> **Note:** `VDP_waitVSync()` is intentionally **not** called inside the hook.
> `TSK_superPend(1)` is already posted by the VBlank ISR, so the hook fires once
> per VBlank naturally. Calling `VDP_waitVSync()` from inside the hook is
> re-entrant into SGDK's task system and causes WiFi init to fail.

### Makefile change

`megawifi.o` now builds from the local copy instead of the SGDK tree:

```makefile
$(OUT)/megawifi.o: $(SRC)/megawifi.c   # was: $(MW_SRC)/megawifi.c
```

---

## Project Structure

```
stock_ticker/
├── src/
│   ├── stock_ticker.c        Main ROM source (68k/SGDK)
│   └── megawifi.c            Local copy of mw-api megawifi.c with draw-hook freeze fix
├── C3_fw/
│   ├── flash.sh              Flash all firmware binaries in one command
│   ├── mw-fw-rtos.bin        HTTPS-capable firmware (~1.1 MB, with Mozilla CA bundle)
│   ├── bootloader.bin        Matching bootloader
│   ├── partition-table.bin   Enlarged OTA partitions (1.25 MB each)
│   └── ota_data_initial.bin  OTA data partition
├── out/
│   └── stock_ticker.bin      Built ROM (git-ignored)
├── Makefile
├── TOKEN.md.example          Template — copy to TOKEN.md and add your API key
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
