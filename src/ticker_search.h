#ifndef TICKER_SEARCH_H
#define TICKER_SEARCH_H

#include <genesis.h>

/*
 * ticker_search.h
 * Popup ticker search — on-screen QWERTY + binary search into
 * nasdaq_tickers.bin embedded in ROM.
 *
 * Controls when popup is open:
 *   Keyboard mode (default):
 *     D-pad      — move key cursor
 *     A          — type highlighted key
 *     B          — backspace
 *     C          — switch to results-browse mode
 *     START      — close popup (cancel)
 *   Results mode:
 *     UP/DOWN    — move selection within page
 *     LEFT/RIGHT — previous / next page
 *     A          — replace target slot with selected ticker
 *     B or C     — back to keyboard mode
 *     START      — close popup (cancel)
 */

typedef void (*ts_redraw_fn)(void);

/* Call once at startup.  redraw_fn is invoked when the popup closes so the
 * caller can repaint the main screen. */
void ticker_search_init(ts_redraw_fn redraw_fn);

/* TRUE while the popup is visible. */
bool ticker_search_active(void);

/* Open the popup targeting the given ticker slot (0..MAX_TICKERS-1).
 * The chosen ticker will replace that slot. */
void ticker_search_open(u8 slot);

/* Call once per frame with freshly-pressed button mask
 * (joy & ~prev_joy in SACBRLDU format from JOY_readJoypad).
 * Handles all input when the popup is active; no-op otherwise. */
void ticker_search_frame(u16 press);

/* Implemented in stock_ticker.c — replaces the symbol in a given slot
 * and marks it for re-fetching. */
extern void ticker_set_slot(u8 slot, const char *sym);

/* Returns TRUE if sym already appears in any ticker slot. */
extern bool ticker_is_duplicate(const char *sym);

/* Returns the symbol string for any slot 0..MAX_TICKERS-1. */
extern const char *ticker_get_slot_sym(u8 slot);

#endif /* TICKER_SEARCH_H */
