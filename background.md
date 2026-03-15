# Adding a Tiled Scrolling Background to a Sega Genesis SGDK Project

This document covers everything required to take a source image and get it
scrolling diagonally on BG_B behind text rendered on BG_A. Every mistake made
during this process is documented so you don't repeat them.

---

## 1. Understand the Genesis VDP Plane Model

The Genesis VDP has two background planes (BG_A and BG_B) plus a sprite layer.
Rendering order from back to front (without priority bits set):

```
BG_B (back)  →  BG_A (front)  →  Sprites
```

**Key rules that affect everything below:**

- **Pixel index 0 is always transparent** in any tile on any plane. It shows
  whatever is behind it (the other plane, or the VDP backdrop colour register
  for BG_B). There is no way to make index 0 opaque — choose a different index
  for any colour you want to actually see.
- **Each plane has its own 16-entry CRAM palette line** (PAL0–PAL3). A tile's
  palette is encoded in the nametable attribute word, not in the tile pixel data.
- The nametable is **64 tiles wide × 32 tiles tall** (512×256 px). Scroll
  registers wrap at exactly these boundaries.

---

## 2. Source Image Requirements

### Dimensions must tile seamlessly

The tile pattern you load into BG_B must divide evenly into the nametable
dimensions so the scroll wrap is invisible:

| Nametable | Valid pattern widths (tiles) | Valid pattern heights (tiles) |
|-----------|------------------------------|-------------------------------|
| 64 wide   | 1, 2, 4, **8, 16**, 32, 64   | —                             |
| 32 tall   | —                            | 1, 2, 4, **8**, 16, 32        |

**The original image was 80×56 px (10×7 tiles). 64 is not divisible by 10 and
32 is not divisible by 7 — this produces a visible seam.** The fix was to
resize to **128×64 px (16×8 tiles)**: 64÷16 = 4 ✓, 32÷8 = 4 ✓.

The scroll registers wrap at 512 px (H) and 256 px (V), which are exact
multiples of the nametable dimensions, so no additional math is needed.

### Format required by rescomp IMAGE

rescomp expects an **8-bit indexed PNG** (palette mode, ≤16 colours).
A 24/32-bit PNG will not work. Use ImageMagick to quantize:

```sh
magick source.png -resize 128x64! -flatten +dither -colors 14 PNG8:res/bg.png
```

Flags explained:
- `-resize 128x64!` — force exact dimensions (ignores aspect ratio)
- `-flatten` — composite alpha onto white; removes the alpha channel
- `+dither` — no dithering (hard colour boundaries look better on tiles)
- `-colors 14` — quantize to 14 colours (leaves indices 0 and 15 free — see §3)
- `PNG8:` — force 8-bit palette output

### Snap palette to Genesis colour space

The Genesis represents each RGB channel in **3 bits** (8 steps: 0, 36, 73, 109,
146, 182, 219, 255). Quantizing to arbitrary RGB888 values wastes the colour
precision. After quantizing, snap each palette entry:

```python
GENESIS_STEPS = [0, 36, 73, 109, 146, 182, 219, 255]

def snap(v):
    return min(GENESIS_STEPS, key=lambda s: abs(s - v))
```

Do this in Python after ImageMagick produces the palette PNG, then write the
snapped palette back before saving. See `tools/prep_nasdaq.py`.

### Reserve index 0 — shift all pixel values up by 1

Index 0 is transparent on the Genesis. If ANY content pixel lands on index 0
it will appear as a black hole. Prevent this by quantizing to 14 colours
(`-colors 14`) and shifting all pixel index values by +1 after loading:

```python
pixels = (np.array(quantized_img, dtype=np.uint16) + 1).clip(1, 15).astype(np.uint8)
```

This uses indices 1–14 for image content. Index 0 is never written to a tile,
so it stays transparent (black backdrop). Index 15 is reserved for a text
colour (cyan in this project, set manually with `PAL_setColor`).

---

## 3. The .res File

```
IMAGE nasdaq_bg "nasdaq_bg.png" NONE ALL 0x6010
```

Parameters:
| Field       | Value   | Meaning                                     |
|-------------|---------|---------------------------------------------|
| compression | `NONE`  | No tile compression                         |
| map_opt     | `ALL`   | Deduplicate identical and flipped tiles     |
| map_base    | `0x6010`| Documents intended palette+tile base; **NOT baked into tilemap data by rescomp** (see §5) |

rescomp generates:
- `nasdaq_bg_palette_data` — 16 Genesis BGR444 colour words
- `nasdaq_bg_tileset_data` — deduplicated 4bpp tile data
- `nasdaq_bg_tilemap_data` — bare tile indices 0..N (NOT attribute words)
- Structs: `nasdaq_bg` → `{palette, tileset, tilemap}`

Include the generated header in your C source:
```c
#include "nasdaq_bg.h"
```

Add to Makefile:
```makefile
RESCOMP  = java -jar $(GDK)/bin/rescomp.jar
RES_DIR  = res
RES_SRC  = $(RES_DIR)/nasdaq_bg.res
RES_PNG  = $(RES_DIR)/nasdaq_bg.png
RES_S    = $(OUT)/nasdaq_bg.s
RES_H    = $(OUT)/nasdaq_bg.h
RES_OBJ  = $(OUT)/nasdaq_bg.o
INCS    += -I$(OUT)          # so #include "nasdaq_bg.h" resolves

$(RES_PNG): money.png tools/prep_nasdaq.py
	python3 tools/prep_nasdaq.py $< $@

$(RES_S) $(RES_H): $(RES_SRC) $(RES_PNG)
	$(RESCOMP) $(RES_SRC) $(RES_S)

$(RES_OBJ): $(RES_S)
	$(CC) $(AFLAGS) -c $< -o $@
```

---

## 4. Scroll Mode Setup

To allow BG_A and BG_B to scroll independently per tile row, use
`HSCROLL_TILE` + `VSCROLL_PLANE`:

```c
VDP_setScrollingMode(HSCROLL_TILE, VSCROLL_PLANE);
```

State variables:
```c
static s16   bg_sx = 0;        /* BG_B horizontal scroll accumulator */
static s16   bg_sy = 0;        /* BG_B vertical scroll accumulator   */
static s16   bgb_hscroll[32];  /* one H-scroll value per tile row    */
```

Every VBlank (called **immediately after** `VDP_waitVSync()` — see §7):
```c
bg_sx = (s16)((bg_sx + 1) & 0x1FF);   /* wraps at 512 = nametable width  */
bg_sy = (s16)((bg_sy + 1) & 0xFF);    /* wraps at 256 = nametable height */

neg_scroll = -(s16)bg_sx;
for (i = 0; i < 32; i++) bgb_hscroll[i] = neg_scroll;
VDP_setHorizontalScrollTile(BG_B, 0, bgb_hscroll, 32, DMA);
VDP_setVerticalScroll(BG_B, (s16)bg_sy);
```

---

## 5. The Critical Bug: rescomp Tilemap Entries Are Bare Indices

**This was the root cause of all display corruption.**

rescomp's `IMAGE` resource stores bare tile indices (0, 1, 2 …) in the
tilemap — the `map_base` parameter in the .res file is documentation only.
It does **not** bake palette bits or tile offset into the tilemap data.

If you write these bare values directly to the VDP nametable, every tile uses
PAL0 (the text palette) instead of PAL3 (the background palette), and the tile
indices are wrong. The display looks completely scrambled.

**You must add `TILE_ATTR_FULL` yourself when filling the nametable:**

```c
for (row = 0; row < 32; row++) {
    for (col = 0; col < 64; col++) {
        u16 src      = (u16)((row % 8) * 16 + (col % 16)); /* 16×8 tile pattern */
        u16 tile_idx = nasdaq_bg.tilemap->tilemap[src];     /* bare index 0..N  */
        u16 attr     = TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE,
                                      TILE_USER_INDEX + tile_idx);
        VDP_setTileMapXY(BG_B, attr, col, row);
    }
}
```

Where:
- `PAL3` — palette line 3 (where the background palette was loaded)
- `TILE_USER_INDEX` (= 16) — VRAM tile slot where `VDP_loadTileSet` placed the tiles
- `tile_idx` — deduplicated tile index from rescomp, 0-based

---

## 6. Palette Setup

```c
/* Load 15 background colours into PAL3 slots 0–14 */
PAL_setColors(PAL3 * 16, nasdaq_bg.palette->data, 15, CPU);

/* Slot 15 reserved for a text colour — set manually */
PAL_setColor(63, RGB24_TO_VDPCOLOR(0x00AAFF));
```

`PAL3 * 16 = 48` is the CRAM offset for palette line 3.
`PAL_setColor(63, ...)` targets CRAM index 63 = PAL3[15].

The image uses pixel indices 1–14, so PAL3[0] is never referenced by a tile
pixel (it would be transparent anyway). PAL3[15] is free for text.

---

## 7. VBlank Timing — Scroll Must Be Written First

Writing scroll registers during active display causes visible glitching
(horizontal tearing, font noise). The **scroll register write must be the very
first thing after `VDP_waitVSync()`**, before any tilemap writes:

```c
/* main loop */
VDP_waitVSync();
update_marquee_scroll();   /* ← scroll registers updated HERE, in VBlank */
draw_price_row(frame % MAX_TICKERS);
frame++;
```

If you write tilemap data first (e.g. `VDP_setTileMapXY` calls) and then update
scroll, the scroll write lands in active display and you get analog-looking
noise artefacts on the top edge of text tiles.

---

## 8. Masking BG_B Under Text (Optional — `BCKGND_ON`)

The money background is guarded by `#ifdef BCKGND_ON` and disabled by default.
To re-enable it: `make CFLAGS+="-DBCKGND_ON"`.

When the background is on, BG_B shows through wherever BG_A has transparent
pixels (index 0). To put a solid-colour "panel" behind specific text rows,
load a tile with all pixels = 1 (PAL0[1] = black) and fill only those rows:

```c
#define BLACK_TILE_IDX  (TILE_FONT_INDEX - 1)
static const u32 black_tile_data[8] = {
    0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111
};

VDP_loadTileData(black_tile_data, BLACK_TILE_IDX, 1, CPU);
u16 black = TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, BLACK_TILE_IDX);

VDP_fillTileMapRect(BG_A, black, 0, 0, 40, 4);   /* header rows */
for (r = 0; r < MAX_TICKERS; r++)
    VDP_fillTileMapRect(BG_A, black, 0,
                        PRICE_START_ROW + r * PRICE_ROW_STRIDE, 40, 1);
```

`TILE_FONT_INDEX - 1 = 1951` is the last user tile slot, safely below the
SGDK font tiles and well above the money tileset (~16..~140).

Note: font tile backgrounds are pixel index 0 (transparent), so the money
background still peeks through inside the glyph curves. Only the blank areas
between characters are covered by the black panel tile.

---

## 9. Complete Initialisation Sequence

```c
/* 1. Set scroll mode */
VDP_setScrollingMode(HSCROLL_TILE, VSCROLL_PLANE);

/* 2. Load palette */
PAL_setColors(PAL3 * 16, nasdaq_bg.palette->data, 15, CPU);
PAL_setColor(63, RGB24_TO_VDPCOLOR(0x00AAFF));

/* 3. Load tileset into VRAM */
VDP_loadTileSet(nasdaq_bg.tileset, TILE_USER_INDEX, DMA);
VDP_waitDMACompletion();

/* 4. Fill BG_B nametable with the tiled pattern */
for (row = 0; row < 32; row++) {
    for (col = 0; col < 64; col++) {
        u16 src      = (u16)((row % 8) * 16 + (col % 16));
        u16 tile_idx = nasdaq_bg.tilemap->tilemap[src];
        VDP_setTileMapXY(BG_B,
            TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE, TILE_USER_INDEX + tile_idx),
            col, row);
    }
}

/* 5. Initialise scroll tables */
for (i = 0; i < 32; i++) bgb_hscroll[i] = 0;
VDP_setHorizontalScrollTile(BG_B, 0, bgb_hscroll, 32, DMA);
VDP_setVerticalScroll(BG_B, 0);
```

---

## 10. Pitfall Summary

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Pattern dimensions don't divide nametable | Visible seam every few seconds as scroll wraps | Resize source to dimensions that divide 64 and 32 evenly |
| Bare tilemap indices written to nametable | Completely scrambled colours | Add `TILE_ATTR_FULL(PAL3, ..., TILE_USER_INDEX + idx)` |
| Pixel index 0 used for content | Black holes in image | Shift all pixel values +1 after quantization |
| Scroll registers written after tilemap updates | Font noise / tearing on text tiles | Move scroll writes to be first after `VDP_waitVSync()` |
| Too many colours quantized (>15) | rescomp palette overflow or wrong colours | Use `-colors 14` in ImageMagick, leaving indices 0 and 15 free |
| Palette not Genesis-snapped | Colours look different from expected | Snap each channel to nearest of 0,36,73,109,146,182,219,255 |
| rescomp C-style comments in .res file | Build error: unknown resource type | Use no comments in .res files |
