/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, RUN4 mode: the scene page (protocol 1.6, docs/PROTOCOL.md "Scene page"). RUN4's Sets as a grid: the white
 * keys 1..16 recall scene bank * 16 + 1 .. +16, OCT- / OCT+ change the bank. The host sends the count and the short
 * names (SCENES) and which scene is active and which is queued (SCENE_STATE); the device sends the pick (SCENE_PICK).
 * Opened with SEL + SEQ (app.c), left with any page button. Part of the unity build, after tape_ui.c (ui.c's helpers). */

#define SCN_NONE 255u
#define SCN_BANK 16u
#define SCN_QUEUE_MS 4000u                    /* a queued scene the host never confirmed stops blinking after this */
static struct {
    uint8_t total;                            /* scenes the host has (0 = none: the page is not offered) */
    uint8_t active, queued;                   /* SCN_NONE = none */
    uint8_t bank;
    uint8_t ph;                               /* the blink phase last drawn */
    uint32_t keys;                            /* the keys seen so far (a press is a new bit) */
    uint32_t queued_at;
    char name[R4P_SCENE_MAX][R4P_SCENE_NAME + 1];
} scn;

static int ctl_queue(uint16_t e);             /* app.c: the event queue behind the buttons */
#define SCN_PICK_EVT 0x4000u                  /* txq: a SCENE_PICK, 0x4000 | index */

static unsigned scn_banks(void) { return ((unsigned)scn.total + SCN_BANK - 1u) / SCN_BANK; }
static void scn_reset(void)
{
    memset(&scn, 0, sizeof scn);
    scn.active = scn.queued = SCN_NONE;
    ui.scn = 0;
}
static int scn_ready(void) { return scn.total > 0; }
/* the rank of key k among the white keys (0..15, left to right), -1 for a black key (the platform's list) */
static const uint8_t SCN_WHITE_KEYS[16] = PLAT_WHITE_KEYS;
static int scn_white(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (SCN_WHITE_KEYS[i] == k)
            return (int)i;
    return -1;
}
static void scn_open(void)
{
    ui.scn = 1;
    scn.bank = scn.active < scn.total ? (uint8_t)(scn.active / SCN_BANK) : 0;
    scn.keys = touch_keys();                  /* keys already down are not picks */
    ui.dirty = D_ALL;
}
static void scn_close(void)
{
    if (!ui.scn)
        return;
    ui.scn = 0;
    ui.dirty = D_ALL;
}
static void scn_bank(int d)
{
    int b = (int)scn.bank + d, last = (int)scn_banks() - 1;
    b = b < 0 ? 0 : b > last ? last : b;
    if (b != scn.bank) {
        scn.bank = (uint8_t)b;
        ui.dirty |= D_HEAD | D_TILES;
    }
}
static void scn_apply(const r4p_msg_t *m)
{
    unsigned i;
    if (m->cmd == R4P_SCENES) {
        const r4p_scenes_t *c = &m->u.scenes;
        scn.total = (uint8_t)c->total;
        for (i = 0; i < c->n; i++)
            strcpy(scn.name[c->first + i], c->name[i]);
        for (i = c->total; i < R4P_SCENE_MAX; i++)
            scn.name[i][0] = 0;
        if (scn.active != SCN_NONE && scn.active >= scn.total)
            scn.active = SCN_NONE;
        if (scn.queued != SCN_NONE && scn.queued >= scn.total)
            scn.queued = SCN_NONE;
        if (scn.bank >= scn_banks())
            scn.bank = scn.total ? (uint8_t)(scn_banks() - 1u) : 0;
        if (!scn.total)
            scn_close();
    } else if (m->cmd == R4P_SCENE_STATE) {
        scn.active = m->u.scene_state.active == R4P_SCENE_NONE ? SCN_NONE : (uint8_t)m->u.scene_state.active;
        scn.queued = m->u.scene_state.queued == R4P_SCENE_NONE ? SCN_NONE : (uint8_t)m->u.scene_state.queued;
        scn.queued_at = plat_ms();
    }
    if (ui.scn)
        ui.dirty |= D_HEAD | D_TILES;
}
/* the keys of this frame: a white key newly down picks its scene (queued behind the buttons; a full queue leaves the
 * key unseen for the next frame) */
static void scn_keys(uint32_t keys)
{
    uint32_t k, now = plat_ms();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = scn_white(k);
        if (!((keys ^ scn.keys) & bit))
            continue;
        if (!(keys & bit)) {
            scn.keys &= ~bit;
            continue;
        }
        if (w >= 0 && w < (int)SCN_BANK && scn.bank * SCN_BANK + (unsigned)w < scn.total) {
            unsigned idx = scn.bank * SCN_BANK + (unsigned)w;
            if (!ctl_queue((uint16_t)(SCN_PICK_EVT | idx)))
                continue;
            scn.queued = (uint8_t)idx;            /* blinks until the host says it landed (or SCN_QUEUE_MS) */
            scn.queued_at = now;
            ui.dirty |= D_TILES;
        }
        scn.keys |= bit;
    }
}
/* the white keys' lights: the active scene on, the queued one blinking, the others dim */
static void scn_lights(uint32_t *lk, uint32_t *dk, uint8_t blink)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        int w = scn_white(k);
        unsigned idx;
        if (w < 0 || w >= (int)SCN_BANK)
            continue;
        idx = scn.bank * SCN_BANK + (unsigned)w;
        if (idx >= scn.total)
            continue;
        if (idx == scn.queued) {
            if (blink)
                *lk |= 1u << k;
        } else if (idx == scn.active) {
            *lk |= 1u << k;
        } else {
            *dk |= 1u << k;
        }
    }
}
static void scn_tick(uint32_t now)
{
    uint8_t ph = (uint8_t)((now / 250u) & 1u);
    if (scn.queued != SCN_NONE) {
        if (now - scn.queued_at >= SCN_QUEUE_MS) {
            scn.queued = SCN_NONE;
            ui.dirty |= D_TILES;
        } else if (ph != scn.ph) {
            ui.dirty |= D_TILES;
        }
    }
    scn.ph = ph;
}

#define SCN_ROW 41
static void scn_draw(void)
{
    char t[R4P_SCENE_NAME + 1], u[12];
    uint16_t ac = accent(100);
    int r, c;
    cv_begin(240, 36, R4_BG);
    cv_rect(0, 0, 240, 34, ac);
    cv_rect(0, 32, 240, 2, accent(55));
    ptext(8, 8, &FONT_B, "SCENES", on_accent());
    t[0] = 0; u_cat(t, "BANK "); u_int((unsigned)scn.bank + 1u, u); u_cat(t, u);
    u_cat(t, "/"); u_int(scn_banks(), u); u_cat(t, u);
    ptext(232 - pwidth(&FONT_B, t), 8, &FONT_B, t, on_accent());
    cv_blit(0, 0);
    for (r = 0; r < 4; r++) {
        cv_begin(240, SCN_ROW, R4_BG);
        for (c = 0; c < 4; c++) {
            unsigned idx = scn.bank * SCN_BANK + (unsigned)(r * 4 + c);
            int32_t x = 4 + c * 59;
            int act, que;
            char nm[R4P_SCENE_NAME + 1];
            if (idx >= scn.total)
                continue;
            act = idx == scn.active;
            que = idx == scn.queued;
            cv_rect(x, 0, 57, 39, act ? ac : R4_TILE);
            if (que && scn.ph) {                                   /* queued: its frame blinks */
                cv_rect(x, 0, 57, 3, ac); cv_rect(x, 36, 57, 3, ac);
                cv_rect(x, 0, 3, 39, ac); cv_rect(x + 54, 0, 3, 39, ac);
            } else if (!act) {
                cv_rect(x, 38, 57, 1, R4_EDGE);
            }
            u_int(idx + 1u, u);
            ptext(x + 5, 4, &FONT_B, u, act ? on_accent() : R4_TEXT);
            fit(nm, scn.name[idx], &FONT_XS, 48);
            ptext(x + 5, 25, &FONT_XS, nm, act ? on_accent() : R4_LABEL);
        }
        cv_blit(0, (uint32_t)(36 + r * SCN_ROW));
    }
    cv_begin(240, 40, R4_BG);
    cv_rect(0, 0, 240, 1, R4_EDGE);
    ptext(8, 6, &FONT_XS, "WHITE KEYS: RECALL   OCT- OCT+: BANK", R4_LABEL);
    ptext(8, 22, &FONT_XS, "ANY PAGE BUTTON: BACK", R4_DIM);
    cv_blit(0, 200);
}
