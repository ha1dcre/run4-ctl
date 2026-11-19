/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, RUN4 mode: the piano roll (protocol 1.7, docs/PROTOCOL.md "Protocol 1.7: the piano roll"). While the host
 * sends a window of its pattern (ROLL with rows), the host's page is drawn with the header and footer as on every RUN4
 * page, a band 232 x 118 with the roll (a key column, the pitch rows from the bottom up, the step columns with their
 * beat and bar lines, the notes as bars, the cursor cell framed white, the playhead), and the four knobs as compact
 * tiles in a row under it. The device only draws: the cursor, the notes and every edit are the host's (the knobs, the
 * keys and the buttons go to it as always). The band redraws whole when the window changes; when only the playhead
 * moves (at most 25 times a second) only its old and new columns are sent.
 *
 * The roll's drawing (a key column beside the grid, bars a cell high with their length in columns, a frame on the
 * cursor cell, a vertical playhead) follows graph_roll in fmini-1's firmware/src/ui_graph.c, Copyright (C) 2026 Leo
 * Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0-only; written anew here for RUN4's window and colours.
 * Part of the unity build, after tape_ui.c (sy_accent, sc_strip, BAND_Y). */

#define RL_H 118                              /* the band: screen rows 34 .. 151 */
#define RL_GTOP 16                            /* the grid's top row at most (the label row above it) */
#define RL_GX 24                              /* the grid's first column (the key column 0 .. 21) */
#define RL_GW 208                             /* the grid's width at most */
#define RL_KW 22                              /* the key column's width */
#define RL_TY 152                             /* the tile row: screen rows 152 .. 199 */
#define RL_TH 48
#define RL_LIVE_MS 40u
#define RL_LANE_B RGB(16, 16, 21)             /* a black key's row */
#define RL_CURCOL RGB(36, 36, 46)             /* the cursor's column, lit faintly */
static struct {
    uint8_t on;                               /* a window is shown (ROLL with rows) */
    r4p_roll_t r;
    uint8_t full, play_dirty;                 /* owed: the whole band; the playhead's columns */
    int16_t drawn_play;                       /* the playhead's column on the screen (-1: none) */
    uint32_t at;                              /* the last live frame (plat_ms) */
    uint32_t frames, bytes;                   /* the simulator's measures: band frames, bytes blitted */
} rl;

static void rl_reset(void)
{
    memset(&rl, 0, sizeof rl);
    rl.drawn_play = -1;
}
static int rl_layout(void)                    /* the host's page in the roll layout */
{
    return rl.on && host_page() && ui.connected && !ui.list_open;
}
static int32_t rl_rh(void) { return (RL_H - RL_GTOP) / (rl.r.rows ? rl.r.rows : 1); }   /* a row's height */
static int32_t rl_cw(void) { return RL_GW / (rl.r.steps ? rl.r.steps : 1); }            /* a column's width */
static int32_t rl_gy(void) { return RL_H - rl_rh() * rl.r.rows; }                       /* the grid's top */
static int32_t rl_ry(int32_t row) { return RL_H - rl_rh() * (row + 1); }                /* row 0 the lowest */

/* two windows alike but for the playhead (and the PLAY flag) */
static int rl_same(const r4p_roll_t *a, const r4p_roll_t *b)
{
    unsigned i;
    if (((a->flags ^ b->flags) & (uint8_t)~R4P_RF_PLAY) || a->lo != b->lo || a->rows != b->rows || a->steps != b->steps ||
        a->first != b->first || a->total != b->total || a->cur_col != b->cur_col || a->cur_row != b->cur_row ||
        a->beat != b->beat || a->n != b->n || strcmp(a->label, b->label))
        return 0;
    for (i = 0; i < a->n && i < R4P_ROLL_NOTES; i++)
        if (memcmp(&a->note[i], &b->note[i], sizeof a->note[i]))
            return 0;
    return 1;
}

/* host frames (ui_apply, RUN4 mode, connected) */
static void rl_apply(const r4p_msg_t *m)
{
    const r4p_roll_t *r = &m->u.roll;
    if (!r->rows) {                           /* rows 0: the roll ends, the page's own layout comes back */
        if (rl.on)
            ui.dirty = D_ALL;
        rl.on = 0;
        rl.drawn_play = -1;
        return;
    }
    if (!rl.on) {
        ui.dirty = D_ALL;                     /* the layout changes: everything */
        big_end();
    }
    /* the same window with only the playhead elsewhere: its columns; anything else: the band whole */
    if (rl.on && !rl.full && rl_same(&rl.r, r)) {
        if (r->play != rl.r.play)
            rl.play_dirty = 1;
        rl.r = *r;
        return;
    }
    rl.on = 1;
    rl.r = *r;
    rl.full = 1;
}

/* the key column, the lanes, the grid lines, the notes, the cursor, the playhead into the canvas (232 x 118) */
static void rl_paint(void)
{
    const r4p_roll_t *r = &rl.r;
    int32_t rh = rl_rh(), cw = rl_cw(), gy = rl_gy(), gw = cw * r->steps, row, c, i, y, x;
    char t[R4P_STR_MAX + 1], u[12];
    cv_begin(232, RL_H, R4_BG);
    /* the label row: the host's label (the pitches, the track), the window's steps of the pattern (up to its end) */
    t[0] = 0;
    u_int((unsigned)r->first + 1u, u); u_cat(t, u); u_cat(t, "-");
    u_int((unsigned)r->first + r->steps < r->total || r->total <= r->first ? (unsigned)r->first + r->steps : r->total, u);
    u_cat(t, u); u_cat(t, "/"); u_int(r->total, u); u_cat(t, u);
    ptext(232 - pwidth(&FONT_XS, t), 0, &FONT_XS, t, R4_LABEL);
    {
        char lab[R4P_STR_MAX + 1];
        fit(lab, r->label, &FONT_XS, 232 - pwidth(&FONT_XS, t) - 10);
        ptext(0, 0, &FONT_XS, lab, (r->flags & R4P_RF_REC) ? R4_RED : R4_LABEL);
    }
    /* the lanes: a white key's row lighter, a black key's darker; the cursor's column lit faintly */
    for (row = 0; row < r->rows; row++) {
        unsigned p = (unsigned)r->lo + (unsigned)row, pc = p % 12u;
        int black = pc == 1u || pc == 3u || pc == 6u || pc == 8u || pc == 10u;
        y = rl_ry(row);
        cv_rect(RL_GX, y, gw, rh, black ? RL_LANE_B : R4_TILE);
        cv_rect(0, y, RL_KW, rh - (rh > 3 ? 1 : 0), row == r->cur_row ? sy_accent(100) : black ? R4_EDGE : R4_LABEL);
        if (black)
            cv_rect(RL_KW - 7, y, 7, rh, R4_BG);                    /* a black key is short */
        if (pc == 0u && row)                                         /* the octave line under a C */
            cv_rect(RL_GX, y + rh - 1, gw, 1, R4_EDGE);
    }
    if (r->cur_col < r->steps)
        cv_rect(RL_GX + r->cur_col * cw, gy, cw, rh * r->rows, RL_CURCOL);
    /* the columns: beat lines, bar lines (four beats), the columns past the pattern's end shaded */
    for (c = 0; c < r->steps; c++) {
        unsigned s = (unsigned)r->first + (unsigned)c;
        x = RL_GX + c * cw;
        if (s >= r->total) {
            for (y = gy + 1; y < RL_H; y += 3)
                cv_rect(x, y, cw, 1, R4_BG);
            continue;
        }
        if (r->beat && s % (r->beat * 4u) == 0u)
            cv_rect(x, gy, 1, RL_H - gy, R4_DIM);
        else if (r->beat && s % r->beat == 0u)
            cv_rect(x, gy, 1, RL_H - gy, R4_EDGE);
        else if (cw >= 8)
            cv_rect(x, gy, 1, RL_H - gy, RL_LANE_B);
    }
    /* the C keys' names (where a row has room for them, over the row above) */
    if (rh >= 5)
        for (row = 0; row < r->rows; row++) {
            unsigned p = (unsigned)r->lo + (unsigned)row;
            if (p % 12u)
                continue;
            t[0] = 'C'; t[1] = 0;
            if (p < 12u) u_cat(t, "-1");
            else u_int(p / 12u - 1u, u), u_cat(t, u);
            y = rl_ry(row) + rh - 12;
            if (y >= gy - 2)
                ptext(1, y, &FONT_T, t, row == r->cur_row ? on_accent() : R4_BG);
        }
    /* the notes: bars a cell high, brighter the louder; muted grey; the one under the cursor outlined */
    for (i = 0; i < r->n; i++) {
        const r4p_rnote_t *nt = &r->note[i];
        int32_t x0 = RL_GX + nt->col * cw + ((nt->flags & R4P_RN_IN) ? 0 : 1);
        int32_t x1 = RL_GX + (nt->col + nt->len) * cw - ((nt->flags & R4P_RN_OUT) ? 0 : 1);
        int32_t y0 = rl_ry(nt->row) + (rh > 3 ? 1 : 0), h = rh - (rh > 3 ? 2 : 1);
        uint16_t col = (nt->flags & R4P_RN_MUTED) ? R4_DIM : accent(40 + nt->vel * 60 / 127);
        if (h < 1) h = 1;
        if (x1 <= x0) x1 = x0 + 1;
        if (nt->flags & R4P_RN_SEL) {
            cv_rect(x0 - 1, y0 - 1, x1 - x0 + 2, h + 2, R4_TEXT);
            cv_rect(x0, y0, x1 - x0, h, col);
        } else {
            cv_rect(x0, y0, x1 - x0, h, col);
        }
        if ((nt->flags & R4P_RN_CHANCE) && h >= 3)                  /* by chance: a dark line through the bar */
            cv_rect(x0 + 1, y0 + h / 2, x1 - x0 - 2 > 0 ? x1 - x0 - 2 : 1, 1, R4_BG);
        if ((nt->flags & R4P_RN_IN) && h >= 3)                       /* from before the window: a notch at its edge */
            cv_rect(x0, y0 + h / 2 - 1, 2, 3, R4_TEXT);
        if ((nt->flags & R4P_RN_OUT) && h >= 3)                      /* on after the window */
            cv_rect(x1 - 2, y0 + h / 2 - 1, 2, 3, R4_TEXT);
    }
    /* the cursor: a white frame two pixels wide around its cell */
    if (r->cur_col < r->steps && r->cur_row < r->rows) {
        x = RL_GX + r->cur_col * cw;
        y = rl_ry(r->cur_row);
        cv_rect(x - 1, y - 1, cw + 2, 2, R4_TEXT);
        cv_rect(x - 1, y + rh - 1, cw + 2, 2, R4_TEXT);
        cv_rect(x - 1, y - 1, 2, rh + 2, R4_TEXT);
        cv_rect(x + cw - 1, y - 1, 2, rh + 2, R4_TEXT);
    }
    /* the playhead: a line through its column's middle (red while recording) */
    if (r->play < r->steps)
        cv_rect(RL_GX + r->play * cw + cw / 2, gy, 1, RL_H - gy, (r->flags & R4P_RF_REC) ? R4_RED : sy_accent(100));
}
/* the canvas's columns c0..c1-1, rows r0..r1-1 to the screen in strips that fit sc_strip; not under a toast */
static void rl_send(int32_t c0, int32_t c1, int32_t r0, int32_t r1)
{
    int32_t c, r, g, sw;
    c0 = c0 < 0 ? 0 : c0;
    c1 = c1 > 232 ? 232 : c1;
    if (c1 <= c0 || r1 <= r0)
        return;
    sw = (int32_t)(sizeof sc_strip[0] / sizeof sc_strip[0][0]) / (r1 - r0);
    sw = sw < 1 ? 1 : sw > SC_G ? SC_G : sw;
    for (g = c0; g < c1; g += sw) {
        int32_t g1 = g + sw > c1 ? c1 : g + sw;
        uint16_t *d = sc_strip[((g - c0) / sw) & 1];
        for (r = r0; r < r1; r++)
            for (c = g; c < g1; c++)
                d[(r - r0) * (g1 - g) + (c - g)] = cv_px[r * 232 + c];
        lcd_blit((uint32_t)(4 + g), (uint32_t)(BAND_Y + r0), (uint32_t)(g1 - g), (uint32_t)(r1 - r0), d);
        rl.bytes += (uint32_t)((g1 - g) * (r1 - r0) * 2);
    }
}
static void rl_rect(int32_t c0, int32_t c1, int32_t r0, int32_t r1)
{
    if (ui.toast_until) {                     /* the toast covers screen rows 84..113 (band rows 50..79) */
        int32_t tw = pwidth(&FONT_B, ui.toast) + 28, tx;
        tw = tw > 224 ? 224 : tw < 80 ? 80 : tw;
        tx = (240 - tw) / 2;
        if (4 + c1 > tx && 4 + c0 < tx + tw && r1 > 50 && r0 < 80) {
            rl_send(c0, c1, r0, 50);
            rl_send(c0, c1, 80, r1);
            return;
        }
    }
    rl_send(c0, c1, r0, r1);
}
static void rl_band(int full)
{
    int32_t cw = rl_cw(), gy = rl_gy();
    int16_t ph = rl.r.play < rl.r.steps ? (int16_t)rl.r.play : -1;
    rl_paint();
    if (full) {
        cv_blit(4, BAND_Y);
        rl.bytes += 232u * RL_H * 2u;
    } else if (ph != rl.drawn_play) {
        if (rl.drawn_play >= 0)
            rl_rect(RL_GX + rl.drawn_play * cw - 1, RL_GX + (rl.drawn_play + 1) * cw + 1, gy - 1, RL_H);
        if (ph >= 0)
            rl_rect(RL_GX + ph * cw - 1, RL_GX + (ph + 1) * cw + 1, gy - 1, RL_H);
    }
    rl.drawn_play = ph;
    rl.full = rl.play_dirty = 0;
    rl.frames++;
}
/* one compact tile (58 x 48 at x 2 + 59 k): the name, the value, a thin bar */
static void rl_tile(int k)
{
    const r4p_slot_t *s = &PG->slot[k];
    int hot = hl_tile() == k;
    uint16_t ac = hot ? sy_accent(100) : SY_BAR;
    char t[R4P_STR_MAX + 1];
    int32_t bw = 44, bx = 7, by = 41, fill;
    cv_begin(59, RL_TH, R4_BG);
    if (s->kind != R4P_KIND_EMPTY) {
        cv_rect(0, 1, 56, RL_TH - 2, R4_TILE);
        cv_rect(0, 1, 2, RL_TH - 2, hot ? sy_accent(100) : R4_EDGE);
        fit(t, s->name, &FONT_XS, 47);
        ptext(7, 3, &FONT_XS, t, hot ? sy_accent(100) : R4_LABEL);
        fit(t, s->text, &FONT_B, 47);
        ptext(7, 21, &FONT_B, t, R4_TEXT);
        if (s->kind == R4P_KIND_UNI) {
            fill = s->value * bw / R4P_V14_MAX;
            cv_rect(bx, by + 1, bw, 1, R4_EDGE);
            cv_rect(bx, by, fill, 3, ac);
        } else if (s->kind == R4P_KIND_BI) {
            fill = ((int32_t)s->value - 8192) * (bw / 2) / 8191;
            cv_rect(bx, by + 1, bw, 1, R4_EDGE);
            if (fill >= 0) cv_rect(bx + bw / 2, by, fill, 3, ac);
            else cv_rect(bx + bw / 2 + fill, by, -fill, 3, ac);
        }
    }
    cv_blit((uint32_t)(2 + 59 * k), RL_TY);
}
/* ui_draw, the roll layout: the band whole (D_CLEAR, a toast gone: D_GAP / D_TBAND), or the playhead's columns when
 * owed and due; the tiles that changed */
static void rl_draw(uint32_t now)
{
    int k;
    if (ui.dirty & (D_CLEAR | D_GAP | D_TBAND))
        rl.full = 1;
    if (rl.full) {
        rl_band(1);
        rl.at = now;
        if (ui.toast_until)
            ui.dirty |= D_TOAST;              /* (drawn over it again) */
    } else if (rl.play_dirty && now - rl.at >= RL_LIVE_MS) {
        rl_band(0);
        rl.at = now;
    }
    for (k = 0; k < 4; k++)
        if (ui.dirty & ((D_TILE0 << k) | D_GAP | D_CLEAR))
            rl_tile(k);
}
