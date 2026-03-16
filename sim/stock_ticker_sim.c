/*
 * stock_ticker_sim.c
 * ncurses + libcurl simulation of the Sega Genesis MegaWifi Stock Ticker.
 *
 * Exact 40-column × 28-row screen, same integer types, same layout constants,
 * same logic.  Real Finnhub HTTPS quotes via libcurl.
 *
 * Build: make  (see Makefile)
 * Run:   ./stock_ticker_sim
 *
 * Keyboard → Genesis button mapping:
 *   Arrow keys  → D-pad
 *   a           → A  (type key / enter share-edit / confirm)
 *   b / Bksp    → B  (backspace / share -1)
 *   c           → C  (switch KB↔results / exit share-edit)
 *   s / Enter   → START (open/close search popup)
 *   q           → quit
 */

#include <ncurses.h>
#include <curl/curl.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>

/* ── Types (mirror SGDK / m68k sizes) ─────────────────────────────────── */
typedef int8_t    s8;
typedef uint8_t   u8;
typedef int16_t   s16;
typedef uint16_t  u16;
typedef int32_t   s32;
typedef uint32_t  u32;
typedef int64_t   s64;
typedef uint64_t  u64;

/* ── Genesis button bitmasks (SGDK values) ─────────────────────────────── */
#define BUTTON_UP     0x0001u
#define BUTTON_DOWN   0x0002u
#define BUTTON_LEFT   0x0004u
#define BUTTON_RIGHT  0x0008u
#define BUTTON_B      0x0010u
#define BUTTON_C      0x0020u
#define BUTTON_A      0x0040u
#define BUTTON_START  0x2000u

/* ── Screen layout — identical to Genesis VDP ──────────────────────────── */
#define SCREEN_COLS      40
#define SCREEN_ROWS      28

#define COPYRIGHT_ROW    2
#define COPYRIGHT_COL    2
#define COUNTDOWN_ROW    4
#define COUNTDOWN_COL    2
#define STATUS_ROW       5
#define PRICE_START_ROW  7
#define PRICE_ROW_STRIDE 2
#define MAX_TICKERS      8
#define NET_WORTH_ROW    (PRICE_START_ROW + MAX_TICKERS * PRICE_ROW_STRIDE + 1)
#define MARQUEE_ROW      27
#define UPDATE_SECS      10

/* ── Palette IDs → ncurses color pairs ─────────────────────────────────── */
#define PAL0  1   /* white  */
#define PAL1  2   /* green  */
#define PAL2  3   /* red    */
#define PAL3  4   /* cyan   */

/* ── TickerEntry — identical to Genesis struct ──────────────────────────── */
typedef struct {
    char  symbol[8];
    s32   price_cents;
    s32   prev_close_cents;
    bool  valid;
    char  timestamp[12];   /* "HH:MM AM\0" */
    u32   shares_owned;
} TickerEntry;

/* ── Globals ────────────────────────────────────────────────────────────── */
static const char *default_syms[MAX_TICKERS] = {
    "ORCL","NVDA","AMZN","IBM","MSFT","GOOGL","META","AVGO"
};
static TickerEntry tickers[MAX_TICKERS];
static u8          ticker_sym_pal[MAX_TICKERS];

static s8   selected = -1;
static bool editing  = false;

/* Marquee */
static char marquee_str[512];
static u8   marquee_pal_arr[512];
static u16  marquee_pos = 0;

static char last_update_str[24] = "";

/* Fetch timing */
static time_t last_fetch_time = 0;
static u8     fetch_cycle     = 0;

/* Token */
static char finnhub_token[128] = "";

/* NASDAQ binary DB */
static char *nasdaq_data  = NULL;
static u32   nasdaq_total = 0;
#define REC_SIZE  38
#define SYM_LEN    6
#define NAME_OFF   6

/* Thread safety for tickers[] */
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ncurses windows */
static WINDOW *scr;        /* 28×40 content window */
static WINDOW *border_win; /* 30×42 border window  */

/* ── Ticker search state (inlined from ticker_search.c) ─────────────────── */
typedef enum { MODE_KB, MODE_RESULTS } ts_mode_t;

static bool      ts_active      = false;
static ts_mode_t ts_mode;
static u8        ts_target_slot;

#define TS_SYM_LEN  6
#define N_RESULTS   6
#define POP_TOP     4
#define POP_BOT     26
#define SEARCH_ROW  5
#define SEP1_ROW    6
#define RES_ROW0    7
#define PAGE_ROW    13
#define SEP2_ROW    14
#define KB_ROW0     15
#define HINT_ROW    19
#define KB_COL      2
#define SP_COL      24
#define N_SP        3

static char sbuf[TS_SYM_LEN + 1];
static u8   slen;
static s8   kb_r, kb_c;
static bool kb_sp;
static s8   sp_r;
static u32  res_first, res_total;
static u8   res_page, res_sel;

static const char *const kb_rows[4] = {
    "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"
};
static const u8 kb_lens[4] = {10, 10, 9, 7};
static const char *const sp_label[N_SP] = {"DEL","CLR","ADD"};

/* ── Platform: VDP-equivalent draw helpers ──────────────────────────────── */
static void sim_setpal(int pal)
{
    wattrset(scr, COLOR_PAIR(pal));
}

/* Draw text at (col, row) — clipped to 40 columns. */
static void sim_draw(const char *txt, int col, int row)
{
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) return;
    int maxlen = SCREEN_COLS - col;
    char tmp[42];
    int i = 0;
    while (txt[i] && i < maxlen) { tmp[i] = txt[i]; i++; }
    tmp[i] = '\0';
    mvwaddstr(scr, row, col, tmp);
}

static void sim_clear_row(int row)
{
    sim_setpal(PAL0);
    sim_draw("                                        ", 0, row);
}

/* ── Palette assignment (same logic as Genesis) ─────────────────────────── */
static void update_ticker_pal(u8 i)
{
    const char *s = tickers[i].symbol;
    u8 pal = PAL0;
    if      (s[0]=='O'&&s[1]=='R'&&s[2]=='C'&&s[3]=='L'&&!s[4]) pal=PAL2;
    else if (s[0]=='N'&&s[1]=='V'&&s[2]=='D'&&s[3]=='A'&&!s[4]) pal=PAL1;
    else if (s[0]=='I'&&s[1]=='B'&&s[2]=='M'&&!s[3])             pal=PAL3;
    else if (s[0]=='M'&&s[1]=='S'&&s[2]=='F'&&s[3]=='T'&&!s[4]) pal=PAL3;
    else if (s[0]=='M'&&s[1]=='E'&&s[2]=='T'&&s[3]=='A'&&!s[4]) pal=PAL1;
    else if (s[0]=='G'&&s[1]=='O'&&s[2]=='O'&&s[3]=='G'&&s[4]=='L'&&!s[5]) pal=PAL0;
    ticker_sym_pal[i] = pal;
}

static bool is_googl(u8 i)
{
    const char *s = tickers[i].symbol;
    return s[0]=='G'&&s[1]=='O'&&s[2]=='O'&&s[3]=='G'&&s[4]=='L'&&!s[5];
}

/* GOOGL: per-letter brand colours  G=cyan  O=red  O=white  G=cyan  L=green */
static void draw_googl(int col, int row)
{
    static const char letters[] = "GOOGL";
    static const int  pals[]    = { PAL3, PAL2, PAL0, PAL3, PAL1 };
    char ch[2] = {0, 0};
    int i;
    for (i = 0; i < 5; i++) {
        ch[0] = letters[i];
        sim_setpal(pals[i]);
        sim_draw(ch, col + i, row);
    }
}

/* ── format_net_worth (identical to Genesis) ────────────────────────────── */
static void format_net_worth(char *buf, s64 total_cents)
{
    u32 bil, rem, mil, tho, hun;
    s64 dollars;
    if (total_cents < 0) total_cents = 0;
    dollars = total_cents / 100;
    bil = (u32)(dollars / 1000000000LL);
    rem = (u32)(dollars % 1000000000LL);
    mil = rem / 1000000;
    tho = (rem / 1000) % 1000;
    hun = rem % 1000;
    if (bil > 0)
        sprintf(buf, "$%u,%03u,%03u,%03u", bil, mil, tho, hun);
    else if (mil > 0)
        sprintf(buf, "$%u,%03u,%03u", mil, tho, hun);
    else if (tho > 0)
        sprintf(buf, "$%u,%03u", tho, hun);
    else
        sprintf(buf, "$%u", hun);
}

/* ── Status line ─────────────────────────────────────────────────────────── */
static void status_clear(void)
{
    sim_setpal(PAL0);
    sim_draw("                                        ", 0, STATUS_ROW);
}

static void status_err(const char *msg)
{
    char buf[42];
    int i = 0;
    while (msg[i] && i < 39) { buf[i] = msg[i]; i++; }
    while (i < 40) buf[i++] = ' ';
    buf[40] = '\0';
    sim_setpal(PAL2);
    sim_draw(buf, 0, STATUS_ROW);
    sim_setpal(PAL0);
}

static void status_info(const char *msg)
{
    char buf[42];
    int i = 0;
    while (msg[i] && i < 39) { buf[i] = msg[i]; i++; }
    while (i < 40) buf[i++] = ' ';
    buf[40] = '\0';
    sim_setpal(PAL0);
    sim_draw(buf, 0, STATUS_ROW);
    wrefresh(scr);
}

/* ── draw_title ──────────────────────────────────────────────────────────── */
static void draw_title(void)
{
    sim_setpal(PAL0);
    sim_draw("[ MegaWifi Stock Ticker (sim)  ]        ", 0, 0);
    sim_setpal(PAL2); /* purple-ish via red — Genesis uses a custom colour */
    sim_draw("(c)2026 Mike Wolak", COPYRIGHT_COL, COPYRIGHT_ROW);
    sim_setpal(PAL0);
}

/* ── draw_countdown ──────────────────────────────────────────────────────── */
static void draw_countdown(void)
{
    char buf[42];
    time_t now = time(NULL);
    int secs = UPDATE_SECS - (int)(now - last_fetch_time);
    if (secs < 0) secs = 0;
    if (last_update_str[0])
        sprintf(buf, "Next Update:%2ds %s  ", secs, last_update_str);
    else
        sprintf(buf, "Next Update: %2ds                    ", secs);
    sim_setpal(PAL0);
    sim_draw(buf, COUNTDOWN_COL, COUNTDOWN_ROW);
}

/* ── draw_net_worth ──────────────────────────────────────────────────────── */
static void draw_net_worth(void)
{
    s64  total = 0;
    char amtbuf[20];
    char line[42];
    u8   i, used;

    pthread_mutex_lock(&data_mutex);
    for (i = 0; i < MAX_TICKERS; i++) {
        if (tickers[i].valid && tickers[i].shares_owned > 0)
            total += (s64)tickers[i].price_cents * (s64)tickers[i].shares_owned;
    }
    pthread_mutex_unlock(&data_mutex);

    format_net_worth(amtbuf, total);
    sim_setpal(PAL0);
    sim_draw("Net Worth: ", 0, NET_WORTH_ROW);
    sim_setpal(PAL3);
    sim_draw(amtbuf, 11, NET_WORTH_ROW);
    used = (u8)(11 + strlen(amtbuf));
    for (i = 0; i < (u8)(40 - used) && i < (u8)(sizeof(line) - 1); i++) line[i] = ' ';
    line[i] = '\0';
    sim_setpal(PAL0);
    if (i > 0) sim_draw(line, used, NET_WORTH_ROW);
}

/* ── draw_price_row (identical layout to Genesis) ───────────────────────── */
/*
 * col  0      cursor '>' if selected
 * col  1-5    symbol (company color)
 * col  6-15   price  "$X.XX     " 10 chars (white)
 * col 16-29   delta  " +X.XX(+X.XX%)" 14 chars (green/red)
 * col 30-39   timestamp "HH:MM AM  " or share count (cyan)
 */
static void draw_price_row(u8 i)
{
    char pricebuf[16];
    char deltabuf[20];
    char rightbuf[12];
    u8   n;
    int  row = PRICE_START_ROW + i * PRICE_ROW_STRIDE;
    bool sel;
    TickerEntry t;

    pthread_mutex_lock(&data_mutex);
    t = tickers[i];   /* struct copy under lock */
    pthread_mutex_unlock(&data_mutex);

    sel = (selected == (s8)i);

    if (t.symbol[0] == '\0') {
        sim_setpal(PAL0);
        sim_draw("                                        ", 0, row);
        return;
    }

    sim_setpal(sel ? PAL1 : PAL0);
    sim_draw(sel ? ">" : " ", 0, row);

    if (is_googl(i)) {
        draw_googl(1, row);
    } else {
        u8 slen_s;
        sim_setpal(ticker_sym_pal[i]);
        sim_draw(t.symbol, 1, row);
        slen_s = (u8)strlen(t.symbol);
        if (slen_s < 5) {
            char pad[6] = "     ";
            pad[5 - slen_s] = '\0';
            sim_setpal(PAL0);
            sim_draw(pad, 1 + slen_s, row);
        }
    }

    if (!t.valid) {
        sim_setpal(PAL0);
        sim_draw("  Loading...              ", 6, row);
        return;
    }

    {
        s32 price  = t.price_cents;
        s32 pc     = t.prev_close_cents;
        s32 delta  = price - pc;
        char sign  = (delta >= 0) ? '+' : '-';
        s32 adelta = (delta < 0) ? -delta : delta;
        s32 pct    = pc ? (adelta * 10000L / pc) : 0;

        /* price — 10 chars fixed */
        n = (u8)sprintf(pricebuf, "$%ld.%02ld",
                        (long)(price / 100), (long)(price % 100));
        while (n < 10) pricebuf[n++] = ' ';
        pricebuf[10] = '\0';
        sim_setpal(PAL0);
        sim_draw(pricebuf, 6, row);

        /* delta — 14 chars fixed */
        n = (u8)sprintf(deltabuf, " %c%ld.%02ld(%c%ld.%02ld%%)",
                        sign, (long)(adelta / 100), (long)(adelta % 100),
                        sign, (long)(pct / 100),    (long)(pct % 100));
        while (n < 14) deltabuf[n++] = ' ';
        deltabuf[14] = '\0';
        sim_setpal((delta < 0) ? PAL2 : PAL1);
        sim_draw(deltabuf, 16, row);

        /* right column (col 30-39) */
        if (sel) {
            u32 sh = t.shares_owned;
            char lbr = editing ? '[' : ' ';
            char rbr = editing ? ']' : ' ';
            if (sh >= 100000)
                sprintf(rightbuf, "%c%6lu%c   ", lbr, (unsigned long)sh, rbr);
            else if (sh >= 10000)
                sprintf(rightbuf, "%c%5lu%c    ", lbr, (unsigned long)sh, rbr);
            else
                sprintf(rightbuf, "%c%4lu%c     ", lbr, (unsigned long)sh, rbr);
            sim_setpal(PAL3);
            sim_draw(rightbuf, 30, row);
        } else {
            sim_setpal(PAL0);
            sim_draw(t.timestamp, 30, row);
            sim_draw("  ", 38, row);
        }
    }
}

static void draw_price_panel(void)
{
    u8 i;
    for (i = 0; i < MAX_TICKERS; i++)
        draw_price_row(i);
    draw_net_worth();
}

/* ── ticker_get_slot_sym / ticker_is_duplicate / ticker_set_slot ─────────── */
const char *ticker_get_slot_sym(u8 slot)
{
    return (slot < MAX_TICKERS) ? tickers[slot].symbol : "";
}

bool ticker_is_duplicate(const char *sym)
{
    u8 i;
    for (i = 0; i < MAX_TICKERS; i++) {
        const char *s = tickers[i].symbol;
        u8 j;
        if (!s || !s[0]) continue;
        for (j = 0; sym[j] && s[j] && sym[j] == s[j]; j++) {}
        if (!sym[j] && !s[j]) return true;
    }
    return false;
}

void ticker_set_slot(u8 slot, const char *sym)
{
    u8 j;
    if (slot >= MAX_TICKERS) return;
    pthread_mutex_lock(&data_mutex);
    {
        bool was_empty = (tickers[slot].symbol[0] == '\0');
        for (j = 0; j < 7 && sym[j]; j++) tickers[slot].symbol[j] = sym[j];
        tickers[slot].symbol[j]        = '\0';
        tickers[slot].valid            = false;
        tickers[slot].price_cents      = 0;
        tickers[slot].prev_close_cents = 0;
        tickers[slot].timestamp[0]     = '\0';
        if (was_empty) tickers[slot].shares_owned = 0;
    }
    pthread_mutex_unlock(&data_mutex);
    update_ticker_pal(slot);
}

/* ── NASDAQ binary search ────────────────────────────────────────────────── */
static int ts_strncmp(const char *a, const char *b, u8 n)
{
    while (n--) {
        if ((u8)*a != (u8)*b) return (u8)*a - (u8)*b;
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}

static const char *rec_sym_ptr(u32 i)  { return nasdaq_data + i * REC_SIZE; }
static const char *rec_name_ptr(u32 i) { return nasdaq_data + i * REC_SIZE + NAME_OFF; }

static u32 lower_bound(const char *pfx, u8 len)
{
    u32 lo = 0, hi = nasdaq_total;
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        if (ts_strncmp(rec_sym_ptr(mid), pfx, len) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void run_search(void)
{
    u32 i;
    if (slen == 0) {
        res_first = 0;
        res_total = nasdaq_total;
    } else {
        res_first = lower_bound(sbuf, slen);
        i = res_first;
        while (i < nasdaq_total && ts_strncmp(rec_sym_ptr(i), sbuf, slen) == 0)
            i++;
        res_total = i - res_first;
    }
    res_page = 0;
    res_sel  = 0;
}

/* ── Ticker search draw ───────────────────────────────────────────────────── */
static void ts_draw_keyboard(void)
{
    u8 r, c;
    char ch[2] = {0, 0};
    for (r = 0; r < 4; r++) {
        for (c = 0; c < kb_lens[r]; c++) {
            bool sel = (ts_mode == MODE_KB) && !kb_sp
                       && kb_r == (s8)r && kb_c == (s8)c;
            sim_setpal(sel ? PAL1 : PAL0);
            ch[0] = kb_rows[r][c];
            sim_draw(ch, KB_COL + c * 2, KB_ROW0 + r);
        }
    }
    for (r = 0; r < N_SP; r++) {
        bool sel = (ts_mode == MODE_KB) && kb_sp && sp_r == (s8)r;
        sim_setpal(sel ? PAL1 : PAL0);
        sim_draw(sp_label[r], SP_COL, KB_ROW0 + r);
    }
    sim_setpal(PAL0);
}

static void ts_draw_search_box(void)
{
    char line[42];
    const char *cur = ticker_get_slot_sym(ts_target_slot);
    sprintf(line, "Type: %-6s_  replacing slot %u: %-5s",
            sbuf, (unsigned)(ts_target_slot + 1), cur[0] ? cur : "(empty)");
    sim_setpal(PAL0);
    sim_draw(line, 1, SEARCH_ROW);
}

static void ts_draw_results(void)
{
    char line[42];
    char sym[7];
    char ntrunc[25];
    u32  base        = res_first + (u32)res_page * N_RESULTS;
    u8   total_pages = res_total ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
    u8   i;

    for (i = 0; i < N_RESULTS; i++) {
        u32  idx = base + i;
        bool sel = (ts_mode == MODE_RESULTS) && (res_sel == i);
        u8   j;

        sim_setpal(PAL0);
        sim_draw("                                        ", 0, RES_ROW0 + i);

        if (idx >= res_first + res_total) continue;

        for (j = 0; j < SYM_LEN; j++)
            sym[j] = rec_sym_ptr(idx)[j] ? rec_sym_ptr(idx)[j] : ' ';
        sym[6] = '\0';

        for (j = 0; j < 24 && rec_name_ptr(idx)[j]; j++)
            ntrunc[j] = rec_name_ptr(idx)[j];
        ntrunc[j] = '\0';

        sprintf(line, "%-6s  %s", sym, ntrunc);
        sim_setpal(sel ? PAL1 : PAL0);
        sim_draw(line, 1, RES_ROW0 + i);
    }

    sim_setpal(PAL0);
    sprintf(line, "pg %u/%u  matches:%-5lu  [C=switch A=set]",
            (unsigned)(res_page + 1), (unsigned)total_pages,
            (unsigned long)res_total);
    sim_draw(line, 1, PAGE_ROW);
}

static void ts_draw_hint(void)
{
    sim_setpal(PAL0);
    sim_draw("KB:A=type B=del C=results | RES:A=set B/C=kb", 0, HINT_ROW);
}

static void ts_draw_popup(void)
{
    char header[48]; /* 44 chars + null for 1-digit slot */
    int  r;
    for (r = POP_TOP; r <= POP_BOT; r++) sim_clear_row(r);
    sprintf(header, "--[ TICKER SEARCH: slot %u ]--- START=close--",
            (unsigned)(ts_target_slot + 1));
    sim_setpal(PAL1);
    sim_draw(header, 0, POP_TOP);
    sim_setpal(PAL0);
    sim_draw("-----------------------------------------", 0, SEP1_ROW);
    sim_draw("-----------------------------------------", 0, SEP2_ROW);
    ts_draw_search_box();
    ts_draw_results();
    ts_draw_keyboard();
    ts_draw_hint();
}

/* ── Ticker search actions ───────────────────────────────────────────────── */
static u8 ts_page_count(void)
{
    u8 total_pages = res_total ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
    if (res_page == total_pages - 1) {
        u32 rem = res_total - (u32)res_page * N_RESULTS;
        return (rem < N_RESULTS) ? (u8)rem : N_RESULTS;
    }
    return N_RESULTS;
}

static void ts_close(void)
{
    u8 i;
    ts_active = false;
    draw_title();
    draw_countdown();
    for (i = 0; i < MAX_TICKERS; i++) draw_price_row(i);
    draw_net_worth();
}

static void ts_set_ticker_at(u32 idx)
{
    char sym[8];
    u8   j;
    for (j = 0; j < SYM_LEN; j++) sym[j] = rec_sym_ptr(idx)[j];
    sym[SYM_LEN] = '\0';
    for (j = SYM_LEN; j > 0 && sym[j-1] == '\0'; j--) sym[j-1] = '\0';

    if (ticker_is_duplicate(sym)) {
        const char *cur = ticker_get_slot_sym(ts_target_slot);
        bool same = true;
        for (j = 0; j <= SYM_LEN; j++) {
            if (sym[j] != cur[j]) { same = false; break; }
        }
        if (!same) {
            ts_draw_search_box();
            ts_draw_results();
            return;
        }
    }
    ticker_set_slot(ts_target_slot, sym);
    ts_close();
}

void ticker_search_open(u8 slot)
{
    ts_target_slot = slot;
    slen = 0; sbuf[0] = '\0';
    kb_r = 1; kb_c = 0;
    kb_sp = false; sp_r = 0;
    ts_mode = MODE_KB;
    if (nasdaq_data) run_search();
    ts_active = true;
    ts_draw_popup();
}

bool ticker_search_active(void) { return ts_active; }

void ticker_search_frame(u16 press)
{
    if (!ts_active) return;
    if (press & BUTTON_START) { ts_close(); return; }

    if (ts_mode == MODE_KB) {
        s8 nr, nc;
        if (press & BUTTON_UP) {
            if (kb_sp) {
                sp_r = (s8)((sp_r - 1 + N_SP) % N_SP);
            } else {
                nr = (s8)((kb_r - 1 + 4) % 4);
                nc = kb_c;
                if (nc >= (s8)kb_lens[(u8)nr]) nc = (s8)(kb_lens[(u8)nr] - 1);
                kb_r = nr; kb_c = nc;
            }
            ts_draw_keyboard();
        } else if (press & BUTTON_DOWN) {
            if (kb_sp) {
                sp_r = (s8)((sp_r + 1) % N_SP);
            } else {
                nr = (s8)((kb_r + 1) % 4);
                nc = kb_c;
                if (nc >= (s8)kb_lens[(u8)nr]) nc = (s8)(kb_lens[(u8)nr] - 1);
                kb_r = nr; kb_c = nc;
            }
            ts_draw_keyboard();
        } else if (press & BUTTON_LEFT) {
            if (kb_sp) {
                kb_sp = false;
            } else {
                nc = (s8)(kb_c - 1);
                if (nc >= 0) kb_c = nc;
            }
            ts_draw_keyboard();
        } else if (press & BUTTON_RIGHT) {
            if (!kb_sp) {
                nc = (s8)(kb_c + 1);
                if (nc >= (s8)kb_lens[(u8)kb_r]) {
                    kb_sp = true;
                    sp_r  = (kb_r < N_SP) ? kb_r : 0;
                } else {
                    kb_c = nc;
                }
                ts_draw_keyboard();
            }
        } else if (press & BUTTON_A) {
            if (kb_sp) {
                switch (sp_r) {
                case 0: /* DEL */
                    if (slen > 0) { sbuf[--slen] = '\0'; run_search();
                                    ts_draw_search_box(); ts_draw_results(); }
                    break;
                case 1: /* CLR */
                    slen = 0; sbuf[0] = '\0'; run_search();
                    ts_draw_search_box(); ts_draw_results();
                    break;
                case 2: /* ADD */
                    if (res_total > 0) ts_set_ticker_at(res_first);
                    break;
                }
            } else {
                if (slen < TS_SYM_LEN) {
                    sbuf[slen++] = kb_rows[(u8)kb_r][(u8)kb_c];
                    sbuf[slen]   = '\0';
                    run_search();
                    ts_draw_search_box();
                    ts_draw_results();
                }
            }
        } else if (press & BUTTON_B) {
            if (slen > 0) {
                sbuf[--slen] = '\0';
                run_search();
                ts_draw_search_box();
                ts_draw_results();
            }
        } else if (press & BUTTON_C) {
            ts_mode = MODE_RESULTS;
            ts_draw_results();
            ts_draw_keyboard();
        }
    } else { /* MODE_RESULTS */
        u8 cnt;
        if (press & BUTTON_UP) {
            cnt = ts_page_count();
            if (cnt > 0) { res_sel = (u8)((res_sel - 1 + cnt) % cnt); ts_draw_results(); }
        } else if (press & BUTTON_DOWN) {
            cnt = ts_page_count();
            if (cnt > 0) { res_sel = (u8)((res_sel + 1) % cnt); ts_draw_results(); }
        } else if (press & BUTTON_LEFT) {
            u8 tp = res_total ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
            res_page = (u8)((res_page - 1 + tp) % tp);
            res_sel  = 0;
            ts_draw_results();
        } else if (press & BUTTON_RIGHT) {
            u8 tp = res_total ? (u8)((res_total + N_RESULTS - 1) / N_RESULTS) : 1;
            res_page = (u8)((res_page + 1) % tp);
            res_sel  = 0;
            ts_draw_results();
        } else if (press & BUTTON_A) {
            u32 idx = res_first + (u32)res_page * N_RESULTS + res_sel;
            if (idx < res_first + res_total) ts_set_ticker_at(idx);
        } else if ((press & BUTTON_B) || (press & BUTTON_C)) {
            ts_mode = MODE_KB;
            ts_draw_results();
            ts_draw_keyboard();
        }
    }
}

/* ── Marquee ──────────────────────────────────────────────────────────────── */
static void rebuild_marquee(void)
{
    char *p  = marquee_str;
    u8   *pp = marquee_pal_arr;
    u8    i;

    pthread_mutex_lock(&data_mutex);
    for (i = 0; i < MAX_TICKERS; i++) {
        u8   sym_len, white_len;
        int  n, j;
        if (!tickers[i].symbol[0]) continue;
        sym_len   = (u8)strlen(tickers[i].symbol);
        white_len = 1 + sym_len + 1;

        if (!tickers[i].valid) {
            n = sprintf(p, " %s:---.- ", tickers[i].symbol);
            for (j = 0; j < n; j++) pp[j] = PAL0;
            p += n; pp += n;
            continue;
        }
        {
            s32  price  = tickers[i].price_cents;
            s32  pc     = tickers[i].prev_close_cents;
            s32  delta  = price - pc;
            char sign   = (delta >= 0) ? '+' : '-';
            s32  adelta = (delta < 0) ? -delta : delta;
            u8   pal    = (delta >= 0) ? PAL1 : PAL2;

            n = sprintf(p, " %s:$%ld.%02ld(%c%ld.%02ld) ",
                        tickers[i].symbol,
                        (long)(price / 100), (long)(price % 100),
                        sign, (long)(adelta / 100), (long)(adelta % 100));
            for (j = 0; j < white_len && j < n; j++) pp[j] = PAL0;
            for (     ; j < n;                 j++) pp[j] = pal;
            p += n; pp += n;
        }
    }
    pthread_mutex_unlock(&data_mutex);
    *p = '\0'; *pp = PAL0;
    marquee_pos = 0;
}

static void draw_marquee(void)
{
    int   slen_m = (int)strlen(marquee_str);
    char  ch[2]  = {0, 0};
    int   i;
    if (slen_m == 0) return;
    for (i = 0; i < SCREEN_COLS; i++) {
        int idx = ((int)marquee_pos + i) % slen_m;
        sim_setpal(marquee_pal_arr[idx]);
        ch[0] = marquee_str[idx];
        sim_draw(ch, i, MARQUEE_ROW);
    }
}

/* ── HTTP fetch via libcurl ──────────────────────────────────────────────── */
struct curl_buf { char *data; size_t len; };

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    struct curl_buf *buf = (struct curl_buf *)ud;
    size_t add = size * nmemb;
    buf->data = realloc(buf->data, buf->len + add + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = '\0';
    return add;
}

static const char *find_substr_local(const char *hay, const char *needle)
{
    if (!*needle) return hay;
    while (*hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
        hay++;
    }
    return NULL;
}

static s32 parse_json_price_cents(const char *body, const char *key)
{
    const char *p = find_substr_local(body, key);
    long ipart, frac;
    int  sign, fdig;
    if (!p) return 0x7FFFFFFF;
    while (*p && *p != ':') p++;
    if (!*p) return 0x7FFFFFFF;
    p++;
    while (*p == ' ') p++;
    sign = 1;
    if (*p == '-') { sign = -1; p++; }
    ipart = 0;
    while (*p >= '0' && *p <= '9') { ipart = ipart * 10 + (*p - '0'); p++; }
    frac = 0; fdig = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9' && fdig < 2) { frac = frac * 10 + (*p - '0'); fdig++; p++; }
        while (*p >= '0' && *p <= '9') p++;
    }
    if (fdig == 1) frac *= 10;
    return (s32)(sign * (ipart * 100 + frac));
}

static void fetch_quote_https(u8 idx)
{
    char url[160];
    char sym[8];
    struct curl_buf resp = {NULL, 0};
    CURL *curl;
    CURLcode res;
    long http_status = 0;
    s32  price, prev;

    pthread_mutex_lock(&data_mutex);
    strncpy(sym, tickers[idx].symbol, 7); sym[7] = '\0';
    pthread_mutex_unlock(&data_mutex);

    if (!sym[0]) return;

    sprintf(url, "https://finnhub.io/api/v1/quote?symbol=%s&token=%s",
            sym, finnhub_token);

    curl = curl_easy_init();
    if (!curl) { status_err("curl init failed"); return; }

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_status < 200 || http_status >= 300 || !resp.data) {
        char dbg[42];
        sprintf(dbg, "[%-5s] curl=%d http=%ld", sym, (int)res, http_status);
        status_err(dbg);
        free(resp.data);
        return;
    }

    price = parse_json_price_cents(resp.data, "\"c\"");
    prev  = parse_json_price_cents(resp.data, "\"pc\"");
    free(resp.data);

    if (price == 0x7FFFFFFF || prev == 0x7FFFFFFF) {
        char dbg[42];
        sprintf(dbg, "[%-5s] JSON parse failed", sym);
        status_err(dbg);
        return;
    }

    status_clear();

    /* Timestamp from local clock */
    {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        char ts[12];
        int hh = lt->tm_hour;
        const char *ampm = (hh >= 12) ? "PM" : "AM";
        if (hh == 0) hh = 12; else if (hh > 12) hh -= 12;
        sprintf(ts, "%2d:%02d %s", hh, lt->tm_min, ampm);

        pthread_mutex_lock(&data_mutex);
        tickers[idx].price_cents      = price;
        tickers[idx].prev_close_cents = prev;
        tickers[idx].valid            = true;
        memcpy(tickers[idx].timestamp, ts, 12);
        sprintf(last_update_str, "%02d/%02d/%02d %d:%02d:%02d %s",
                lt->tm_mon + 1, lt->tm_mday, lt->tm_year % 100,
                hh, lt->tm_min, lt->tm_sec, ampm);
        pthread_mutex_unlock(&data_mutex);
    }
}

/* ── Load helpers ────────────────────────────────────────────────────────── */
static void load_token(void)
{
    FILE *f = fopen("../TOKEN.md", "r");
    if (!f) f = fopen("TOKEN.md", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Token:", 6) == 0) {
            char *p = line + 6;
            int   i = 0;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != '\n' && *p != '\r' && i < 127)
                finnhub_token[i++] = *p++;
            finnhub_token[i] = '\0';
            break;
        }
    }
    fclose(f);
}

static void load_nasdaq_db(void)
{
    FILE *f = fopen("../nasdaq_tickers.bin", "rb");
    if (!f) f = fopen("nasdaq_tickers.bin", "rb");
    if (!f) { return; } /* no DB → search popup disabled */
    fseek(f, 0, SEEK_END);
    {
        long sz = ftell(f);
        rewind(f);
        nasdaq_data = malloc((size_t)sz + 1);
        if (!nasdaq_data) { fclose(f); return; }
        fread(nasdaq_data, 1, (size_t)sz, f);
        fclose(f);
        nasdaq_total = (u32)((size_t)sz / REC_SIZE);
    }
}

/* ── Input mapping ───────────────────────────────────────────────────────── */
static u16 poll_input(void)
{
    int ch = wgetch(scr);
    if (ch == 'q' || ch == 'Q') return 0xFFFFu;
    switch (ch) {
    case KEY_UP:                    return BUTTON_UP;
    case KEY_DOWN:                  return BUTTON_DOWN;
    case KEY_LEFT:                  return BUTTON_LEFT;
    case KEY_RIGHT:                 return BUTTON_RIGHT;
    case 'a': case 'A':             return BUTTON_A;
    case 'b': case 'B':
    case KEY_BACKSPACE: case 127:   return BUTTON_B;
    case 'c': case 'C':             return BUTTON_C;
    case 's': case 'S':
    case '\n': case KEY_ENTER:      return BUTTON_START;
    }
    return 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    int term_rows, term_cols;
    int win_y, win_x;
    u8  i;
    u32 frame_num = 0;
    struct timeval last_tv, now_tv;

    /* ── ncurses init ─────────────────────────────────────────────────── */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors\n");
        return 1;
    }
    start_color();
    init_pair(PAL0, COLOR_WHITE,  COLOR_BLACK);
    init_pair(PAL1, COLOR_GREEN,  COLOR_BLACK);
    init_pair(PAL2, COLOR_RED,    COLOR_BLACK);
    init_pair(PAL3, COLOR_CYAN,   COLOR_BLACK);

    getmaxyx(stdscr, term_rows, term_cols);
    if (term_rows < SCREEN_ROWS + 2 || term_cols < SCREEN_COLS + 2) {
        endwin();
        fprintf(stderr, "Terminal too small: need %dx%d, have %dx%d\n",
                SCREEN_COLS + 2, SCREEN_ROWS + 2, term_cols, term_rows);
        return 1;
    }

    win_y = (term_rows - SCREEN_ROWS - 2) / 2;
    win_x = (term_cols - SCREEN_COLS - 2) / 2;

    border_win = newwin(SCREEN_ROWS + 2, SCREEN_COLS + 2, win_y, win_x);
    wbkgd(border_win, COLOR_PAIR(PAL0));
    box(border_win, 0, 0);
    /* Key legend in border */
    mvwaddstr(border_win, SCREEN_ROWS + 1, 1,
              " arrows=dpad  a=A  b=B  c=C  s=START  q=quit ");
    wrefresh(border_win);

    scr = newwin(SCREEN_ROWS, SCREEN_COLS, win_y + 1, win_x + 1);
    keypad(scr, TRUE);
    wtimeout(scr, 0);   /* non-blocking */
    wbkgd(scr, COLOR_PAIR(PAL0));

    /* ── App init ─────────────────────────────────────────────────────── */
    load_token();
    load_nasdaq_db();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    for (i = 0; i < MAX_TICKERS; i++) {
        u8 j;
        for (j = 0; j < 7 && default_syms[i][j]; j++)
            tickers[i].symbol[j] = default_syms[i][j];
        tickers[i].symbol[j]        = '\0';
        tickers[i].price_cents      = 0;
        tickers[i].prev_close_cents = 0;
        tickers[i].valid            = false;
        tickers[i].timestamp[0]     = '\0';
        tickers[i].shares_owned     = 0;
        update_ticker_pal(i);
    }

    /* Random share counts (1-100,000), seeded by clock same as Genesis */
    srand((unsigned)time(NULL));
    for (i = 0; i < MAX_TICKERS; i++) {
        u32 r = ((u32)rand() << 16) | (u32)rand();
        tickers[i].shares_owned = (r % 100000) + 1;
    }

    /* ── Initial display and fetch ─────────────────────────────────────── */
    draw_title();
    wrefresh(scr);

    for (i = 0; i < MAX_TICKERS; i++) {
        char msg[42];
        sprintf(msg, "Fetching %s (%d/%d)...", tickers[i].symbol, i + 1, MAX_TICKERS);
        status_info(msg);
        fetch_quote_https(i);
        draw_price_row(i);
        wrefresh(scr);
    }

    rebuild_marquee();
    draw_price_panel();
    draw_countdown();
    draw_marquee();
    last_fetch_time = time(NULL);
    wrefresh(scr);

    /* ── Main loop — ~60 fps ────────────────────────────────────────────── */
    gettimeofday(&last_tv, NULL);

    while (1) {
        long elapsed_us;
        long sleep_us;
        u16  press;
        time_t now;

        /* pace to 60fps */
        gettimeofday(&now_tv, NULL);
        elapsed_us = (now_tv.tv_sec  - last_tv.tv_sec)  * 1000000L
                   + (now_tv.tv_usec - last_tv.tv_usec);
        sleep_us = 16667L - elapsed_us;
        if (sleep_us > 0) usleep((useconds_t)sleep_us);
        gettimeofday(&last_tv, NULL);

        /* ── Input ──────────────────────────────────────────────────── */
        press = poll_input();
        if (press == 0xFFFFu) break;

        /* START: open popup — if/else prevents same-frame open+close */
        if ((press & BUTTON_START) && !ticker_search_active()) {
            editing = false;
            ticker_search_open((u8)(selected >= 0 ? (u8)selected : 0));
        } else {
            ticker_search_frame(press);
        }

        if (ticker_search_active()) {
            wrefresh(scr);
            frame_num++;
            continue;
        }

        /* ── Selection and share-edit controls ──────────────────────── */
        if (editing) {
            if (press & BUTTON_UP) {
                tickers[(u8)selected].shares_owned += 100;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_DOWN) {
                if (tickers[(u8)selected].shares_owned >= 100)
                    tickers[(u8)selected].shares_owned -= 100;
                else
                    tickers[(u8)selected].shares_owned = 0;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_RIGHT) {
                tickers[(u8)selected].shares_owned += 1000;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_LEFT) {
                if (tickers[(u8)selected].shares_owned >= 1000)
                    tickers[(u8)selected].shares_owned -= 1000;
                else
                    tickers[(u8)selected].shares_owned = 0;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_A) {
                tickers[(u8)selected].shares_owned += 1;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_B) {
                if (tickers[(u8)selected].shares_owned > 0)
                    tickers[(u8)selected].shares_owned--;
                draw_price_row((u8)selected); draw_net_worth();
            } else if (press & BUTTON_C) {
                editing = false;
                draw_price_row((u8)selected);
            }
        } else {
            if (press & BUTTON_UP) {
                u8 prev_sel = (u8)(selected >= 0 ? (u8)selected : 0);
                selected = (s8)((selected <= 0) ? MAX_TICKERS - 1 : selected - 1);
                draw_price_row(prev_sel);
                draw_price_row((u8)selected);
            } else if (press & BUTTON_DOWN) {
                u8 prev_sel = (u8)(selected >= 0 ? (u8)selected : MAX_TICKERS - 1);
                selected = (s8)((selected < 0 || selected >= MAX_TICKERS - 1) ? 0 : selected + 1);
                draw_price_row(prev_sel);
                draw_price_row((u8)selected);
            } else if ((press & BUTTON_A) && selected >= 0) {
                if (tickers[(u8)selected].symbol[0]) {
                    editing = true;
                    draw_price_row((u8)selected);
                }
            }
        }

        /* Rolling price row redraw (1 per frame) */
        if (!editing)
            draw_price_row((u8)(frame_num % MAX_TICKERS));

        /* Marquee: scroll 1 char per 8 frames (matches Genesis 1px/frame at 8px/char) */
        if (frame_num % 8 == 0) {
            int slen_m = (int)strlen(marquee_str);
            if (slen_m > 0)
                marquee_pos = (u16)((marquee_pos + 1) % slen_m);
            draw_marquee();
        }

        /* Countdown: update once per second (60 frames) */
        if (frame_num % 60 == 0)
            draw_countdown();

        /* Periodic fetch — one ticker per UPDATE_SECS */
        now = time(NULL);
        if (now - last_fetch_time >= UPDATE_SECS) {
            char msg[42];
            last_fetch_time = now;
            sprintf(msg, "Fetching %s...", tickers[fetch_cycle].symbol);
            status_info(msg);
            fetch_quote_https(fetch_cycle);
            draw_price_row(fetch_cycle);
            rebuild_marquee();
            draw_net_worth();
            draw_countdown();
            fetch_cycle = (u8)((fetch_cycle + 1) % MAX_TICKERS);
        }

        wrefresh(scr);
        frame_num++;
    }

    /* ── Cleanup ─────────────────────────────────────────────────────── */
    curl_global_cleanup();
    free(nasdaq_data);
    delwin(scr);
    delwin(border_win);
    endwin();
    return 0;
}
