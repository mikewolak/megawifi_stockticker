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
 *     A          — add selected ticker to next free user slot
 *     B or C     — back to keyboard mode
 *     START      — close popup (cancel)
 */

typedef void (*ts_redraw_fn)(void);

/* Call once at startup.  redraw_fn is invoked when the popup closes so the
 * caller can repaint the main screen. */
void ticker_search_init(ts_redraw_fn redraw_fn);

/* TRUE while the popup is visible. */
bool ticker_search_active(void);

/* Call once per frame with freshly-pressed button mask
 * (joy & ~prev_joy in SACBRLDU format from JOY_readJoypad). */
void ticker_search_frame(u16 press);

/* Implemented in stock_ticker.c — copies sym into user slot and
 * marks the ticker for fetching. */
extern void ticker_add_user(u8 slot, const char *sym);

/* Returns TRUE if sym already appears in any of the 8 ticker slots. */
extern bool ticker_is_duplicate(const char *sym);

/* Returns the symbol string for user slot 0 or 1 (for the replace prompt). */
extern const char *ticker_get_user_sym(u8 slot);

#endif /* TICKER_SEARCH_H */
