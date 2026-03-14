/*
 * stock_ticker.c
 * Sega Genesis stock ticker using SGDK + MegaWiFi ESP32-C3
 *
 * Displays scrolling ticker tape on plane B, static prices on plane A.
 * Polls Finnhub /quote endpoint over HTTPS every ~15 seconds (round-robin).
 *
 * Build: SGDK with mw-api linked in. Set MW_BUFLEN to at least 512.
 *
 * HTTPS NOTE: Finnhub uses TLS. To enable server certificate verification,
 * call mw_http_cert_set() once with the Finnhub CA certificate (PEM format).
 * Without it the ESP32 firmware may accept any cert (depends on fw config).
 * Get the cert hash with:  openssl x509 -hash -in finnhub_ca.pem -noout
 */

#include <genesis.h>
#include <task.h>
#include <string.h>
#include "ext/mw/megawifi.h"
/* no stdlib.h available in SGDK headers; provide minimal prototypes */
long atol(const char *nptr);

static char *find_char(const char *s, char c)
{
    while (*s) { if (*s == c) return (char*)s; s++; }
    return NULL;
}

static const char *find_substr(const char *hay, const char *needle)
{
    if (!*needle) return hay;
    while (*hay) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (*n == '\0') return hay;
        hay++;
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Config
 * ---------------------------------------------------------------------- */
#define MS_TO_FRAMES(ms)    ((((ms) * 60 / 500) + 1) / 2)
#define FPS                 60
#define AP_SLOT             0               /* WiFi config slot              */
#define AP_SSID             "YourSSID"
#define AP_PASS             "YourPassword"
#define UPDATE_FRAMES       (FPS * 5)       /* poll each ticker ~5 s        */
#define MAX_TICKERS         6
#define TICKER_SCROLL_SPEED 1               /* pixels per frame              */
#ifndef FINNHUB_TOKEN
#define FINNHUB_TOKEN   "REPLACE_WITH_YOUR_TOKEN"
#warning "FINNHUB_TOKEN not defined; using placeholder will fail authentication"
#endif
#define FINNHUB_URL_FMT "https://finnhub.io/api/v1/quote?symbol=%s&token=%s"

/* -------------------------------------------------------------------------
 * Ticker data
 * Prices stored as raw cents (s32) to avoid fix16 overflow on high-price
 * stocks (fix16 is a 10.6 signed 16-bit type, max integer = 511).
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *symbol;
    s32         price_cents;     /* current price  × 100 */
    s32         prev_close_cents;/* previous close × 100 */
    bool        valid;
} TickerEntry;

static TickerEntry tickers[MAX_TICKERS] = {
    { "ORCL", 0, 0, false },
    { "NVDA", 0, 0, false },
    { "AMZN", 0, 0, false },
    { "IBM",  0, 0, false },
    { "MSFT", 0, 0, false },
    { "GOOGL",0, 0, false },
};

/* -------------------------------------------------------------------------
 * Scrolling marquee state — double-buffered to avoid partial reads
 * ---------------------------------------------------------------------- */
static char  marquee_buf[2][256]; /* ping-pong buffers                     */
static u8    marquee_read_idx = 0;/* main loop always reads from this index */
static volatile bool marquee_pending = false; /* new data ready to swap in  */
static u16   scroll_x = 0;

/* -------------------------------------------------------------------------
 * MegaWiFi shared command buffer (reused for all commands)
 * ---------------------------------------------------------------------- */
static uint16_t cmd_buf[MW_BUFLEN/2];

/* Finnhub server root CA (GTS Root R4, hash a3418fda) */
static const char finnhub_ca_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
"NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
"Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
"WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
"BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
"BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
"l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
"Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
"Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
"SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
"odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
"+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
"kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
"8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
"vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
"-----END CERTIFICATE-----\n";

static void ensure_finnhub_cert(void)
{
    /* Install GTS Root R4 if not already present on the module */
    uint32_t hash = mw_http_cert_query();
    if (hash != 0xa3418fda) {
        mw_http_cert_set(0xa3418fda, finnhub_ca_pem, strlen(finnhub_ca_pem));
    }
}

/* -------------------------------------------------------------------------
 * Minimal JSON number parser: returns value * 100 (cents) for the field
 * named key (e.g. "\"c\"" or "\"pc\""). Returns 0x7FFFFFFF on failure.
 * ---------------------------------------------------------------------- */
static s32 parse_json_price_cents(const char *body, const char *key)
{
    const char *p = find_substr(body, key);
    if (!p) return 0x7FFFFFFF;
    p = find_char(p, ':');
    if (!p) return 0x7FFFFFFF;
    p++;
    while (*p == ' ') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    long ipart = 0;
    while (*p >= '0' && *p <= '9') { ipart = ipart * 10 + (*p - '0'); p++; }
    long frac = 0; int fdig = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9' && fdig < 2) { frac = frac * 10 + (*p - '0'); fdig++; p++; }
        while (*p >= '0' && *p <= '9') p++; /* skip remaining decimals */
    }
    if (fdig == 1) frac *= 10;
    return sign * (s32)(ipart * 100 + frac);
}
/* -------------------------------------------------------------------------
 * fetch_quote_https()
 * Uses MegaWiFi HTTP client to call Finnhub /quote over HTTPS.
 * ---------------------------------------------------------------------- */
static void fetch_quote_https(u8 idx)
{
    uint8_t ch = MW_HTTP_CH;
    enum mw_err err;
    int16_t recv_len;
    uint32_t content_len = 0;
    char url[160];
    const char *sym = tickers[idx].symbol;

    ensure_finnhub_cert();

    mw_http_cleanup(); /* reset any previous session */

    sprintf(url, FINNHUB_URL_FMT, sym, FINNHUB_TOKEN);

    err = mw_http_url_set(url);
    { char buf[32]; sprintf(buf, "url err=%d           ", (int)err); VDP_drawText(buf, 0, 20); }
    if (err) return;

    err = mw_http_method_set(MW_HTTP_METHOD_GET);
    { char buf[32]; sprintf(buf, "meth err=%d          ", (int)err); VDP_drawText(buf, 0, 21); }
    if (err) return;

    mw_http_header_add("Accept", "application/json");

    err = mw_http_open(0);
    { char buf[32]; sprintf(buf, "open err=%d          ", (int)err); VDP_drawText(buf, 0, 22); }
    if (err) return;

    int16_t status = mw_http_finish(&content_len, MS_TO_FRAMES(12000));
    { char buf[32]; sprintf(buf, "finish=%d len=%ld    ", (int)status, (long)content_len); VDP_drawText(buf, 0, 22); }
    if (status < 200 || status >= 300 || content_len == 0) {
        mw_http_cleanup();
        return;
    }

    recv_len = (content_len < (uint32_t)(MW_BUFLEN - 2)) ? (int16_t)content_len : (int16_t)(MW_BUFLEN - 2);
    err = mw_recv_sync(&ch, (char*)cmd_buf, &recv_len, MS_TO_FRAMES(12000));
    mw_http_cleanup();
    { char buf[32]; sprintf(buf, "recv err=%d len=%d   ", (int)err, (int)recv_len); VDP_drawText(buf, 0, 23); }
    if (err || recv_len <= 0) return;
    ((char*)cmd_buf)[recv_len] = '\0';

    s32 price = parse_json_price_cents((char*)cmd_buf, "\"c\"");
    s32 prev  = parse_json_price_cents((char*)cmd_buf, "\"pc\"");
    if (price == 0x7FFFFFFF || prev == 0x7FFFFFFF) return;

    tickers[idx].price_cents      = price;
    tickers[idx].prev_close_cents = prev;
    tickers[idx].valid            = true;
}
static void fetch_quote(u8 idx)
{
    fetch_quote_https(idx);
}

/* -------------------------------------------------------------------------
 * rebuild_marquee()
 * Assembles the scrolling ticker string into the inactive marquee buffer,
 * then marks it ready for the main loop to swap in.
 * ---------------------------------------------------------------------- */
static void rebuild_marquee(void)
{
    u8   write_idx = 1 - marquee_read_idx; /* write to inactive buffer */
    char *p = marquee_buf[write_idx];
    u8 i;

    for (i = 0; i < MAX_TICKERS; i++) {
        if (!tickers[i].valid) {
            p += sprintf(p, " %s:---.- ", tickers[i].symbol);
            continue;
        }
        {
            s32 price  = tickers[i].price_cents;
            s32 pc     = tickers[i].prev_close_cents;
            s32 delta  = price - pc;
            char sign  = delta >= 0 ? '+' : '-';
            s32 adelta = delta < 0 ? -delta : delta;

            p += sprintf(p, " %s:$%ld.%02ld(%c%ld.%02ld) ",
                         tickers[i].symbol,
                         (long)(price / 100),  (long)(price % 100),
                         sign,
                         (long)(adelta / 100), (long)(adelta % 100));
        }
    }
    *p = '\0';

    marquee_pending = true; /* signal main loop to swap buffer */
}

/* -------------------------------------------------------------------------
 * update_marquee_tiles()
 * Writes the active marquee string into plane B tile row 26 and advances
 * the hardware horizontal scroll register.
 * ---------------------------------------------------------------------- */
static void update_marquee_tiles(void)
{
    const char *buf;
    u16 slen;
    u16 plane_w = 64;
    u16 row     = 26;
    u16 col;

    /* swap in new marquee data if ready */
    if (marquee_pending) {
        marquee_read_idx = 1 - marquee_read_idx;
        marquee_pending  = false;
    }

    buf  = marquee_buf[marquee_read_idx];
    slen = (u16)strlen(buf);
    if (!slen) return;

    for (col = 0; col < plane_w; col++) {
        u8 ch = (u8)buf[col % slen];
        /* assumes font tiles start at TILE_USER_INDEX, ASCII 0x20 = tile 0 */
        u16 tile_idx = (ch >= 0x20 && ch <= 0x7E)
                     ? (TILE_USER_INDEX + (ch - 0x20))
                     : TILE_USER_INDEX;
        VDP_setTileMapXY(BG_B,
            TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, tile_idx),
            col, row);
    }

    scroll_x = (scroll_x + TICKER_SCROLL_SPEED) & 0x1FF;
    VDP_setHorizontalScroll(BG_B, -(s16)scroll_x);
}

/* -------------------------------------------------------------------------
 * draw_price_panel()
 * Redraws the static price list on plane A, rows 2-22.
 * ---------------------------------------------------------------------- */
static void draw_price_panel(void)
{
    char line[40];
    u8 i;

    for (i = 0; i < MAX_TICKERS; i++) {
        u8 row = 2 + i * 4;

        if (!tickers[i].valid) {
            sprintf(line, "%-5s  Loading...", tickers[i].symbol);
            VDP_setTextPalette(PAL0);
            VDP_drawText(line, 1, row);
            continue;
        }

        {
            s32 price  = tickers[i].price_cents;
            s32 pc     = tickers[i].prev_close_cents;
            s32 delta  = price - pc;
            char sign  = delta >= 0 ? '+' : '-';
            s32 adelta = delta < 0 ? -delta : delta;
            /* percent * 100: e.g. 1.23% stored as 123 */
            s32 pct    = pc ? (adelta * 10000L / pc) : 0;

            /* symbol + price in white */
            sprintf(line, "%-5s  $%ld.%02ld",
                    tickers[i].symbol,
                    (long)(price / 100), (long)(price % 100));
            VDP_setTextPalette(PAL0);
            VDP_drawText(line, 1, row);

            /* delta in green (PAL1) or red (PAL2) */
            sprintf(line, "  %c%ld.%02ld  %c%ld.%02ld%%",
                    sign, (long)(adelta / 100), (long)(adelta % 100),
                    sign, (long)(pct / 100),    (long)(pct % 100));
            VDP_setTextPalette(delta >= 0 ? PAL1 : PAL2);
            VDP_drawText(line, 14, row);
            VDP_setTextPalette(PAL0);
        }
    }
}

/* -------------------------------------------------------------------------
 * user_tsk()
 * Runs mw_process() continuously during idle CPU time between VBlanks.
 * This is the ONLY work the user task should do — all synchronous MegaWiFi
 * API calls (mw_http_open, mw_http_finish, mw_recv_sync, etc.) must be
 * issued from the supervisor task (main), because they internally block
 * the supervisor via tsk_super_pend() and yield here for mw_process().
 * ---------------------------------------------------------------------- */
static void user_tsk(void)
{
    while (1) {
        mw_process();
    }
}

/* -------------------------------------------------------------------------
 * megawifi_init()
 * Detects module, programs WiFi credentials to slot 0 (DHCP), associates.
 * ---------------------------------------------------------------------- */
static bool megawifi_init(void)
{
    uint8_t ver_major = 0, ver_minor = 0;
    char *variant = NULL;
    /* All-zeros mw_ip_cfg = DHCP (addr/mask/gw/dns all 0.0.0.0) */
    struct mw_ip_cfg dhcp_cfg = { {0}, {0}, {0}, {0}, {0} };
    enum mw_err err;

    if (mw_init(cmd_buf, MW_BUFLEN) != MW_ERR_NONE) return false;

    err = mw_detect(&ver_major, &ver_minor, &variant);
    if (err != MW_ERR_NONE) return false;

    /* Program credentials — stored in ESP32 NVS, persists across resets */
    err = mw_ap_cfg_set(AP_SLOT, AP_SSID, AP_PASS, MW_PHY_11BGN);
    if (err != MW_ERR_NONE) return false;

    err = mw_ip_cfg_set(AP_SLOT, &dhcp_cfg);
    if (err != MW_ERR_NONE) return false;

    err = mw_cfg_save();
    if (err != MW_ERR_NONE) return false;

    err = mw_ap_assoc(AP_SLOT);
    if (err != MW_ERR_NONE) return false;

    err = mw_ap_assoc_wait(30 * FPS); /* up to 30 seconds */
    if (err != MW_ERR_NONE) return false;

    return true;
}

/* -------------------------------------------------------------------------
 * main()
 * ---------------------------------------------------------------------- */
int main(bool hard_reset)
{
    u16 frame        = UPDATE_FRAMES; /* force first fetch immediately     */
    u8  ticker_cycle = 0;

    /* --- VDP setup ---------------------------------------------------- */
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    /*
     * Load your ASCII font tiles at TILE_USER_INDEX here, e.g.:
     *   VDP_loadFont(&font_data, DMA);
     * The tile at TILE_USER_INDEX + (ch - 0x20) must represent ASCII ch.
     */

    /* --- Palette setup ------------------------------------------------ */
    /* PAL0: background black, text white */
    PAL_setColor(0,  RGB24_TO_VDPCOLOR(0x000000)); /* PAL0 bg  (black)  */
    PAL_setColor(1,  RGB24_TO_VDPCOLOR(0xFFFFFF)); /* PAL0 fg  (white)  */
    /* PAL1: gains green — use color index 1 (not 0 which is transparent) */
    PAL_setColor(16, RGB24_TO_VDPCOLOR(0x000000)); /* PAL1 bg  (black)  */
    PAL_setColor(17, RGB24_TO_VDPCOLOR(0x00EE00)); /* PAL1 fg  (green)  */
    /* PAL2: losses red */
    PAL_setColor(32, RGB24_TO_VDPCOLOR(0x000000)); /* PAL2 bg  (black)  */
    PAL_setColor(33, RGB24_TO_VDPCOLOR(0xFF4040)); /* PAL2 fg  (red)    */

    /* --- Header ------------------------------------------------------- */
    VDP_setTextPalette(PAL0);
    VDP_drawText("  GENESIS STOCK TICKER  ", 4, 0);

    /* --- Wire up user task BEFORE init so mw_process runs during assoc  */
    TSK_userSet(user_tsk);

    /* --- WiFi init ---------------------------------------------------- */
    VDP_drawText("Connecting to WiFi...", 4, 14);
    if (!megawifi_init()) {
        VDP_drawText("WiFi init FAILED       ", 4, 14);
        while (1) VDP_waitVSync();
    }
    VDP_drawText("WiFi connected         ", 4, 14);

    /* --- Initial fetch: get all tickers before entering main loop ----- */
    {
        u8 i;
        VDP_drawText("Fetching quotes...     ", 4, 15);
        for (i = 0; i < MAX_TICKERS; i++) {
            fetch_quote(i);
        }
        rebuild_marquee();
        draw_price_panel();
        VDP_drawText("                       ", 4, 15);
    }

    /* --- Main loop ---------------------------------------------------- */
    while (1) {
        VDP_waitVSync(); /* yields to user_tsk for mw_process()           */

        update_marquee_tiles();

        /* redraw price panel at 2 Hz */
        if ((frame & 31) == 0)
            draw_price_panel();

        frame++;
        if (frame >= UPDATE_FRAMES) {
            frame = 0;
            /* fetch one ticker per cycle (round-robin) */
            fetch_quote(ticker_cycle);
            rebuild_marquee();
            ticker_cycle = (ticker_cycle + 1) % MAX_TICKERS;
        }
    }

    return 0;
}
