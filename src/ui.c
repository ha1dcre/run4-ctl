/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL display: state from the host's frames, drawn by regions (only what changed is redrawn).
 * Part of the unity build, included by app/app.c after the platform: gfx.c (cv_*, fonts) is in scope. */

/* colours of our own: the track colour carries the page, the rest is neutral */
#define R4_BG     RGB(10, 10, 14)
#define R4_TILE   RGB(24, 24, 30)
#define R4_EDGE   RGB(52, 52, 62)
#define R4_DIM    RGB(78, 78, 90)
#define R4_LABEL  RGB(150, 150, 160)
#define R4_TEXT   RGB(236, 236, 240)
#define R4_GREEN  RGB(80, 220, 120)
#define R4_RED    RGB(236, 70, 60)

enum { D_HEAD = 1, D_TILE0 = 2, D_TILES = 2 | 4 | 8 | 16, D_FOOT = 32, D_TOAST = 64, D_CLEAR = 128, D_DOTS = 256,
       D_BIG = 512, D_GAP = 1024,             /* protocol 1.2: the big value view; the strip between the tile rows */
       D_LIVE = 2048,                         /* Synth mode: a live frame of the band (synth_ui.c) */
       D_TBAND = 4096,                        /* protocol 1.3, the tape layout: the band whole (a toast went) */
       D_TOAST_UNDER = 2 | 4,                 /* the two tiles of the top row: the toast sits in y 84..113 */
       D_ALL = D_HEAD | D_TILES | D_FOOT | D_CLEAR | D_DOTS };

typedef struct {
    uint8_t connected;
    r4p_page_t page;
    r4p_status_t st;
    char toast[R4P_STR_MAX + 1];
    uint32_t toast_until;           /* plat_ms deadline, 0 = none */
    int8_t octave;                  /* local rules (app.c) shown in the footer */
    uint8_t shift;
    uint8_t dots;                   /* the "not connected" screen's animation phase */
    uint16_t dirty;
    uint8_t setup;                  /* the firmware-local SETUP page is open (app.c) */
    uint8_t local;                  /* Synth mode: the pages are ours, no host needed (synth_ui.c) */
    uint8_t list_open;              /* a host LIST is shown (protocol 1.1) */
    r4p_list_t list;
    r4p_page_t setup_page;          /* what it shows (app.c fills it) */
    /* protocol 1.2 (a host that said minor 2): the big value view */
    uint8_t v12;                    /* the host speaks 1.2 */
    uint8_t touch1;                 /* the tile of the knob turned last + 1 (framed until its VALUE), 0 = none */
    uint8_t big1;                   /* the slot shown big + 1, 0 = the four tiles */
    uint32_t touch_at, big_until;
} ui_t;
static ui_t ui;
#define PG (ui.setup ? &ui.setup_page : &ui.page)   /* the page being drawn */
static void tv_reset(void);                 /* tape_ui.c: the tape view (protocol 1.3) */
static int tv_layout(void);
static void tv_draw(uint32_t now);
static void tv_apply(const r4p_msg_t *m);

static void ui_reset_page(void)
{
    memset(&ui.page, 0, sizeof ui.page);
    strcpy(ui.page.title, "RUN4");
    ui.page.r = 120; ui.page.g = 70; ui.page.b = 20;
    ui.page.page_count = 0;
    ui.page.cursor = R4P_CURSOR_NONE;
    ui.touch1 = ui.big1 = 0;
}
static void ui_connect(int on)
{
    if (on == ui.connected && on == 0)
        return;
    ui.connected = (uint8_t)on;
    if (!on) {
        ui.toast_until = 0;
        ui.toast[0] = 0;
    }
    ui_reset_page();
    ui.list_open = 0;
    ui.big1 = ui.touch1 = 0;
    memset(&ui.st, 0, sizeof ui.st);
    tv_reset();                                     /* (1.3: the host sends the tape view again after its SYNC_REQ) */
    ui.dirty = D_ALL;
}

/* ------------------------------------------------------------ helpers --- */
static void u_int(unsigned v, char *b)
{
    char t[12];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) && n < 11);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void u_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
}
/* the fonts are ASCII: Latin-1 letters are drawn as their base letter */
#define PT_MAX 48
static int32_t ptext(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    char t[PT_MAX + 1];
    uint32_t n = 0;
    while (s[n] && n < PT_MAX)
        t[n] = r4p_fold((unsigned char)s[n]), n++;
    t[n] = 0;
    return cv_text(x, y, f, t, c);
}
static int32_t pwidth(const felucca_font_t *f, const char *s)
{
    char t[PT_MAX + 1];
    uint32_t n = 0;
    while (s[n] && n < PT_MAX)
        t[n] = r4p_fold((unsigned char)s[n]), n++;
    t[n] = 0;
    return text_w(f, t);
}
static uint16_t scale(uint16_t c565_dummy, int r, int g, int b, int pct)
{
    (void)c565_dummy;
    return RGB(r * pct / 100, g * pct / 100, b * pct / 100);
}
static void accent_rgb(int *r, int *g, int *b)       /* 0..127 on the wire, x2 on the screen */
{
    *r = PG->r * 2; *g = PG->g * 2; *b = PG->b * 2;
}
static uint16_t accent(int pct)
{
    int r, g, b;
    accent_rgb(&r, &g, &b);
    return scale(0, r, g, b, pct);
}
static uint16_t on_accent(void)                       /* text on the accent: black or white by luminance */
{
    return (PG->r * 299 + PG->g * 587 + PG->b * 114) / 1000 >= 56 ? RGB(10, 10, 14) : RGB(250, 250, 250);
}

/* ------------------------------------------------------------- header --- */
static int host_page(void) { return !ui.setup && !ui.local; }   /* the host's page (not SETUP, not Synth mode) */
static void fit(char *t, const char *s, const felucca_font_t *f, int32_t w)   /* s cut to w pixels */
{
    size_t n = 0;
    while (s[n] && n < R4P_STR_MAX)
        t[n] = s[n], n++;
    t[n] = 0;
    while (n && pwidth(f, t) > w)
        t[--n] = 0;
}
static void draw_head(void)
{
    int32_t w = 0;
    char t[R4P_STR_MAX + 1];
    cv_begin(240, 34, accent(100));
    cv_rect(0, 32, 240, 2, accent(55));
    if (PG->track[0]) {                          /* the track in a dark pill, in the track colour */
        w = pwidth(&FONT_B, PG->track) + 14;
        cv_rect(232 - w, 5, w, 22, R4_BG);
        ptext(232 - w + 7, 8, &FONT_B, PG->track, accent(100));
    }
    fit(t, host_page() && PG->path[0] ? PG->path : PG->title, &FONT_B, 232 - w - 14);   /* 1.2: the path, where given */
    ptext(8, 8, &FONT_B, t, on_accent());
    cv_blit(0, 0);
}
static int hl_tile(void)                          /* the framed tile: the knob just turned, else the host's cursor */
{
    if (!host_page())
        return -1;
    if (ui.touch1)
        return ui.touch1 - 1;
    return PG->cursor < R4P_NSLOTS ? PG->cursor : -1;
}

/* -------------------------------------------------------------- tiles --- */
#define BAR_X 10
#define BAR_W 96
static void draw_bar(const r4p_slot_t *s, int32_t y)
{
    int32_t v = s->value, fill, c, i, n;
    switch (s->kind) {
    case R4P_KIND_UNI:
        cv_rect(BAR_X, y, BAR_W, 8, R4_EDGE);
        fill = v * BAR_W / R4P_V14_MAX;
        cv_rect(BAR_X, y, fill, 8, accent(100));
        break;
    case R4P_KIND_BI:
        cv_rect(BAR_X, y, BAR_W, 8, R4_EDGE);
        c = BAR_X + BAR_W / 2;
        fill = (v - 8192) * (BAR_W / 2) / 8191;     /* 8192 = centre */
        if (fill >= 0)
            cv_rect(c, y, fill, 8, accent(100));
        else
            cv_rect(c + fill, y, -fill, 8, accent(100));
        cv_rect(c - 1, y - 2, 2, 12, R4_TEXT);
        break;
    case R4P_KIND_ENUM:                              /* value = count << 7 | index */
        n = (v >> 7) & 0x7F;
        if (n > 16) n = 16;
        if (n < 2) n = 2;
        for (i = 0; i < n; i++) {
            int32_t x0 = BAR_X + i * BAR_W / n, x1 = BAR_X + (i + 1) * BAR_W / n - 2;
            cv_rect(x0, y, x1 - x0, 8, i == (v & 0x7F) ? accent(100) : R4_EDGE);
        }
        break;
    case R4P_KIND_TOGGLE:                            /* a pill with a knob */
        cv_rect(BAR_X, y - 3, 34, 14, v ? accent(100) : R4_EDGE);
        cv_rect(v ? BAR_X + 22 : BAR_X + 2, y - 1, 10, 10, v ? on_accent() : R4_DIM);
        break;
    default:
        break;
    }
}
static void draw_tile(int i)
{
    const r4p_slot_t *s = &PG->slot[i];
    int empty = s->kind == R4P_KIND_EMPTY;
    int32_t x = (i & 1) ? 120 : 4, y = (i & 2) ? 118 : 38;
    cv_begin(116, 76, R4_BG);
    cv_rect(0, 0, 116, 76, empty ? R4_BG : R4_TILE);
    cv_rect(0, 0, 116, 1, empty ? R4_TILE : R4_EDGE);          /* a hairline frame */
    cv_rect(0, 75, 116, 1, empty ? R4_TILE : R4_EDGE);
    cv_rect(0, 0, 3, 76, empty ? R4_EDGE : accent(100));       /* accent stripe */
    if (!empty) {
        const felucca_font_t *f = pwidth(&FONT_M, s->text) <= 100 ? &FONT_M : &FONT_B;
        ptext(BAR_X, 6, &FONT_XS, s->name, R4_LABEL);
        ptext(BAR_X, f == &FONT_M ? 22 : 26, f, s->text, R4_TEXT);
        draw_bar(s, 58);
    } else {
        ptext(BAR_X, 6, &FONT_XS, s->name[0] ? s->name : "-", R4_DIM);
    }
    if (hl_tile() == i) {                                       /* 1.2: in focus, a frame in the page colour */
        cv_rect(0, 0, 116, 2, accent(100)); cv_rect(0, 74, 116, 2, accent(100));
        cv_rect(114, 0, 2, 76, accent(100));
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}

/* ------------------------------------------------------------- footer --- */
static void draw_foot(void)
{
    char t[24], u[12];
    int32_t x, i, n = PG->page_count;
    cv_begin(240, 40, R4_BG);
    cv_rect(0, 0, 240, 1, R4_EDGE);
    if (n > 0 && n <= 12) {                                    /* page dots */
        for (i = 0; i < n; i++)
            cv_rect(8 + i * 12, 7, 8, 8, i == PG->page_idx ? accent(100) : R4_EDGE);
    } else if (n > 12) {
        t[0] = 'P'; t[1] = 0;
        u_int((unsigned)PG->page_idx + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int((unsigned)n, u); u_cat(t, u);
        ptext(8, 5, &FONT_XS, t, R4_LABEL);
    }
    x = 232;
    if (host_page() && PG->item_count) {                   /* 1.2: the focused parameter's position, right-aligned */
        t[0] = 0; u_int((unsigned)PG->item_idx + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int(PG->item_count, u); u_cat(t, u);
        x -= pwidth(&FONT_XS, t);
        ptext(x, 5, &FONT_XS, t, accent(100));
        x -= 10;
    }
    if (PG->bank_count > 1) {                              /* bank, right-aligned */
        t[0] = 0; u_cat(t, "BK "); u_int((unsigned)PG->bank_idx + 1u, u); u_cat(t, u); u_cat(t, "/");
        u_int((unsigned)PG->bank_count, u); u_cat(t, u);
        ptext(x - pwidth(&FONT_XS, t), 5, &FONT_XS, t, R4_LABEL);
    }
    if (ui.shift) {
        cv_rect(160, 4, 38, 13, accent(100));
        ptext(165, 5, &FONT_XS, "SHIFT", on_accent());
    }
    /* row 2: tempo, bar.beat, transport, octave */
    t[0] = 0; u_int(ui.st.bpm10 / 10u, u); u_cat(t, u); u_cat(t, "."); u_int(ui.st.bpm10 % 10u, u); u_cat(t, u);
    x = ptext(8, 21, &FONT_B, ui.st.bpm10 ? t : "--", R4_TEXT);
    ptext(x + 3, 25, &FONT_XS, "BPM", R4_DIM);
    t[0] = 0; u_int((unsigned)ui.st.bar, u); u_cat(t, u); u_cat(t, "."); u_int((unsigned)ui.st.beat + 1u, u); u_cat(t, u);
    x = ptext(88, 21, &FONT_B, ui.st.bpm10 ? t : "-.-", R4_TEXT);
    if (ui.st.flags & R4P_ST_PLAY) {                           /* play triangle */
        for (i = 0; i < 6; i++)
            cv_rect(150 + i * 2, 22 + i, 2, 12 - 2 * i, R4_GREEN);
    } else {
        cv_rect(150, 24, 10, 10, R4_DIM);                      /* stopped square */
    }
    if (ui.st.flags & R4P_ST_REC) {                            /* record disc */
        static const uint8_t HW[11] = {2, 3, 4, 4, 5, 5, 5, 4, 4, 3, 2};
        for (i = 0; i < 11; i++)
            cv_rect(172 - HW[i], 23 + i, 2 * HW[i], 1, R4_RED);
    }
    if (ui.st.flags & R4P_ST_OVERDUB)
        ptext(184, 24, &FONT_XS, "OD", R4_RED);
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' '; t[4] = ui.octave < 0 ? '-' : '+'; t[5] = (char)('0' + (ui.octave < 0 ? -ui.octave : ui.octave)); t[6] = 0;
    ptext(232 - pwidth(&FONT_XS, t), 26, &FONT_XS, t, ui.octave ? accent(100) : R4_DIM);
    cv_blit(0, 200);
}

/* -------------------------------------------------------------- toast --- */
static void draw_toast(void)
{
    int32_t w = pwidth(&FONT_B, ui.toast) + 28, x;
    if (w > 224) w = 224;
    if (w < 80) w = 80;
    x = (240 - w) / 2;
    cv_begin((uint32_t)w, 30, R4_BG);
    cv_rect(0, 0, w, 30, accent(100));
    cv_rect(2, 2, w - 4, 26, R4_BG);
    ptext((w - pwidth(&FONT_B, ui.toast)) / 2, 7, &FONT_B, ui.toast, R4_TEXT);
    cv_blit((uint32_t)x, 84);
}

/* ---------------------------------------------------- not connected ----- */
static void draw_nc(int dots_only)
{
    static const char *const DOTS[4] = {".", "..", "...", ""};
    char t[8];
    if (!dots_only) {
        cv_begin(240, 120, R4_BG);
        ptext(120 - pwidth(&FONT_L, "RUN4") / 2, 32, &FONT_L, "RUN4", RGB(255, 140, 40));
        cv_rect(60, 80, 120, 2, R4_EDGE);
        ptext(120 - pwidth(&FONT_B, "NOT CONNECTED") / 2, 92, &FONT_B, "NOT CONNECTED", R4_TEXT);
        cv_blit(0, 0);
        cv_begin(240, 120, R4_BG);
        ptext(120 - pwidth(&FONT_XS, "OPEN RUN4 AND ENABLE THE FM-1") / 2, 40, &FONT_XS, "OPEN RUN4 AND ENABLE THE FM-1", R4_LABEL);
        ptext(120 - pwidth(&FONT_XS, "THE KEYS STILL PLAY NOTES") / 2, 60, &FONT_XS, "THE KEYS STILL PLAY NOTES", R4_DIM);
        t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' '; t[4] = ui.octave < 0 ? '-' : '+'; t[5] = (char)('0' + (ui.octave < 0 ? -ui.octave : ui.octave)); t[6] = 0;
        ptext(120 - pwidth(&FONT_XS, t) / 2, 100, &FONT_XS, t, ui.octave ? RGB(255, 140, 40) : R4_DIM);
        cv_blit(0, 120);
    }
    cv_begin(40, 16, R4_BG);
    ptext(0, 0, &FONT_B, DOTS[ui.dots & 3], RGB(255, 140, 40));
    cv_blit(100, 114);
}

/* --------------------------------------------------------------- list --- */
#define LIST_ROW 23
static void draw_list(void)
{
    const r4p_list_t *l = &ui.list;
    char t[24], u[12];
    uint16_t ac = scale(0, l->r * 2, l->g * 2, l->b * 2, 100);
    uint16_t fg = (l->r * 299 + l->g * 587 + l->b * 114) / 1000 >= 56 ? RGB(10, 10, 14) : RGB(250, 250, 250);
    int i;
    cv_begin(240, 34, ac);
    cv_rect(0, 32, 240, 2, scale(0, l->r * 2, l->g * 2, l->b * 2, 55));
    ptext(8, 8, &FONT_B, l->title, fg);
    t[0] = 0; u_int((unsigned)l->sel + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int((unsigned)l->total, u); u_cat(t, u);
    ptext(232 - pwidth(&FONT_B, t), 8, &FONT_B, t, fg);
    cv_blit(0, 0);
    for (i = 0; i < R4P_LIST_ROWS; i++) {
        int idx = l->first + i, has = i < l->n, sel = has && idx == l->sel;
        cv_begin(240, LIST_ROW, R4_BG);
        if (sel)
            cv_rect(4, 1, 232, LIST_ROW - 2, ac);
        else if (has)
            cv_rect(4, LIST_ROW - 1, 232, 1, R4_TILE);
        if (has)
            ptext(14, 3, &FONT_B, l->item[i], sel ? fg : R4_TEXT);
        cv_blit(0, (uint32_t)(36 + i * LIST_ROW));
    }
    cv_begin(240, 40, R4_BG);
    cv_rect(0, 0, 240, 1, R4_EDGE);
    ptext(8, 8, &FONT_XS, "SELECT MOVE   HOME OK   OTHER BACK", R4_LABEL);
    cv_blit(0, 200);
}

/* ------------------------------------------------- big value (1.2) --- */
/* the slot ui.big1 - 1 over the tile area (x 4..235, y 38..193), in two bands: where (path or title), name, the value
 * large; then a wide bar and the position */
static void draw_big(void)
{
    const r4p_slot_t *s = &PG->slot[(ui.big1 - 1u) & 3u];
    char t[R4P_STR_MAX + 1], u[12];
    const felucca_font_t *f = pwidth(&FONT_L, s->text) <= 212 ? &FONT_L : pwidth(&FONT_M, s->text) <= 212 ? &FONT_M : &FONT_B;
    int32_t bw = 200, bx = 16, by = 14, fill, n, i, c;
    cv_begin(232, 78, R4_TILE);
    cv_rect(0, 0, 232, 2, accent(100));
    cv_rect(0, 0, 3, 78, accent(100));
    cv_rect(229, 0, 3, 78, accent(100));
    fit(t, PG->path[0] ? PG->path : PG->title, &FONT_XS, 212);
    ptext(12, 6, &FONT_XS, t, R4_LABEL);
    fit(t, s->name, &FONT_B, 212);
    ptext(12, 22, &FONT_B, t, accent(100));
    ptext(12, f == &FONT_L ? 42 : f == &FONT_M ? 48 : 54, f, s->text, R4_TEXT);
    cv_blit(4, 38);
    cv_begin(232, 78, R4_TILE);
    cv_rect(0, 0, 3, 78, accent(100));
    cv_rect(229, 0, 3, 78, accent(100));
    cv_rect(0, 76, 232, 2, accent(100));
    switch (s->kind) {
    case R4P_KIND_UNI:
        cv_rect(bx, by, bw, 14, R4_EDGE);
        cv_rect(bx, by, s->value * bw / R4P_V14_MAX, 14, accent(100));
        break;
    case R4P_KIND_BI:
        cv_rect(bx, by, bw, 14, R4_EDGE);
        c = bx + bw / 2;
        fill = ((int32_t)s->value - 8192) * (bw / 2) / 8191;
        if (fill >= 0) cv_rect(c, by, fill, 14, accent(100));
        else cv_rect(c + fill, by, -fill, 14, accent(100));
        cv_rect(c - 1, by - 3, 2, 20, R4_TEXT);
        break;
    case R4P_KIND_ENUM:
        n = (s->value >> 7) & 0x7F;
        n = n > 16 ? 16 : n < 2 ? 2 : n;
        for (i = 0; i < n; i++) {
            int32_t x0 = bx + i * bw / n, x1 = bx + (i + 1) * bw / n - 3;
            cv_rect(x0, by, x1 - x0, 14, i == (s->value & 0x7F) ? accent(100) : R4_EDGE);
        }
        break;
    case R4P_KIND_TOGGLE:
        cv_rect(bx, by - 4, 60, 22, s->value ? accent(100) : R4_EDGE);
        cv_rect(s->value ? bx + 40 : bx + 4, by, 16, 14, s->value ? on_accent() : R4_DIM);
        break;
    default:
        break;
    }
    t[0] = 'K'; t[1] = (char)('1' + ((ui.big1 - 1u) & 3u)); t[2] = 0;
    ptext(16, 44, &FONT_XS, t, R4_DIM);
    if (PG->item_count) {
        t[0] = 0; u_int((unsigned)PG->item_idx + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int(PG->item_count, u); u_cat(t, u);
        ptext(216 - pwidth(&FONT_XS, t), 44, &FONT_XS, t, R4_LABEL);
    }
    cv_blit(4, 116);
}
/* a knob K1..K4 was turned (app.c; a 1.2 host, RUN4 mode): frame its tile until its VALUE comes */
static void ui_touch(uint32_t slot)
{
    if (!ui.v12 || !host_page() || ui.list_open || !ui.connected)
        return;
    if (ui.touch1 && ui.touch1 != slot + 1u)
        ui.dirty |= (uint16_t)(D_TILE0 << (ui.touch1 - 1u));
    if (ui.touch1 != slot + 1u)
        ui.dirty |= (uint16_t)(D_TILE0 << slot);
    if (PG->cursor < R4P_NSLOTS && PG->cursor != slot)
        ui.dirty |= (uint16_t)(D_TILE0 << PG->cursor);        /* the host's frame gives way meanwhile */
    ui.touch1 = (uint8_t)(slot + 1u);
    ui.touch_at = plat_ms();
}
static void big_end(void)
{
    if (!ui.big1)
        return;
    ui.big1 = 0;
    ui.dirty |= D_TILES | D_GAP;
}

/* ---------------------------------------------------------------- draw --- */
static void sp_draw_head(void);             /* synth_ui.c: Synth mode's own look (header, the band: D_TILE0 / 1, tiles: 2 / 3) */
static void sp_draw_band(int full);
static void sp_draw_tiles(void);
static void ui_draw(void)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.toast_until && (int32_t)(now - ui.toast_until) >= 0) {
        ui.toast_until = 0;
        ui.dirty |= D_TOAST_UNDER | D_TBAND;                   /* the band under the toast */
    }
    if (ui.big1 && (int32_t)(now - ui.big_until) >= 0) {       /* 1.2: the big value's time is up: the tiles again */
        big_end();
        ui.touch1 = 0;
    }
    if (ui.touch1 && !ui.big1 && now - ui.touch_at > 1500u) {  /* a turn whose VALUE never came */
        ui.dirty |= (uint16_t)(D_TILE0 << (ui.touch1 - 1u));
        if (PG->cursor < R4P_NSLOTS)
            ui.dirty |= (uint16_t)(D_TILE0 << PG->cursor);
        ui.touch1 = 0;
    }
    if (ui.list_open && ui.connected && !ui.setup) {
        if (ui.dirty & (D_ALL | D_TILES | D_FOOT | D_HEAD | D_TOAST))
            draw_list();
        ui.dirty = 0;
        return;
    }
    if (!ui.connected && !ui.local && !ui.setup) {
        uint8_t ph = (uint8_t)((now / 500u) & 3u);
        if (ph != ui.dots) {
            ui.dots = ph;
            ui.dirty |= D_DOTS;
        }
        if (ui.dirty & D_CLEAR) {
            lcd_fill(0, 0, 240, 240, R4_BG);
            draw_nc(0);
        } else if (ui.dirty & D_HEAD) {                        /* (octave changed) */
            draw_nc(0);
        } else if (ui.dirty & D_DOTS) {
            draw_nc(1);
        }
        ui.dirty = 0;
        return;
    }
    if (ui.dirty & D_CLEAR)
        lcd_fill(0, 0, 240, 240, R4_BG);
    if (ui.dirty & D_HEAD)
        ui.local ? sp_draw_head() : draw_head();
    if (ui.local) {
        if (ui.dirty & (D_TILE0 | D_TILE0 << 1 | D_CLEAR))
            sp_draw_band(1);
        else if (ui.dirty & D_LIVE) {
            sp_draw_band(0);
            if (ui.toast_until)
                ui.dirty |= D_TOAST;                           /* (the live band drew under the toast: it again) */
        }
        if (ui.dirty & (D_TILE0 << 2 | D_TILE0 << 3))
            sp_draw_tiles();
    } else if (ui.big1 && host_page() && ui.connected) {      /* 1.2: the big value over the tiles */
        if (ui.dirty & (D_BIG | D_TOAST_UNDER | D_CLEAR)) {
            draw_big();
            if (tv_layout()) {                                 /* (1.3: the band's top and the tile row's foot around it) */
                lcd_fill(0, 34, 240, 4, R4_BG);
                lcd_fill(0, 194, 240, 6, R4_BG);
            }
        }
    } else if (tv_layout()) {                                  /* 1.3: the tape view (tape_ui.c) */
        tv_draw(now);
    } else {
        if (ui.dirty & D_GAP)
            lcd_fill(4, 114, 232, 4, R4_BG);
        for (i = 0; i < 4; i++)
            if (ui.dirty & (D_TILE0 << i))
                draw_tile(i);
    }
    if (ui.dirty & D_FOOT)
        draw_foot();
    if ((ui.dirty & (D_TOAST | D_TOAST_UNDER)) && ui.toast_until)
        draw_toast();
    ui.dirty &= (uint16_t)~(D_HEAD | D_TILES | D_FOOT | D_TOAST | D_CLEAR | D_DOTS | D_BIG | D_GAP | D_LIVE | D_TBAND);
}

/* --------------------------------------------- host frames into state --- */
static void ui_toast(const char *text, unsigned dur100ms)
{
    if (ui.toast_until)
        ui.dirty |= D_TOAST_UNDER | D_TBAND;                   /* an older toast may be wider: repaint under it */
    strcpy(ui.toast, text);
    ui.toast_until = plat_ms() + 100u * (dur100ms ? dur100ms : 15u);
    ui.dirty |= D_TOAST;
}
static void ui_apply(const r4p_msg_t *m)
{
    int i;
    switch (m->cmd) {
    case R4P_LIST:                                             /* total 0 closes the list */
        ui.list = m->u.list;
        ui.list_open = m->u.list.total > 0;
        ui.dirty = D_ALL;
        break;
    case R4P_PAGE:
        ui.page = m->u.page;
        big_end();                                             /* (1.2: a new page ends the big value) */
        ui.touch1 = 0;
        ui.dirty |= D_HEAD | D_TILES | D_FOOT;
        break;
    case R4P_VALUE: {
        uint32_t now = plat_ms();
        i = m->u.value.slot;
        if (ui.page.slot[i].value != m->u.value.value || strcmp(ui.page.slot[i].text, m->u.value.text)) {
            ui.page.slot[i].value = m->u.value.value;
            strcpy(ui.page.slot[i].text, m->u.value.text);
            ui.dirty |= (uint16_t)(D_TILE0 << i);
            if (ui.big1 == (uint8_t)(i + 1))
                ui.dirty |= D_BIG;
        }
        /* 1.2: the big value for a slot just turned (its VALUE within 1.5 s) or flagged; a VALUE of the slot shown keeps it */
        if (ui.v12 && host_page() && !ui.list_open &&
            ((m->u.value.flags & R4P_VF_BIG) || (ui.touch1 == (uint8_t)(i + 1) && now - ui.touch_at <= 1500u) ||
             ui.big1 == (uint8_t)(i + 1))) {
            if (ui.big1 != (uint8_t)(i + 1))
                ui.dirty |= D_BIG;
            ui.big1 = (uint8_t)(i + 1);
            ui.big_until = now + 1200u;
        }
        break;
    }
    case R4P_FOCUS:                                            /* 1.2: the focus moves; the tiles it leaves and enters */
        if (ui.page.cursor < R4P_NSLOTS)
            ui.dirty |= (uint16_t)(D_TILE0 << ui.page.cursor);
        ui.page.cursor = m->u.focus.cursor;
        ui.page.item_idx = m->u.focus.item_idx;
        ui.page.item_count = m->u.focus.item_count;
        if (ui.page.cursor < R4P_NSLOTS)
            ui.dirty |= (uint16_t)(D_TILE0 << ui.page.cursor);
        ui.dirty |= D_FOOT | (ui.big1 ? D_BIG : 0u);
        break;
    case R4P_STATUS:
        if (memcmp(&ui.st, &m->u.status, sizeof ui.st)) {
            ui.st = m->u.status;
            ui.dirty |= D_FOOT;
        }
        break;
    case R4P_TOAST:
        ui_toast(m->u.toast.text, m->u.toast.dur100ms);
        break;
    case R4P_TAPE_WAVE: case R4P_TAPE_POS: case R4P_METERS:   /* 1.3 */
        tv_apply(m);
        break;
    default:
        break;
    }
}
