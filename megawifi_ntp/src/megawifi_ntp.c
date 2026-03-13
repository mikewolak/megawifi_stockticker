/*
 * megawifi_ntp.c — Sega Genesis NTP clock using SGDK + MegaWiFi
 *
 * Displays US time zones + UTC with DST handling. Build: see ../Makefile
 */

#include <genesis.h>

/* -------------------------------------------------------------------------
 * Config
 * ---------------------------------------------------------------------- */
#define MS_TO_FRAMES(ms)    ((((ms) * 60 / 500) + 1) / 2)
#define FPS                 60
#define UPDATE_FRAMES       (FPS * 1)   /* update clock once per second */

#define API_HOST  "https://finnhub.io"  /* kept for reference */
#define API_GA_ENDPOINT "https://finnhub.io/api/v1" /* kept for reference */
#define API_TOKEN "d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30" /* kept for reference */

/* Central Time base offset (standard). DST handled in code. */
#define TZ_CENTRAL_STD   (-6)
#define TZ_CENTRAL_DST   (-5)

/* Screen layout:
 *  Row  0 : title + build# + clock (CST)
 *  Row  1 : MW version / status
 *  Row  2 : WiFi / time sync status
 *  Row  3 : separator line
 *  Rows 4-27 : timezone table + compact log
 */
#define LOG_TOP   18
#define LOG_ROWS  10   /* compact log */
#define LOG_COLS  39

/* -------------------------------------------------------------------------
 * Command buffer
 * ---------------------------------------------------------------------- */
static char cmd_buf[MW_BUFLEN] __attribute__((aligned(2)));

/* -------------------------------------------------------------------------
 * Clock
 * ---------------------------------------------------------------------- */
static uint32_t epoch_utc    = 0;
static u16      clock_frames = 0;
static bool     have_time    = FALSE;
static bool     dt_ok_last   = FALSE;

static int8_t current_ct_offset(uint32_t epoch);

static void clock_draw(void)
{
    char h24[9] = "--:--:--";
    char h12[9] = "--:-- --";

    if (have_time) {
        int8_t ct_offset = current_ct_offset(epoch_utc);
        int32_t s = (int32_t)(epoch_utc % 86400UL) + (ct_offset * 3600);
        if (s < 0) s += 86400;
        uint8_t hh = (uint8_t)(s / 3600);
        uint8_t mm = (uint8_t)((s / 60) % 60);
        uint8_t ss = (uint8_t)(s % 60);
        sprintf(h24, "%02u:%02u:%02u", hh, mm, ss);
        uint8_t h12v = hh % 12; if (h12v == 0) h12v = 12;
        sprintf(h12, "%2u:%02u %s", h12v, mm, (hh >= 12) ? "PM" : "AM");
    }

    VDP_setTextPalette(PAL1);  /* green: call attention to CST 12h */
    VDP_drawText(h12, 22, 0);
    VDP_setTextPalette(PAL0);  /* white: neutral info */
    VDP_drawText(h24, 31, 0);
}

/* -------------------------------------------------------------------------
 * Debug log — 22 rows, newest at bottom, scrolls up when full
 * ---------------------------------------------------------------------- */
static char log_buf[LOG_ROWS][LOG_COLS + 1];
static u8   log_next = 0;   /* next row to write (0..LOG_ROWS-1) */
static bool log_full = FALSE;

static void log_redraw(void)
{
    u8 i;
    for (i = 0; i < LOG_ROWS; i++) {
        u8 src = log_full ? (u8)((log_next + i) % LOG_ROWS) : i;
        VDP_drawText(log_buf[src], 0, LOG_TOP + i);
    }
}

static void log_print(const char *msg)
{
    u8 i;
    /* Copy msg, pad with spaces to LOG_COLS */
    for (i = 0; i < LOG_COLS && msg[i]; i++) log_buf[log_next][i] = msg[i];
    for (; i < LOG_COLS; i++) log_buf[log_next][i] = ' ';
    log_buf[log_next][LOG_COLS] = '\0';

    log_next++;
    if (log_next >= LOG_ROWS) { log_next = 0; log_full = TRUE; }

    log_redraw();
}

/* Formatted log helper — up to LOG_COLS chars */
#define LOG(...) do { char _lb[LOG_COLS+1]; sprintf(_lb, __VA_ARGS__); log_print(_lb); } while(0)

/* -------------------------------------------------------------------------
 * Timezone table
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *label;
    int8_t      offset_hours; /* offset from UTC */
} tz_entry_t;

static const tz_entry_t tz_table[] = {
    { "ET",   -5 },  /* adjusted for DST at runtime */
    { "CT",   -6 },  /* adjusted for DST at runtime */
    { "MT",   -7 },
    { "PT",   -8 },
    { "AK",   -9 },
    { "HI",  -10 },
    { "UTC",   0 }
};

/* Palette selection: PAL0=white (info), PAL1=green (callouts), PAL2=reserved red, PAL3=blue (warnings) */
static const u8 tz_palette[] = { PAL0 /*ET*/, PAL1 /*CT*/, PAL0 /*MT*/, PAL0 /*PT*/, PAL0 /*AK*/, PAL0 /*HI*/, PAL1 /*UTC*/ };

/* Date helpers (from Howard Hinnant algorithms, reduced) */
typedef long long i64;

static i64 days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static void civil_from_days(i64 z, int *y, int *m, int *d)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    const unsigned mp  = (5*doy + 2)/153;
    const unsigned dd  = doy - (153*mp+2)/5 + 1;
    const unsigned mm  = mp + (mp < 10 ? 3 : -9);
    const int yy = (int)(yoe) + era*400 + (mm <= 2);
    *y = yy; *m = (int)mm; *d = (int)dd;
}

static int weekday_from_days(i64 z)
{
    /* 1970-01-01 was Thursday (4) using 0=Sunday */
    return (int)((z + 4) % 7 + 7) % 7;
}

static int is_us_dst(uint32_t epoch)
{
    /* Compute DST bounds in UTC for the given year */
    int y, m, d;
    civil_from_days(epoch / 86400, &y, &m, &d);

    /* March second Sunday at 08:00 UTC (2am CST) */
    int w_march1 = weekday_from_days(days_from_civil(y, 3, 1));
    int first_sun = (w_march1 == 0) ? 1 : (8 - w_march1);
    int second_sun = first_sun + 7;
    i64 start_days = days_from_civil(y, 3, second_sun);
    uint32_t dst_start = (uint32_t)(start_days * 86400LL + 8 * 3600);

    /* November first Sunday at 07:00 UTC (2am CDT) */
    int w_nov1 = weekday_from_days(days_from_civil(y, 11, 1));
    int first_sun_nov = (w_nov1 == 0) ? 1 : (8 - w_nov1);
    i64 end_days = days_from_civil(y, 11, first_sun_nov);
    uint32_t dst_end = (uint32_t)(end_days * 86400LL + 7 * 3600);

    return (epoch >= dst_start && epoch < dst_end);
}

static int8_t current_ct_offset(uint32_t epoch)
{
    return is_us_dst(epoch) ? TZ_CENTRAL_DST : TZ_CENTRAL_STD;
}

static void draw_timezones(void)
{
    u8 row = 4;
    char line[40];
    u32 base = epoch_utc;

    if (!base) {
        VDP_drawText("Waiting for NTP sync...", 4, row);
        return;
    }

    int8_t ct_offset = current_ct_offset(base);

    for (u8 i = 0; i < sizeof(tz_table)/sizeof(tz_table[0]); i++) {
        int8_t offset = tz_table[i].offset_hours;
        if (!strcmp(tz_table[i].label, "CT")) offset = ct_offset;
        if (!strcmp(tz_table[i].label, "ET")) offset = ct_offset - 1; /* ET follows US DST too */
        int32_t t = (int32_t)base + offset * 3600;
        t %= 86400;
        if (t < 0) t += 86400;
        uint8_t h = (uint8_t)(t / 3600);
        uint8_t m = (uint8_t)((t / 60) % 60);
        uint8_t s = (uint8_t)(t % 60);
        uint8_t h12 = h % 12; if (h12 == 0) h12 = 12;
        sprintf(line, "%-3s %02u:%02u:%02u  %2u:%02u %s",
                tz_table[i].label, h, m, s,
                h12, m, (h >= 12) ? "PM" : "AM");
        VDP_setTextPalette(tz_palette[i]);
        VDP_drawText(line, 0, row + i);
    }
    VDP_setTextPalette(PAL0);
}

/* JSON quote helpers removed for clock-only build */

/* -------------------------------------------------------------------------
 * idle_tsk
 * ---------------------------------------------------------------------- */
static void idle_tsk(void)
{
    while (1) mw_process();
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(bool hard_reset)
{
    u16  frame   = UPDATE_FRAMES;
    enum mw_err err;
    uint8_t ver_major = 0, ver_minor = 0;
    char *variant = NULL;
    int16_t def_ap;
    uint32_t dt_bin[2] = {0,0};
    bool time_synced = FALSE;

    (void)hard_reset;

    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);

    /* Set palette foreground colors used by text (color index 15 of each palette) */
    PAL_setColor(15, RGB24_TO_VDPCOLOR(0xFFFFFF));   /* PAL0 fg: white */
    PAL_setColor(31, RGB24_TO_VDPCOLOR(0x00EE00));   /* PAL1 fg: green */
    PAL_setColor(47, RGB24_TO_VDPCOLOR(0xFF4040));   /* PAL2 fg: red (reserved for errors) */
    PAL_setColor(63, RGB24_TO_VDPCOLOR(0x40C0FF));   /* PAL3 fg: blue (warnings/info) */

    {
        char title[41];
        sprintf(title, "NOC CLOCK #%04d", BUILD_NUM);
        VDP_setTextPalette(PAL3);
        VDP_drawText(title, 0, 0);
        VDP_setTextPalette(PAL0);
    }
    VDP_drawText("--:--:--", 31, 0);
    VDP_drawText("---------------------------------------", 0, 3);

    /* zero log buffer */
    {
        u8 i, j;
        for (i = 0; i < LOG_ROWS; i++) {
            for (j = 0; j < LOG_COLS; j++) log_buf[i][j] = ' ';
            log_buf[i][LOG_COLS] = '\0';
        }
    }

    /* --- MegaWiFi init -------------------------------------------------- */
    mw_init((u16*)cmd_buf, MW_BUFLEN);
    TSK_userSet(idle_tsk);

    LOG("MW: detecting...");
    err = mw_detect(&ver_major, &ver_minor, &variant);
    if (err != MW_ERR_NONE) {
        LOG("detect FAILED err=%d", (int)err);
        while (1) SYS_doVBlankProcess();
    }
    LOG("MW: v%d.%d ok", (int)ver_major, (int)ver_minor);
    {
        char vl[24];
        sprintf(vl, "MegaWiFi v%d.%d            ", (int)ver_major, (int)ver_minor);
        VDP_drawText(vl, 0, 1);
    }

    /* --- Associate ------------------------------------------------------ */
    def_ap = mw_def_ap_cfg_get();
    if (def_ap < 0) def_ap = 0;

    LOG("AP: assoc ap%d...", (int)def_ap);
    err = mw_ap_assoc((uint8_t)def_ap);
    if (err) { LOG("assoc FAILED err=%d", (int)err); while (1) SYS_doVBlankProcess(); }

    err = mw_ap_assoc_wait(MS_TO_FRAMES(30000));
    if (err) { LOG("assoc timeout"); while (1) SYS_doVBlankProcess(); }
    mw_sleep(3 * 60);

    {
        char *ap_ssid = NULL;
        if (mw_ap_cfg_get((uint8_t)def_ap, &ap_ssid, NULL, NULL) == MW_ERR_NONE && ap_ssid)
            LOG("AP: connected %s", ap_ssid);
        else
            LOG("AP: connected");
    }

    /* --- sys_stat ------------------------------------------------------- */
    {
        union mw_msg_sys_stat *ss = mw_sys_stat_get();
        if (ss) LOG("stat: sys=%d onl=%d cfg=%d",
                    (int)ss->sys_stat, (int)ss->online, (int)ss->cfg);
        else    LOG("stat: NULL");
    }

    /* --- SNTP config (CST with DST) ------------------------------------ */
    {
        const char *servers[3] = {"0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"};
        err = mw_sntp_cfg_set("CST6CDT", servers);
        LOG("sntp_cfg: err=%d", (int)err);
    }

    VDP_drawText("Syncing NTP...                        ", 0, 2);

    /* --- Main loop ------------------------------------------------------ */
    while (1) {
        VDP_waitVSync();

        clock_frames++;
        if (clock_frames >= 60) {
            clock_frames = 0;
            if (have_time) { epoch_utc++; clock_draw(); draw_timezones(); }
        }

        frame++;
        if (frame >= UPDATE_FRAMES) {
            frame = 0;
            /* Poll time from module once per second */
            union mw_msg_sys_stat *ss = mw_sys_stat_get();
            bool dt_ok = ss && ss->dt_ok;
            if (dt_ok) {
                char *ts = mw_date_time_get(dt_bin);
                /* Some firmwares report dt_bin[0]==0 and dt_bin[1]!=0; use whichever is non-zero */
                uint32_t new_epoch = dt_bin[0] ? dt_bin[0] : dt_bin[1];
                if (ts && new_epoch) {
                    epoch_utc = new_epoch;
                    have_time = TRUE;
                    if (!time_synced) {
                        LOG("time synced: %s", ts);
                        time_synced = TRUE;
                    }
                    clock_draw();
                    draw_timezones();
                    VDP_setTextPalette(PAL0);
                    VDP_drawText("Time synced                          ", 0, 2);
                }
            } else {
                dt_ok_last = FALSE;
                /* Keep last known time ticking; just show status */
                VDP_drawText("Syncing NTP...                        ", 0, 2);
            }
        }
    }

    return 0;
}
