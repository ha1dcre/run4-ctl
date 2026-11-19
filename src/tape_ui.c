/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, RUN4 mode: the tape view (protocol 1.3, docs/PROTOCOL.md "Tape view"). While the host sends a waveform
 * (TAPE_WAVE), the host's page is drawn in Synth mode's layout: the header and footer as on every RUN4 page, a band
 * 232 x 82 with the focused tape loop's waveform, its loop region, the playhead and five meters (T1..T4, master), and
 * the four knobs' tiles in a row under it. Black, greys and white with one amber accent (the playhead, the focused
 * track's meter, the knob in focus). The band redraws whole only when the waveform or the loop region changes; the
 * playhead and the meters move at most 25 times a second, and only the columns they cover are sent (as Synth mode's
 * scope sends its strips). Part of the unity build, after synth_ui.c (sy_accent, row_tile, sc_strip). */

#define TV_W 184                              /* the waveform: band columns 0..183 */
#define TV_MID 40                             /* its centre row */
#define TV_HALF 25                            /* rows above and below it at full scale */
#define TV_PH0 12                             /* the playhead's rows: 12 .. 69 */
#define TV_PH1 70
#define TV_MX 190                             /* the meters: five bars of 6 columns, 9 apart, from column 190 */
#define TV_MTOP 15                            /* their rows 15 .. 66 */
#define TV_MH 52
#define TV_LIVE_MS 40u                        /* 25 live frames a second at most */
static struct {
    uint8_t on;                               /* a waveform is shown (TAPE_WAVE with columns) */
    r4p_wave_t w;
    r4p_tpos_t pos;
    r4p_meters_t met;
    uint8_t full, pos_dirty, met_dirty;       /* owed: the whole band; the playhead's columns; the meters */
    int16_t drawn_x;                          /* the playhead's column on the screen (-1: none) */
    uint32_t at;                              /* the last live frame (plat_ms) */
    uint32_t frames, bytes, ticks;            /* the simulator's measures: band frames, bytes blitted, host time */
} tv;

static void tv_reset(void)
{
    memset(&tv, 0, sizeof tv);
    tv.pos.flags = R4P_TP_NOLOOP;
    tv.drawn_x = -1;
}
static int tv_layout(void)                    /* the host's page in the tape layout */
{
    return tv.on && host_page() && ui.connected && !ui.list_open;
}
static int32_t tv_x(uint32_t p) { int32_t x = (int32_t)(p * TV_W / 16384u); return x >= TV_W ? TV_W - 1 : x; }

/* host frames (ui_apply, RUN4 mode, connected) */
static void tv_apply(const r4p_msg_t *m)
{
    switch (m->cmd) {
    case R4P_TAPE_WAVE:
        if (!m->u.wave.n) {                   /* no columns: the tape view ends, the four tiles come back */
            if (tv.on)
                ui.dirty = D_ALL;
            tv.on = 0;
            break;
        }
        if (!tv.on)
            ui.dirty = D_ALL;                 /* the layout changes: everything */
        tv.on = 1;
        tv.w = m->u.wave;
        tv.full = 1;
        break;
    case R4P_TAPE_POS: {
        const r4p_tpos_t *p = &m->u.tpos;
        if (p->in != tv.pos.in || p->out != tv.pos.out || ((p->flags ^ tv.pos.flags) & (R4P_TP_NOLOOP | R4P_TP_REC)))
            tv.full = 1;                      /* the region (or the playhead's colour): the waveform's shading */
        else if (tv_x(p->play) != tv.drawn_x)
            tv.pos_dirty = 1;
        tv.pos = *p;
        break;
    }
    case R4P_METERS:
        if (memcmp(&tv.met, &m->u.meters, sizeof tv.met))
            tv.met_dirty = 1;
        tv.met = m->u.meters;
        break;
    default:
        break;
    }
}

/* the whole band into the canvas (232 x 82, band coordinates) */
static void tv_paint(void)
{
    const r4p_wave_t *w = &tv.w;
    int loop = !(tv.pos.flags & R4P_TP_NOLOOP);
    int32_t xin = tv_x(tv.pos.in), xout = tv_x(tv.pos.out), x, i, ph = tv_x(tv.pos.play);
    char t[R4P_STR_MAX + 1];
    static const char *const ML[R4P_NMETERS] = {"1", "2", "3", "4", "M"};
    cv_begin(232, BAND_H, R4_BG);
    fit(t, w->label, &FONT_XS, TV_W);
    ptext(0, 0, &FONT_XS, t, R4_LABEL);
    for (x = 0; x < TV_W; x += 4)             /* the zero line, dotted */
        cv_rect(x, TV_MID, 2, 1, R4_EDGE);
    for (i = 0; i < w->n; i++) {              /* a column a peak, mirrored; inside the loop region lighter */
        int32_t x0 = i * TV_W / w->n, x1 = (i + 1) * TV_W / w->n, h = w->peak[i] * TV_HALF / 127, cw;
        int in = !loop || ((x0 + x1) / 2 >= xin && (x0 + x1) / 2 <= xout);
        if (x1 <= x0) x1 = x0 + 1;
        cw = x1 - x0 > 2 ? x1 - x0 - 1 : x1 - x0;
        if (w->peak[i])
            cv_rect(x0, TV_MID - h, cw, 2 * h + 1, in ? SY_BAR : R4_DIM);
    }
    cv_rect(0, 73, TV_W, 1, R4_EDGE);         /* the loop region under it */
    if (loop) {
        cv_rect(xin, 72, xout - xin + 1, 3, R4_TEXT);
        cv_rect(xin, TV_PH0, 1, TV_PH1 - TV_PH0, SY_MID);
        cv_rect(xout, TV_PH0, 1, TV_PH1 - TV_PH0, SY_MID);
    }
    cv_rect(ph, TV_PH0, 2, TV_PH1 - TV_PH0, (tv.pos.flags & R4P_TP_REC) ? R4_RED : sy_accent(100));
    for (i = 0; i < R4P_NMETERS; i++) {       /* the meters: the focused track in the accent, the master white */
        int32_t mx = TV_MX + i * 9, h = tv.met.level[i] * TV_MH / 127;
        uint16_t c = i == w->track ? sy_accent(100) : i == R4P_NMETERS - 1 ? R4_TEXT : SY_BAR;
        cv_rect(mx, TV_MTOP, 6, TV_MH, R4_TILE);
        if (h)
            cv_rect(mx, TV_MTOP + TV_MH - h, 6, h, c);
        cv_rect(mx, TV_MTOP - 4, 6, 2, (tv.met.clip >> i) & 1u ? R4_RED : R4_TILE);
        ptext(mx + 3 - pwidth(&FONT_XS, ML[i]) / 2, 70, &FONT_XS, ML[i], i == w->track ? sy_accent(100) : R4_LABEL);
    }
}
/* the canvas's columns c0..c1-1, rows r0..r1-1 to the screen, in strips 16 columns wide; not under a toast */
static void tv_rect(int32_t c0, int32_t c1, int32_t r0, int32_t r1)
{
    int32_t c, r, g;
    c0 = c0 < 0 ? 0 : c0;
    c1 = c1 > 232 ? 232 : c1;
    if (ui.toast_until) {                     /* the toast covers screen rows 84..113 (band rows 50..79) */
        int32_t tw = pwidth(&FONT_B, ui.toast) + 28, tx;
        tw = tw > 224 ? 224 : tw < 80 ? 80 : tw;
        tx = (240 - tw) / 2;
        if (4 + c1 > tx && 4 + c0 < tx + tw && r1 > 50)
            r1 = 50;
    }
    if (c1 <= c0 || r1 <= r0)
        return;
    /* the last rect's last strip may still be on its way to the panel (lcd_blit only starts the transfer), and this
     * rect fills strip 0 first: without the wait the old playhead's columns went out as the meters' tiles or the new
     * head's columns (the grey grid left where the head had been, firmware 0.9; tests/tape_view_test.c) */
    lcd_sync();
    for (g = c0; g < c1; g += SC_G) {
        int32_t g1 = g + SC_G > c1 ? c1 : g + SC_G;
        uint16_t *d = sc_strip[((g - c0) / SC_G) & 1];
        for (r = r0; r < r1; r++)
            for (c = g; c < g1; c++)
                d[(r - r0) * (g1 - g) + (c - g)] = cv_px[r * 232 + c];
        lcd_blit((uint32_t)(4 + g), (uint32_t)(BAND_Y + r0), (uint32_t)(g1 - g), (uint32_t)(r1 - r0), d);
        tv.bytes += (uint32_t)((g1 - g) * (r1 - r0) * 2);
    }
}
static void tv_band(int full)
{
    uint32_t t0 = plat_ticks24();
    int32_t ph = tv_x(tv.pos.play);
    tv_paint();
    if (full) {
        cv_blit(4, BAND_Y);
        tv.bytes += 232u * BAND_H * 2u;
    } else {
        if (tv.pos_dirty && ph != tv.drawn_x) {
            int32_t o = tv.drawn_x;
            if (o >= 0 && (o > ph + 3 || o + 3 < ph)) {     /* apart: two narrow rects, else one */
                tv_rect(o - 1, o + 3, TV_PH0, TV_PH1);
                tv_rect(ph - 1, ph + 3, TV_PH0, TV_PH1);
            } else {
                int32_t a = o >= 0 && o < ph ? o : ph, b = o > ph ? o : ph;
                tv_rect(a - 1, b + 3, TV_PH0, TV_PH1);
            }
        }
        if (tv.met_dirty)
            tv_rect(TV_MX, 232, TV_MTOP - 4, TV_MTOP + TV_MH);
    }
    tv.drawn_x = (int16_t)ph;
    tv.full = tv.pos_dirty = tv.met_dirty = 0;
    tv.frames++;
    tv.ticks += plat_ticks24() - t0;
}
/* one tile of the row (y 116..199): a canvas 59 wide at x 2 + 59 k, so the four cover x 2..237 */
static void tv_tile(int k)
{
    cv_begin(59, 84, R4_BG);
    row_tile(2, &PG->slot[k], hl_tile() == k, -1);
    cv_blit((uint32_t)(2 + 59 * k), 116);
}
/* ui_draw, the tape layout: the band whole (D_CLEAR, the big view or a toast gone: D_GAP / D_TBAND), or a live frame
 * when one is owed and due; the tiles that changed */
static void tv_draw(uint32_t now)
{
    int k;
    if (ui.dirty & (D_CLEAR | D_GAP | D_TBAND))
        tv.full = 1;
    if (tv.full) {
        tv_band(1);
        tv.at = now;
        if (ui.toast_until)
            ui.dirty |= D_TOAST;              /* (drawn over it again) */
    } else if ((tv.pos_dirty || tv.met_dirty) && now - tv.at >= TV_LIVE_MS) {
        tv_band(0);
        tv.at = now;
    }
    if (ui.dirty & D_CLEAR)
        lcd_fill(0, BAND_Y, 4, BAND_H, R4_BG), lcd_fill(236, BAND_Y, 4, BAND_H, R4_BG);
    for (k = 0; k < 4; k++)
        if (ui.dirty & ((D_TILE0 << k) | D_GAP | D_CLEAR))
            tv_tile(k);
}
