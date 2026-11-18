/* SPDX-License-Identifier: GPL-3.0-only
 * The tape view's live frames (src/tape_ui.c) against a screen that sends like the FM-1's: a blit only starts its
 * transfer and the panel reads the buffer while the next one is being filled, until lcd_sync or the next blit's window
 * (base/device/lcd.c). The host simulator's screen copies at once and cannot show it. The test draws the band whole,
 * moves the playhead across the waveform with the meters changing every frame (as RUN4 sends them), and checks after
 * every frame that the screen is exactly what a fresh whole draw gives: the columns the playhead left are the
 * waveform again, not the meters' tiles (the grey grid owners saw, firmware 0.9). `make tape`. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "r4p.h"

/* ---- what tape_ui.c takes from ui.c / synth_ui.c / gfx.c, drawn plainly ---- */
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define R4_BG RGB(0, 0, 0)
#define R4_LABEL RGB(150, 150, 158)
#define R4_EDGE RGB(34, 34, 40)
#define R4_DIM RGB(74, 74, 82)
#define R4_TEXT RGB(236, 236, 240)
#define R4_RED RGB(232, 76, 60)
#define R4_TILE RGB(40, 40, 46)
#define SY_BAR RGB(120, 120, 128)
#define SY_MID RGB(90, 90, 96)
#define BAND_Y 34
#define BAND_H 82
#define SC_G 16
enum { D_ALL = 0xFFFF, D_CLEAR = 1, D_GAP = 2, D_TBAND = 4, D_TOAST = 8, D_TILE0 = 16 };
typedef struct { int h; } felucca_font_t;
static const felucca_font_t FONT_XS = {7}, FONT_B = {12};
static struct { uint32_t dirty, toast_until; char toast[32]; int connected, list_open; } ui = {0, 0, "", 1, 0};
static struct { r4p_slot_t slot[4]; } page_, *PG = &page_;
static uint16_t sy_accent(int pct) { return RGB(232 * pct / 100, 136 * pct / 100, 30 * pct / 100); }
static int host_page(void) { return 1; }
static int hl_tile(void) { return -1; }
static void row_tile(int32_t x, const r4p_slot_t *s, int hot, int32_t base) { (void)x, (void)s, (void)hot, (void)base; }
static int32_t pwidth(const felucca_font_t *f, const char *s) { (void)f; return (int32_t)strlen(s) * 5; }
static void fit(char *t, const char *s, const felucca_font_t *f, int32_t w) { (void)f, (void)w; strcpy(t, s); }
static uint32_t plat_ticks24(void) { return 0; }

static uint16_t fb[240 * 240];
/* the transfer in flight: the panel has its window, the bytes are read when it completes */
static struct { int on; uint32_t x, y, w, h; const uint16_t *px; } dma;
static void lcd_sync(void)
{
    uint32_t i, j;
    if (!dma.on)
        return;
    for (j = 0; j < dma.h; j++)
        for (i = 0; i < dma.w; i++)
            fb[(dma.y + j) * 240u + dma.x + i] = dma.px[j * dma.w + i];
    dma.on = 0;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    lcd_sync();                                   /* lcd_window: the last transfer finishes first */
    dma.on = 1, dma.x = x, dma.y = y, dma.w = w, dma.h = h, dma.px = px;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    lcd_sync();
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            fb[j * 240u + i] = c;
}
static uint16_t cv_px[240 * 144];
static uint32_t cv_w, cv_h;
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i;
    lcd_sync();                                   /* (gfx.c: the last blit may still read cv_px) */
    cv_w = w, cv_h = h;
    for (i = 0; i < w * h; i++)
        cv_px[i] = bg;
}
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && (uint32_t)i < cv_w && (uint32_t)j < cv_h)
                cv_px[(uint32_t)j * cv_w + (uint32_t)i] = c;
}
static void ptext(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    for (; *s; s++, x += 5)                       /* a block a letter: enough to compare screens */
        cv_rect(x, y, 4, f->h, c);
}
static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }
static uint16_t sc_strip[2][SC_G * BAND_H];

#include "tape_ui.c"

static int fails, checks;
static void band(uint16_t *out)
{
    uint32_t r;
    lcd_sync();
    for (r = 0; r < BAND_H; r++)
        memcpy(out + r * 232u, fb + (BAND_Y + r) * 240u + 4u, 232u * 2u);
}
int main(void)
{
    static uint16_t got[232 * BAND_H], want[232 * BAND_H];
    r4p_msg_t m;
    uint32_t now = 0, f;
    int i;
    tv_reset();
    memset(&m, 0, sizeof m);
    m.cmd = R4P_TAPE_WAVE;
    m.u.wave.track = 1;
    strcpy(m.u.wave.label, "T2 LOOP");
    m.u.wave.n = R4P_WAVE_MAX;
    for (i = 0; i < R4P_WAVE_MAX; i++)
        m.u.wave.peak[i] = (uint8_t)(20 + (i * 37) % 100);
    tv_apply(&m);
    m.cmd = R4P_TAPE_POS;
    m.u.tpos.play = 0, m.u.tpos.in = 2000, m.u.tpos.out = 14000, m.u.tpos.flags = R4P_TP_PLAY;
    tv_apply(&m);
    ui.dirty = D_CLEAR;
    tv_draw(now);
    ui.dirty = 0;
    for (f = 1; f <= 400; f++) {                  /* 16 s at 25 frames: the head across the band, slow and fast */
        now += TV_LIVE_MS;
        memset(&m, 0, sizeof m);                  /* (the message's fields share a union) */
        m.cmd = R4P_TAPE_POS;
        m.u.tpos.in = 2000, m.u.tpos.out = 14000, m.u.tpos.flags = R4P_TP_PLAY;
        m.u.tpos.play = (uint16_t)((f * (f < 200 ? 97u : 450u)) % 16384u);   /* a slow loop, then a short one (5 columns a frame) */
        tv_apply(&m);
        memset(&m, 0, sizeof m);
        m.cmd = R4P_METERS;
        for (i = 0; i < R4P_NMETERS; i++)
            m.u.meters.level[i] = (uint8_t)((f * (7u + (uint32_t)i * 13u)) % 128u);
        m.u.meters.clip = (uint8_t)(f & 1u);
        tv_apply(&m);
        tv_draw(now);
        band(got);
        tv_paint();                               /* a fresh draw of the same state */
        memcpy(want, cv_px, sizeof want);
        checks++;
        if (memcmp(got, want, sizeof got)) {
            int bad = 0, first = -1, k;
            for (k = 0; k < 232 * BAND_H; k++)
                if (got[k] != want[k]) { bad++; if (first < 0) first = k; }
            if (fails++ < 3)
                printf("FAIL frame %u: %d pixels differ from a fresh draw (first column %d row %d)\n", f, bad,
                       first % 232, first / 232);
        }
    }
    if (tv.frames < 300u || tv.bytes / tv.frames > 232u * BAND_H)   /* live frames, not whole bands */
        printf("FAIL the band was sent whole every frame\n"), fails++;
    printf("tape view: %d frames compared with a fresh draw, %d differ (%u band frames, %u bytes)\n", checks, fails,
           tv.frames, tv.bytes);
    return fails != 0;
}
