/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, AMB mode: the garden of AMB-1 (src/amb/amb.c, Charles Vestal, MPL-2.0) with RUN4 CTL's pages. You tend it,
 * you don't play it: PRESETS the piece, the keys take notes out of the scale and put them back, PLAY grows / rests,
 * REC freezes, EDIT moves to the next chord, the page buttons open the layers' pages (four values each, on K1..K4).
 * The store (OBJ_AMB): every piece as it was left, the piece playing, Tune and MIDI out. Part of the unity build, after
 * synth_ui.c (its tiles and colours) and before app.c's input. */
#include "amb/amb.h"

enum { AV_GARDEN, AV_PAD, AV_BELLS, AV_BASS, AV_PULSE, AV_SPACE, AV_KEY, AV_N };
static const char *const AV_NAME[AV_N] = {"GARDEN", "PAD", "BELLS", "BASS", "PULSE", "SPACE", "KEY"};
static const uint8_t AV_BTN[AV_N] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO};
#define AK_SCALE 100                         /* the KEY page's fourth knob: the scale (ALGORITHM does the same) */
static const uint8_t AV_KNOB[AV_N][4] = {
    {P_TEMPO, P_EVOLVE, P_MOTION, P_DRIFT}, {P_PAD, P_SWELL, P_BRIGHT, P_PADTONE}, {P_BELLS, P_DENSITY, P_BTONE, P_BOCT},
    {P_BASS, P_BASSOCT, P_GLIDE, P_FIFTH}, {P_PULSE, P_HITS, P_SWING, P_HATS}, {P_ECHO, P_ETIME, P_REVERB, P_SIZE},
    {P_ROOT, P_TUNE, P_MIDI, AK_SCALE}};
static const char *const AV_LABEL[AV_N][4] = {
    {"TEMPO", "EVOLVE", "MOTION", "DRIFT"}, {"LEVEL", "SWELL", "BRIGHT", "COLOUR"}, {"LEVEL", "DENSITY", "TONE", "OCTAVE"},
    {"LEVEL", "OCTAVE", "GLIDE", "FIFTH"}, {"LEVEL", "HITS", "SWING", "HATS"}, {"ECHO", "ECHO TIME", "REVERB", "SIZE"},
    {"ROOT", "TUNE", "MIDI OUT", "SCALE"}};

/* ------------------------------------------------------------- the store --- */
/* OBJ_AMB, layout 1 (476 B). A store of another magic, layout or size, or values out of their ranges, gives the
 * recipes (a later layout migrates its older ones here, as SETUP's do). `boot`: AMB mode was the mode when the FM-1
 * was last left; SETUP keeps SYNTH then, so an older firmware (two modes) boots Synth mode with SETUP intact. */
#define AM_MAGIC 0x4D413452u                 /* "R4AM" */
#define AM_VER 1u
typedef struct { int16_t par[P_NPARAMS]; uint16_t pool; } am_slot_t;
typedef struct {
    uint32_t magic;
    uint8_t ver, piece, boot, rsv0;
    int16_t tune;                            /* Tune and MIDI out belong to the player, not the piece */
    uint8_t midi, rsv1;
    am_slot_t slot[EN_NPIECES];              /* each piece as it was left; slot[piece] is the one playing */
} am_store_t;
_Static_assert(sizeof(am_store_t) == 476u, "AMB store layout");
static am_store_t am;
static struct {
    uint8_t view;
    uint8_t dirty;                           /* changed since saved */
    uint8_t want_grow;                       /* it grows when the mode comes in (it was growing when left) */
    uint8_t reset_done;                      /* SAVE held: the piece back to its recipe (this hold) */
    uint32_t edit_at, grow_at, save_at, live_at, sig;
    uint32_t saves, frames, bytes;           /* host-simulator checks */
    uint8_t mode_hold;                       /* (app.c) a mode switch: the render silent */
} amr = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#define AM_CUR (&am.slot[am.piece % EN_NPIECES])
#define AM_GROW_MS 700u                      /* entering AMB mode, it grows after this (a mode passed through stays quiet) */
#define AM_RESET_MS 2000u                    /* SAVE held this long: the piece back to its recipe */
#define AM_SAVE_MS 4000u                     /* a change saved this long after, while it rests */

static int am_pool_ok(uint32_t p)
{
    uint32_t n = 0, i;
    for (i = 0; i < 12u; i++)
        n += (p >> i) & 1u;
    return (p & 1u) && !(p >> 12) && n >= 3u;
}
static int am_slot_ok(const am_slot_t *s)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *q = en_param_info(i);
        if (s->par[i] < q->lo || s->par[i] > q->hi)
            return 0;
    }
    return am_pool_ok(s->pool);
}
static void am_recipe(int i, am_slot_t *s) { en_piece(i, s->par, &s->pool); }
static void am_defaults(void)
{
    int i;
    memset(&am, 0, sizeof am);
    am.magic = AM_MAGIC;
    am.ver = AM_VER;
    for (i = 0; i < EN_NPIECES; i++)
        am_recipe(i, &am.slot[i]);
    am.tune = 0;
    am.midi = 0;                             /* MIDI OUT off: nothing reaches RUN4 or a DAW unasked */
}
static void am_load(void)
{
    am_store_t t;
    int n = plat_store_load(OBJ_AMB, &t, sizeof t), i, ok;
    am_defaults();
    ok = n == (int)sizeof t && t.magic == AM_MAGIC && t.ver == AM_VER && t.piece < EN_NPIECES && t.boot <= 1u &&
         t.tune >= -50 && t.tune <= 50 && t.midi <= 2u;
    for (i = 0; ok && i < EN_NPIECES; i++)
        ok = am_slot_ok(&t.slot[i]);
    if (ok)
        am = t;
    amr.dirty = 0;
}
static int am_playing(void) { return en_playing; }
static void am_save(void)
{
    if (sy_hold_store)                       /* (a USB backup streams: saved after it) */
        return;
    if (plat_store_save(OBJ_AMB, &am, sizeof am) == 0)
        amr.dirty = 0;
    amr.saves++;
}
static int am_par(int p)
{
    if (p == P_TUNE) return am.tune;
    if (p == P_MIDI) return am.midi;
    return AM_CUR->par[p];
}
static void am_apply(void)                   /* the piece playing, every value, to the engine */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, am_par(p));
    en_pool(AM_CUR->pool);
}

/* ----------------------------------------------------------------- page --- */
static void am_touch(void)
{
    amr.dirty = 1;
    amr.edit_at = plat_ms();
}
static const char *am_scale_name(void)
{
    int m = AM_CUR->par[P_MODE];
    return AM_CUR->pool == EN_MODE_POOL[m] ? EN_MODE_NAME[m] : "Custom";
}
static void am_upper(char *t, const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s[i] && i < max; i++)
        t[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    t[i] = 0;
}
static void am_value(int p, r4p_slot_t *s)   /* a knob's tile */
{
    char t[16];
    if (p == AK_SCALE) {
        s->kind = R4P_KIND_ENUM;
        s->value = (uint16_t)((EN_NMODES + 1) << 7 | (AM_CUR->pool == EN_MODE_POOL[AM_CUR->par[P_MODE]] ? AM_CUR->par[P_MODE] : EN_NMODES));
        am_upper(s->text, am_scale_name(), R4P_STR_MAX);
        return;
    }
    {
        const en_param_t *q = en_param_info(p);
        int v = am_par(p), span = q->hi - q->lo;
        if (p == P_ETIME || p == P_PADTONE || p == P_MIDI || p == P_ROOT) {
            s->kind = R4P_KIND_ENUM;
            s->value = (uint16_t)((span + 1) << 7 | (v - q->lo));
        } else if (q->lo < 0) {
            s->kind = R4P_KIND_BI;
            s->value = (uint16_t)(v < 0 ? 8192 - v * 8192 / q->lo : 8192 + v * 8191 / q->hi);
        } else {
            s->kind = R4P_KIND_UNI;
            s->value = (uint16_t)(span ? (v - q->lo) * R4P_V14_MAX / span : 0);
        }
        en_param_text(p, v, t);
        if (p == P_TUNE) u_cat(t, " ct");
        am_upper(s->text, p == P_ROOT ? EN_NOTE_NAME[v % 12] : t, R4P_STR_MAX);
    }
}
static void am_build(void)                   /* the page into ui.page (as a host-less page: synth_ui.c draws the tiles) */
{
    r4p_page_t *g = &ui.page;
    uint32_t k;
    memset(g, 0, sizeof *g);
    strcpy(g->title, AV_NAME[amr.view % AV_N]);
    am_upper(g->track, en_piece_name(am.piece), R4P_STR_MAX);
    g->r = SY_AR; g->g = SY_AG; g->b = SY_AB;
    g->page_count = 0;                       /* (no sub-pages: the header's strip shows the seven pages) */
    sy_lockview = 0;
    for (k = 0; k < 4u; k++) {
        strcpy(g->slot[k].name, AV_LABEL[amr.view % AV_N][k]);
        am_value(AV_KNOB[amr.view % AV_N][k], &g->slot[k]);
    }
    ui.octave = (int8_t)AM_CUR->par[P_BOCT];  /* the footer's OCT: the bells' octave here */
    ui.dirty |= D_HEAD | D_TILES | D_FOOT;
}
static void am_set(int p, int v)             /* a value, clamped, to the engine and the store */
{
    const en_param_t *q = en_param_info(p);
    v = v < q->lo ? q->lo : v > q->hi ? q->hi : v;
    if (p == P_TUNE) am.tune = (int16_t)v;
    else if (p == P_MIDI) am.midi = (uint8_t)v;
    else AM_CUR->par[p] = (int16_t)v;
    en_set(p, v);
    am_touch();
}
static void am_set_mode(int m)               /* a mode: its scale */
{
    m = m < 0 ? 0 : m >= EN_NMODES ? EN_NMODES - 1 : m;
    AM_CUR->par[P_MODE] = (int16_t)m;
    AM_CUR->pool = EN_MODE_POOL[m];
    en_set(P_MODE, m);
    en_pool(AM_CUR->pool);
    am_touch();
}
static void am_turn(uint32_t k, int d)
{
    int p = AV_KNOB[amr.view % AV_N][k & 3u];
    sy_touch_k = (uint8_t)(k + 1u);
    sy_touch_at = plat_ms();
    if (p == AK_SCALE)
        am_set_mode((AM_CUR->pool == EN_MODE_POOL[AM_CUR->par[P_MODE]] ? AM_CUR->par[P_MODE] : (d > 0 ? -1 : EN_NMODES)) + (d > 0 ? 1 : -1));
    else {
        const en_param_t *q = en_param_info(p);
        if (q->hi - q->lo <= 11)             /* a choice: one step a detent whatever the speed */
            d = d > 0 ? 1 : -1;
        am_set(p, am_par(p) + d);
    }
    am_build();
}
static void am_view(int v)
{
    amr.view = (uint8_t)(v < 0 ? 0 : v >= AV_N ? AV_N - 1 : v);
    sy_touch_k = 0;
    am_build();
}
static void am_piece(int i)                  /* the piece: as it was left; the loops grow anew */
{
    char t[32] = "";
    am.piece = (uint8_t)((i % EN_NPIECES + EN_NPIECES) % EN_NPIECES);
    am_apply();
    en_regrow();
    am_touch();
    am_upper(t, en_piece_name(am.piece), 20);
    sp_toast(t);
    am_build();
}
static void am_toggle(int pc)                /* a key (or a note in): its note out of the scale, or in */
{
    int rel = ((pc - AM_CUR->par[P_ROOT]) % 12 + 12) % 12;
    uint32_t p = AM_CUR->pool ^ (1u << rel);
    char t[24];
    if (!rel) { sp_toast("THE ROOT STAYS"); return; }
    if (!am_pool_ok(p)) { sp_toast("THREE NOTES AT LEAST"); return; }
    AM_CUR->pool = (uint16_t)p;
    en_pool(p);
    am_touch();
    strcpy(t, (p >> rel) & 1u ? "ADDED " : "REMOVED ");
    am_upper(t + strlen(t), EN_NOTE_NAME[pc % 12], 3);
    sp_toast(t);
    am_build();
}

/* ------------------------------------------------------------ the mode --- */
static void am_init(void)                    /* at boot: the engine's state (not its buffers: Synth mode may own them) */
{
    en_init();
    am_load();
    am_apply();
    en_regrow();
}
static void am_enter(void)                   /* (app.c mode_switch, the render held) */
{
    en_enter();                              /* the arena and the plate: AMB's now */
    en_panic();
    am_apply();
    amr.grow_at = plat_ms() + AM_GROW_MS;
    amr.reset_done = 1;                      /* (a SAVE still held from before does not reset) */
    amr.sig = 0;
    am_build();
}
static void am_leave(void)                   /* (app.c mode_switch, the render held): stop, MIDI off, save */
{
    static int32_t none[2];
    amr.want_grow = (uint8_t)(en_playing || amr.grow_at);
    amr.grow_at = 0;
    en_play(0);
    en_freeze(0);
    en_panic();
    en_render(none, 0, 0);                   /* (drains them now: the STOP and notes off go out) */
    if (amr.dirty)
        am_save();
    ui.octave = ctl.octave;
}
static void am_boot_flag(int amb)            /* the boot mode's half kept here (see am_store_t) */
{
    if (am.boot != (uint8_t)amb) {
        am.boot = (uint8_t)amb;
        if (!sy_hold_store) am_save(); else amr.dirty = 1;
    }
}

/* ------------------------------------------------------------- input --- */
static void am_button(uint32_t k)            /* a press (app.c: SEL's combinations are taken before) */
{
    int v;
    for (v = 0; v < AV_N; v++)
        if (AV_BTN[v] == k) {
            am_view(v);
            return;
        }
    switch (k) {
    case B_EDIT:
        en_next();
        sp_toast(en_playing ? "NEXT CHORD ON THE BAR" : "NEXT CHORD");
        break;
    case B_PLAY:
        amr.grow_at = 0;
        if (en_playing) {
            en_play(0);
            sp_toast("RESTING");
        } else {
            en_seed(plat_ticks24());
            en_play(1);
            sp_toast("GROWING");
        }
        break;
    case B_REC:
        sp_toast(en_frozen ? "FREEZE OFF" : "FREEZE");
        en_freeze(!en_frozen);
        break;
    case B_OCTDN: case B_OCTUP: {
        char t[24] = "BELL OCTAVE ", u[8];
        am_set(P_BOCT, AM_CUR->par[P_BOCT] + (k == B_OCTUP ? 1 : -1));
        en_param_text(P_BOCT, AM_CUR->par[P_BOCT], u);
        u_cat(t, u);
        sp_toast(t);
        am_build();
        break;
    }
    case B_SAVE:
        amr.reset_done = 0;
        amr.save_at = plat_ms();
        am_save();
        sp_toast(amr.dirty ? "NOT SAVED" : "SAVED");
        break;
    default:
        break;
    }
}
static void am_keys(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (!((keys ^ ctl.keys) & bit))
            continue;
        ctl.keys ^= bit;
        if (keys & bit)
            am_toggle((int)((5u + k) % 12u));    /* key 0 is F3 */
    }
}
/* notes from MIDI in (channel 1) toggle as the keys do: queued by the audio context, taken here */
static volatile uint8_t am_nq[16], am_nqh, am_nqt;
static void am_note_in(uint8_t note)          /* audio context */
{
    uint8_t h = (uint8_t)((am_nqh + 1u) & 15u);
    if (h != am_nqt) {
        am_nq[am_nqh] = note;
        am_nqh = h;
    }
}
static int enc_get(int k);
static void am_frame(uint32_t keys, uint32_t btn, uint32_t now)
{
    int k;
    am_keys(keys);
    while (am_nqt != am_nqh) {
        am_toggle(am_nq[am_nqt] % 12);
        am_nqt = (uint8_t)((am_nqt + 1u) & 15u);
    }
    for (k = 0; k < R4P_NENC; k++) {
        int d = enc_get(k);
        if (!d)
            continue;
        if (ui.setup) { setup_enc((uint32_t)k, d); continue; }
        if (k >= 3) {
            am_turn((uint32_t)k - 3u, d);
        } else if (k == 0 && (btn >> B_HOME & 1u)) {     /* HOME held + SELECT: the tempo from any page */
            char t[24] = "TEMPO ", u[12];
            am_set(P_TEMPO, AM_CUR->par[P_TEMPO] + d);
            u_int((unsigned)AM_CUR->par[P_TEMPO], u); u_cat(t, u);
            sp_toast(t);
            am_build();
        } else if (k == 0) {
            am_view((int)amr.view + (d > 0 ? 1 : -1));
        } else if (k == 1) {                              /* ALGORITHM: the scale */
            char t[24];
            am_set_mode((AM_CUR->pool == EN_MODE_POOL[AM_CUR->par[P_MODE]] ? AM_CUR->par[P_MODE] : (d > 0 ? -1 : EN_NMODES)) + (d > 0 ? 1 : -1));
            am_upper(t, EN_MODE_NAME[AM_CUR->par[P_MODE]], 20);
            sp_toast(t);
            am_build();
        } else {
            am_piece((int)am.piece + (d > 0 ? 1 : -1));
        }
    }
    if ((btn >> B_SAVE & 1u) && !amr.reset_done && now - amr.save_at >= AM_RESET_MS) {
        char t[32] = "RECIPE: ";
        amr.reset_done = 1;                  /* held: the piece back to its recipe (not saved until the next save) */
        am_recipe(am.piece, AM_CUR);
        am_apply();
        en_regrow();
        am_touch();
        am_upper(t + strlen(t), en_piece_name(am.piece), 20);
        sp_toast(t);
        am_build();
        ctl_card_off();
    }
    if (amr.grow_at && (int32_t)(now - amr.grow_at) >= 0) {
        amr.grow_at = 0;
        if (amr.want_grow && !en_playing) {
            en_seed(plat_ticks24());
            en_play(1);
        }
    }
    if (amr.dirty && !en_playing && now - amr.edit_at > AM_SAVE_MS)   /* a flash write stops the sound: only resting */
        am_save();
    {   /* the footer: the tempo, bar.beat, growing, frozen */
        r4p_status_t st;
        uint32_t b = en_bars;
        st.bpm10 = (uint16_t)(AM_CUR->par[P_TEMPO] * 10);
        st.bar = (uint16_t)(b / 16u + 1u);
        st.beat = (uint8_t)((b / 4u) % 4u);
        st.flags = (uint8_t)((en_playing ? R4P_ST_PLAY : 0u) | (en_frozen ? R4P_ST_REC : 0u));
        if (memcmp(&st, &ui.st, sizeof st)) {
            ui.st = st;
            ui.dirty |= D_FOOT;
        }
    }
    if (now - amr.live_at >= 100u) {         /* the band: ten frames a second at most, only when it changed */
        amr.live_at = now;
        ui.dirty |= D_LIVE;
    }
    if (sy_touch_k && now - sy_touch_at >= 2000u) {
        sy_touch_k = 0;
        ui.dirty |= D_TILE0 << 2;
    }
}
static void am_lights(uint32_t *lb, uint32_t *lk, uint32_t *db, uint32_t *dk)
{
    uint32_t i, abs_pool = 0;
    (void)db;
    *lb |= 1u << AV_BTN[amr.view % AV_N];
    if (en_playing) *lb |= 1u << B_PLAY;
    else if (amr.grow_at) *lb |= ctl.blink ? 1u << B_PLAY : 0u;
    if (en_frozen) *lb |= 1u << B_REC;
    for (i = 0; i < 12u; i++)                 /* the scale, as pitch classes */
        if ((AM_CUR->pool >> i) & 1u)
            abs_pool |= 1u << ((i + (uint32_t)AM_CUR->par[P_ROOT]) % 12u);
    for (i = 0; i < NKEYS; i++) {
        uint32_t pc = (5u + i) % 12u;
        if ((abs_pool >> pc) & 1u)
            *(pc == (uint32_t)AM_CUR->par[P_ROOT] % 12u || ((ctl.keys >> i) & 1u) ? lk : dk) |= 1u << i;
    }
}

/* ------------------------------------------------------------- screen --- */
/* the header: the page left, the piece right (grey), the seven pages as a strip under them, a dot while unsaved */
static void am_draw_head(void)
{
    char t[R4P_STR_MAX + 1];
    int32_t w = PG->track[0] ? pwidth(&FONT_B, PG->track) : 0, i, x;
    cv_begin(240, 34, R4_BG);
    if (w)
        ptext(232 - w, 7, &FONT_B, PG->track, R4_LABEL);
    fit(t, PG->title, &FONT_B, 232 - w - 26);
    x = ptext(8, 7, &FONT_B, t, R4_TEXT);
    if (amr.dirty && !ui.setup)
        cv_rect(x + 5, 15, 4, 4, sy_accent(100));
    for (i = 0; i < AV_N; i++)
        cv_rect(8 + i * 33, 28, 30, 2, !ui.setup && i == amr.view ? sy_accent(100) : R4_TILE);
    cv_blit(0, 0);
}
/* the band (232 x 82): the twelve notes as a ring (C on top; the scale grey, the chord in the accent, the root framed,
 * the chord's name inside); the four bell loops (13, 17, 23, 29 beats) as rows of beats, a note a block, the playhead
 * in the accent; the three pulse rings (16, 12, 7) under them; the layers' levels (pad, bells, bass, pulse); the way to
 * the next chord as a line at the foot */
static const int8_t AM_RING[12][2] = {{0, -32}, {16, -28}, {28, -16}, {32, 0}, {28, 16}, {16, 28}, {0, 32}, {-16, 28},
                                      {-28, 16}, {-32, 0}, {-28, -16}, {-16, -28}};
static uint32_t am_band_sig(void)
{
    uint32_t h = 2166136261u, i, k;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIX(AM_CUR->pool); MIX(AM_CUR->par[P_ROOT]); MIX(en_playing | en_frozen << 1 | en_ext << 2 | en_pulse_rest << 3);
    MIX(amr.grow_at != 0); MIX(ui.setup);
    for (i = 0; i < EN_NPAD; i++) MIX(en_chord[i]);
    for (i = 0; i < EN_NLOOP; i++) {
        MIX(en_loop_pos[i]); MIX(en_loop_glow[i] > 0.4f);
        for (k = 0; k < EN_LOOP_MAX; k++) MIX(en_loop_note[i][k] >= 0);
    }
    for (i = 0; i < EN_NDRUM; i++) { MIX(en_drum_pos[i]); MIX(en_drum_pat[i]); }
    MIX((int)(en_pad_level * 16.0f)); MIX((int)(en_bass_level * 16.0f)); MIX((int)(en_drift * 64.0f));
#undef MIX
    return h;
}
static void am_draw_band(int full)
{
    static const char *const STATE[] = {"RESTING", "GROWING", "FROZEN", "EXT CLOCK", "WAKING"};
    uint32_t sig = am_band_sig(), i, k, abs_pool = 0, chord = 0;
    int32_t cx = 40, cy = 41, x, y;
    char nm[16];
    float lv[4];
    if (!full && sig == amr.sig)
        return;
    amr.sig = sig;
    for (i = 0; i < 12u; i++)
        if ((AM_CUR->pool >> i) & 1u)
            abs_pool |= 1u << ((i + (uint32_t)AM_CUR->par[P_ROOT]) % 12u);
    for (i = 0; i < EN_NPAD; i++)
        chord |= 1u << (en_chord[i] % 12u);
    cv_begin(232, 82, R4_BG);
    for (i = 0; i < 12u; i++) {               /* the ring */
        int in = (abs_pool >> i) & 1u, ch = (chord >> i) & 1u;
        x = cx + AM_RING[i][0];
        y = cy + AM_RING[i][1];
        if (i == (uint32_t)AM_CUR->par[P_ROOT] % 12u) {
            cv_rect(x - 5, y - 5, 10, 10, R4_TEXT);
            cv_rect(x - 4, y - 4, 8, 8, R4_BG);
        }
        if (ch && in) cv_rect(x - 3, y - 3, 6, 6, sy_accent(100));
        else if (in) cv_rect(x - 2, y - 2, 4, 4, SY_MID);
        else cv_rect(x - 1, y - 1, 2, 2, R4_EDGE);
    }
    {   /* the chord's name, its root first */
        int pcs[4], n = 0, j;
        pcs[n++] = en_chord_root % 12;
        for (i = 0; i < EN_NPAD; i++) {
            int pc = en_chord[i] % 12, dup = 0;
            for (j = 0; j < n; j++) dup |= pcs[j] == pc;
            if (!dup && n < 4) pcs[n++] = pc;
        }
        en_chord_name(pcs, n, nm);
        ptext(cx - pwidth(&FONT_B, nm) / 2, cy - 14, &FONT_B, nm, R4_TEXT);
        k = amr.grow_at ? 4u : en_frozen ? 2u : en_ext ? 3u : en_playing ? 1u : 0u;
        ptext(cx - pwidth(&FONT_T, STATE[k]) / 2, cy + 6, &FONT_T, STATE[k], k == 1u ? R4_LABEL : sy_accent(100));
    }
    for (i = 0; i < EN_NLOOP; i++) {          /* the bell loops */
        y = 4 + (int32_t)i * 10;
        for (k = 0; k < EN_LOOP_LEN[i]; k++) {
            int has = en_loop_note[i][k] >= 0, here = en_playing && en_loop_pos[i] == k;
            x = 86 + (int32_t)k * 5;
            if (here) cv_rect(x, y, 4, 7, en_loop_glow[i] > 0.4f && has ? sy_accent(100) : SY_BAR);
            else if (has) cv_rect(x, y + 1, 4, 5, SY_MID);
            else cv_rect(x + 1, y + 3, 2, 1, R4_EDGE);
        }
    }
    for (i = 0; i < EN_NDRUM; i++) {          /* the pulse */
        y = 48 + (int32_t)i * 9;
        for (k = 0; k < EN_DRUM_N[i]; k++) {
            int hit = (en_drum_pat[i] >> k) & 1u, here = en_playing && en_drum_pos[i] == k;
            x = 86 + (int32_t)k * 5;
            if (here) cv_rect(x, y, 4, 6, hit ? sy_accent(100) : SY_BAR);
            else if (hit) cv_rect(x, y + 1, 4, 4, en_pulse_rest ? R4_EDGE : SY_MID);
            else cv_rect(x + 1, y + 3, 2, 1, R4_EDGE);
        }
    }
    lv[0] = en_pad_level; lv[1] = 0; lv[2] = en_bass_level; lv[3] = 0;
    for (i = 0; i < EN_NLOOP; i++) if (en_loop_glow[i] > lv[1]) lv[1] = en_loop_glow[i];
    for (i = 0; i < EN_NDRUM; i++) if (en_drum_glow[i] > lv[3]) lv[3] = en_drum_glow[i];
    for (i = 0; i < 4u; i++) {                /* the layers' levels */
        int32_t hgt = (int32_t)(lv[i] * 30.0f);
        x = 176 + (int32_t)i * 13;
        hgt = hgt < 0 ? 0 : hgt > 30 ? 30 : hgt;
        cv_rect(x, 46, 8, 30, R4_TILE);
        if (hgt) cv_rect(x, 76 - hgt, 8, hgt, SY_BAR);
    }
    x = (int32_t)(en_drift * 146.0f);         /* to the next chord */
    cv_rect(86, 80, 146, 1, R4_TILE);
    if (!en_frozen && x > 0) cv_rect(86, 80, x > 146 ? 146 : x, 1, SY_MID);
    amr.bytes += 232u * 82u * 2u;
    amr.frames++;
    cv_blit(4, BAND_Y);
    if (full)
        lcd_fill(0, BAND_Y, 4, BAND_H, R4_BG), lcd_fill(236, BAND_Y, 4, BAND_H, R4_BG);
}
