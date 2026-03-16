/*
 * ticker_search.c
 * Popup ticker search with on-screen QWERTY keyboard and binary search
 * into nasdaq_tickers.bin (3889 records, 38 bytes each, sorted by symbol).
 *
 * Record layout:
 *   offset 0..5   char symbol[6]  null-padded, e.g. "NVDA\0\0"
 *   offset 6..37  char name[32]   truncated company name, null-padded
 *
 * Screen popup occupies rows 4-26 (leaves title area and marquee intact).
 */

#include <genesis.h>
#include <string.h>
#include "ticker_search.h"

/* Forward declaration */
void ticker_search_close(void);

/* SGDK's libmd may not export ts_strncmp — provide a local copy. */
static int ts_strncmp(const char *a, const char *b, u8 n)
{
    while (n--) {
        if ((unsigned char)*a != (unsigned char)*b)
            return (unsigned char)*a - (unsigned char)*b;
        if (*a == '\0') return 0;
        a++; b++;
    }
    return 0;
}

/* ── ROM binary ──────────────────────────────────────────────────────────── */
extern char nasdaq_tickers_start[];
extern char nasdaq_tickers_end[];

#define REC_SIZE   38
#define SYM_LEN     6
#define NAME_OFF    6
#define NAME_LEN   32

/* ── Screen layout ───────────────────────────────────────────────────────── */
/*  Row 4   : popup header bar
 *  Row 5   : search input line
 *  Row 6   : separator
 *  Rows 7-12 : 6 result lines
 *  Row 13  : page / match count info
 *  Row 14  : separator
 *  Rows 15-18: QWERTY keyboard (4 rows)
 *  Row 19  : hint line
 *  Rows 20-26: cleared (covers user-slot ticker rows + spare)
 */
#define POP_TOP       4
#define POP_BOT      26
#define SEARCH_ROW    5
#define SEP1_ROW      6
#define RES_ROW0      7
#define N_RESULTS     6
#define PAGE_ROW     13
#define SEP2_ROW     14
#define KB_ROW0      15
#define HINT_ROW     19

#define KB_COL        2    /* left edge of key grid  */
#define SP_COL       24    /* column for DEL/CLR/ADD */

/* ── QWERTY keyboard ─────────────────────────────────────────────────────── */
static const char *const kb_rows[4] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM"
};
static const u8 kb_lens[4] = {10, 10, 9, 7};

#define N_SP 3
static const char *const sp_label[N_SP] = {"DEL", "CLR", "ADD"};

/* ── State ───────────────────────────────────────────────────────────────── */
typedef enum { MODE_KB, MODE_RESULTS, MODE_REPLACE } ts_mode_t;

static bool       ts_active;
static ts_mode_t  ts_mode;
static ts_redraw_fn ts_redraw_cb;

/* search buffer */
static char  sbuf[SYM_LEN + 1];
static u8    slen;

/* keyboard cursor */
static s8    kb_r;         /* row  0..3              */
static s8    kb_c;         /* col  0..kb_lens[r]-1   */
static bool  kb_sp;        /* TRUE = cursor on special keys */
static s8    sp_r;         /* special-key row 0..N_SP-1     */

/* results */
static u32   res_first;    /* record index of first match   */
static u32   res_total;    /* number of matching records    */
static u8    res_page;
static u8    res_sel;      /* selected entry on current page */

static u8    user_slots_used;
static u32   total_records;

/* pending symbol while user chooses which slot to replace */
static char  replace_sym[8];
static u8    replace_sel;    /* 0 = slot 1, 1 = slot 2 */

/* extern to read current user slot symbols for the replace prompt */
extern const char *ticker_get_user_sym(u8 slot);

/* ── ROM accessors ───────────────────────────────────────────────────────── */
static inline const char *rec_sym(u32 i)
{
    return nasdaq_tickers_start + i * REC_SIZE;
}
static inline const char *rec_name(u32 i)
{
    return nasdaq_tickers_start + i * REC_SIZE + NAME_OFF;
}

/* ── Binary search ───────────────────────────────────────────────────────── */
/* Returns first index where sym >= pfx (len chars compared). */
static u32 lower_bound(const char *pfx, u8 len)
{
    u32 lo = 0, hi = total_records;
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        if (ts_strncmp(rec_sym(mid), pfx, len) < 0) lo = mid + 1;
        else                                      hi = mid;
    }
    return lo;
}

static void run_search(void)
{
    u32 i;
    if (slen == 0) {
        res_first = 0;
        res_total = total_records;
    } else {
        res_first = lower_bound(sbuf, slen);
        i = res_first;
        while (i < total_records && ts_strncmp(rec_sym(i), sbuf, slen) == 0)
            i++;
        res_total = i - res_first;
    }
    res_page = 0;
    res_sel  = 0;
}

/* ── Draw helpers ────────────────────────────────────────────────────────── */
static void clear_rows(u8 r0, u8 r1)
{
    u8 r;
    VDP_setTextPalette(PAL0);
    for (r = r0; r <= r1; r++)
        VDP_drawText("                                        ", 0, r);
}

static void draw_keyboard(void)
{
    u8 r, c;
    char ch[2] = {0, 0};

    for (r = 0; r < 4; r++) {
        for (c = 0; c < kb_lens[r]; c++) {
            bool sel = (ts_mode == MODE_KB) && !kb_sp
                       && kb_r == (s8)r && kb_c == (s8)c;
            VDP_setTextPalette(sel ? PAL1 : PAL0);
            ch[0] = kb_rows[r][c];
            VDP_drawText(ch, KB_COL + c * 2, KB_ROW0 + r);
        }
    }
    /* special-key column */
    for (r = 0; r < N_SP; r++) {
        bool sel = (ts_mode == MODE_KB) && kb_sp && sp_r == (s8)r;
        VDP_setTextPalette(sel ? PAL1 : PAL0);
        VDP_drawText(sp_label[r], SP_COL, KB_ROW0 + r);
    }
    VDP_setTextPalette(PAL0);
}

static void draw_search_box(void)
{
    char line[42];
    sprintf(line, "Type: %-6s_  slots free: %u  ", sbuf,
            (unsigned)(2 - user_slots_used));
    VDP_setTextPalette(PAL0);
    VDP_drawText(line, 1, SEARCH_ROW);
}

static void draw_results(void)
{
    u8  i;
    u32 base         = res_first + (u32)res_page * N_RESULTS;
    u8  total_pages  = res_total
                       ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS)
                       : 1;
    char line[42];

    for (i = 0; i < N_RESULTS; i++) {
        u32  idx = base + i;
        bool sel = (ts_mode == MODE_RESULTS) && (res_sel == i);
        char sym[7];
        char ntrunc[25];
        u8   j;

        /* blank the row first */
        VDP_setTextPalette(PAL0);
        VDP_drawText("                                        ", 0, RES_ROW0 + i);

        if (idx >= res_first + res_total) continue;

        /* copy and display-pad symbol */
        for (j = 0; j < SYM_LEN; j++)
            sym[j] = rec_sym(idx)[j] ? rec_sym(idx)[j] : ' ';
        sym[6] = '\0';

        /* truncate name */
        for (j = 0; j < 24 && rec_name(idx)[j]; j++)
            ntrunc[j] = rec_name(idx)[j];
        ntrunc[j] = '\0';

        sprintf(line, "%-6s  %s", sym, ntrunc);
        VDP_setTextPalette(sel ? PAL1 : PAL0);
        VDP_drawText(line, 1, RES_ROW0 + i);
    }

    VDP_setTextPalette(PAL0);
    sprintf(line, "pg %u/%u  matches:%-5lu  [C=switch A=add]",
            (unsigned)(res_page + 1), (unsigned)total_pages,
            res_total);
    VDP_drawText(line, 1, PAGE_ROW);
}

static void draw_replace_prompt(void)
{
    char line[42];
    const char *s0 = ticker_get_user_sym(0);
    const char *s1 = ticker_get_user_sym(1);

    VDP_setTextPalette(PAL1);
    VDP_drawText("Both slots full — choose slot to replace:", 0, RES_ROW0);
    VDP_setTextPalette(replace_sel == 0 ? PAL1 : PAL0);
    sprintf(line, " %c Slot 7: %-6s -> %s",
            replace_sel == 0 ? '>' : ' ', s0, replace_sym);
    VDP_drawText(line, 0, RES_ROW0 + 2);
    VDP_setTextPalette(replace_sel == 1 ? PAL1 : PAL0);
    sprintf(line, " %c Slot 8: %-6s -> %s",
            replace_sel == 1 ? '>' : ' ', s1, replace_sym);
    VDP_drawText(line, 0, RES_ROW0 + 3);
    VDP_setTextPalette(PAL0);
    VDP_drawText("[UP/DOWN=choose  A=confirm  B=cancel]   ", 0, RES_ROW0 + 5);
}

static void draw_hint(void)
{
    VDP_setTextPalette(PAL0);
    VDP_drawText("KB:A=type B=del C=results | RES:A=add B/C=kb",
                 0, HINT_ROW);
}

static void draw_popup(void)
{
    clear_rows(POP_TOP, POP_BOT);
    VDP_setTextPalette(PAL1);
    VDP_drawText("--[ TICKER SEARCH ]----- START=close ----", 0, POP_TOP);
    VDP_setTextPalette(PAL0);
    VDP_drawText("-----------------------------------------", 0, SEP1_ROW);
    VDP_drawText("-----------------------------------------", 0, SEP2_ROW);
    draw_search_box();
    draw_results();
    draw_keyboard();
    draw_hint();
}

/* ── Keyboard actions ────────────────────────────────────────────────────── */
static void kb_type_char(char c)
{
    if (slen >= SYM_LEN) return;
    sbuf[slen++] = c;
    sbuf[slen]   = '\0';
    run_search();
    draw_search_box();
    draw_results();
}

static void kb_backspace(void)
{
    if (slen == 0) return;
    sbuf[--slen] = '\0';
    run_search();
    draw_search_box();
    draw_results();
}

static void add_ticker_at(u32 idx)
{
    char sym[8];
    u8   j;

    for (j = 0; j < SYM_LEN; j++)
        sym[j] = rec_sym(idx)[j];
    sym[SYM_LEN] = '\0';
    /* right-trim null padding */
    for (j = SYM_LEN; j > 0 && sym[j - 1] == '\0'; j--)
        sym[j - 1] = '\0';

    /* reject duplicates — don't add a ticker already in the list */
    if (ticker_is_duplicate(sym)) {
        draw_search_box();   /* redraw so any stale message clears */
        draw_results();
        return;
    }

    if (user_slots_used >= 2) {
        /* both slots full — ask which to overwrite */
        u8 k;
        for (k = 0; k <= SYM_LEN; k++) replace_sym[k] = sym[k];
        replace_sel = 0;
        ts_mode = MODE_REPLACE;
        /* clear the results area and show the replace prompt */
        u8 r;
        for (r = RES_ROW0; r <= PAGE_ROW + 2; r++)
            VDP_drawText("                                        ", 0, r);
        draw_replace_prompt();
        return;
    }

    ticker_add_user(user_slots_used, sym);
    user_slots_used++;
    ticker_search_close();
}

static void kb_select(void)
{
    if (kb_sp) {
        switch (sp_r) {
        case 0: kb_backspace(); break;
        case 1:                                /* CLR */
            slen = 0; sbuf[0] = '\0';
            run_search();
            draw_search_box();
            draw_results();
            break;
        case 2:                                /* ADD — adds first result */
            if (res_total > 0) add_ticker_at(res_first);
            break;
        }
    } else {
        kb_type_char(kb_rows[(u8)kb_r][(u8)kb_c]);
    }
}

static void kb_move_row(s8 d)
{
    if (kb_sp) {
        sp_r = (s8)((sp_r + d + N_SP) % N_SP);
    } else {
        s8 nr = (s8)((kb_r + d + 4) % 4);
        s8 nc = kb_c;
        if (nc >= (s8)kb_lens[(u8)nr]) nc = (s8)(kb_lens[(u8)nr] - 1);
        kb_r = nr;
        kb_c = nc;
    }
    draw_keyboard();
}

static void kb_move_col(s8 d)
{
    if (kb_sp) {
        if (d < 0) { kb_sp = false; }   /* left → back to keys */
    } else {
        s8 nc = (s8)(kb_c + d);
        if (nc >= (s8)kb_lens[(u8)kb_r]) {
            kb_sp = true;
            sp_r  = (kb_r < N_SP) ? kb_r : 0;
        } else if (nc < 0) {
            nc = 0;
        } else {
            kb_c = nc;
        }
    }
    draw_keyboard();
}

/* ── Results actions ─────────────────────────────────────────────────────── */
static u8 page_count(void)
{
    u8 total_pages = res_total
                     ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
    if (res_page == total_pages - 1) {
        u32 rem = res_total - (u32)res_page * N_RESULTS;
        return rem < N_RESULTS ? (u8)rem : N_RESULTS;
    }
    return N_RESULTS;
}

static void res_move(s8 d)
{
    u8 cnt = page_count();
    if (cnt == 0) return;
    res_sel = (u8)((res_sel + d + cnt) % cnt);
    draw_results();
}

static void res_page_move(s8 d)
{
    u8 total_pages = res_total
                     ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
    res_page = (u8)((res_page + d + total_pages) % total_pages);
    res_sel  = 0;
    draw_results();
}

static void res_add(void)
{
    u32 idx;
    if (res_total == 0 || user_slots_used >= 2) return;
    idx = res_first + (u32)res_page * N_RESULTS + res_sel;
    if (idx >= res_first + res_total) return;
    add_ticker_at(idx);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void ticker_search_init(ts_redraw_fn redraw_fn)
{
    total_records   = (u32)(nasdaq_tickers_end - nasdaq_tickers_start) / REC_SIZE;
    ts_redraw_cb    = redraw_fn;
    ts_active       = false;
    user_slots_used = 0;
}

bool ticker_search_active(void) { return ts_active; }

void ticker_search_close(void)
{
    ts_active = false;
    clear_rows(POP_TOP, POP_BOT);
    if (ts_redraw_cb) ts_redraw_cb();
}

static void ticker_search_open(void)
{
    /* always allow open — if both slots full we replace slot 1 */
    slen   = 0; sbuf[0] = '\0';
    kb_r   = 1; kb_c = 0;              /* start on Q   */
    kb_sp  = false; sp_r = 0;
    ts_mode = MODE_KB;
    run_search();
    ts_active = true;
    draw_popup();
}

void ticker_search_frame(u16 press)
{
    if (!ts_active) {
        if (press & BUTTON_START) ticker_search_open();
        return;
    }

    if (press & BUTTON_START) { ticker_search_close(); return; }

    if (ts_mode == MODE_REPLACE) {
        if (press & BUTTON_UP || press & BUTTON_DOWN) {
            replace_sel ^= 1;
            draw_replace_prompt();
        } else if (press & BUTTON_A) {
            ticker_add_user(replace_sel, replace_sym);
            /* user_slots_used stays at 2 */
            ticker_search_close();
        } else if (press & BUTTON_B) {
            /* cancel replace — go back to results */
            ts_mode = MODE_RESULTS;
            draw_results();
            draw_keyboard();
        }
        return;
    }

    if (ts_mode == MODE_KB) {
        if      (press & BUTTON_UP)    kb_move_row(-1);
        else if (press & BUTTON_DOWN)  kb_move_row(1);
        else if (press & BUTTON_LEFT)  kb_move_col(-1);
        else if (press & BUTTON_RIGHT) kb_move_col(1);
        else if (press & BUTTON_A)     kb_select();
        else if (press & BUTTON_B)     kb_backspace();
        else if (press & BUTTON_C) {
            ts_mode = MODE_RESULTS;
            draw_results();
            draw_keyboard();
        }
    } else {
        if      (press & BUTTON_UP)    res_move(-1);
        else if (press & BUTTON_DOWN)  res_move(1);
        else if (press & BUTTON_LEFT)  res_page_move(-1);
        else if (press & BUTTON_RIGHT) res_page_move(1);
        else if (press & BUTTON_A)     res_add();
        else if ((press & BUTTON_B) || (press & BUTTON_C)) {
            ts_mode = MODE_KB;
            draw_results();
            draw_keyboard();
        }
    }
}
