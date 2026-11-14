/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, protocol 1.4: AMB mode described to the host (src/lm.c, docs/PROTOCOL.md "AMB's description"). Page 0
 * PLAY: the piece, the notes of the scale, the transport and the actions; pages 1..7 the device's pages, their four
 * knobs each, ids = the engine's parameter numbers (amb.h P_*). Part of the unity build (app.c), after amb_ui.c. */

enum { AL_PIECE = 32, AL_NOTES, AL_GROW, AL_FREEZE, AL_NEXT, AL_SAVE, AL_RESET };
#define AL_NPLAY 7                                  /* page 0's parameters, then 7 pages x 4 knobs */
#define AL_N (AL_NPLAY + AV_N * 4)
static const char *const AL_PAGES[1 + AV_N] = {"PLAY", "GARDEN", "PAD", "BELLS", "BASS", "PULSE", "SPACE", "KEY"};
static const char *const AL_STATE[4] = {"PIECE", "CHORD", "SCALE", "STATE"};
static const char *const AL_PLAYN[AL_NPLAY] = {"PIECE", "NOTES", "GROW", "FREEZE", "NEXT CHORD", "SAVE", "RESET PIECE"};

static int al_engine(uint32_t i) { return i < AL_NPLAY ? -1 : AV_KNOB[(i - AL_NPLAY) / 4][(i - AL_NPLAY) % 4]; }
static uint32_t al_abs_pool(void)                   /* the scale as pitch classes (bit 0 = C) */
{
    uint32_t i, a = 0;
    for (i = 0; i < 12u; i++)
        if ((AM_CUR->pool >> i) & 1u)
            a |= 1u << ((i + (uint32_t)AM_CUR->par[P_ROOT]) % 12u);
    return a;
}
static void al_param(uint32_t i, lm_param_t *p)
{
    int e = al_engine(i);
    memset(p, 0, sizeof *p);
    p->step = 1;
    p->unit = "";
    if (e < 0) {
        static const uint8_t K[AL_NPLAY] = {R4P_LK_ENUM, R4P_LK_MASK, R4P_LK_TOGGLE, R4P_LK_TOGGLE, R4P_LK_ACTION,
                                            R4P_LK_ACTION, R4P_LK_ACTION};
        p->id = (uint16_t)(AL_PIECE + i);
        p->page = 0;
        p->kind = K[i];
        p->name = AL_PLAYN[i];
        p->hi = 1;
        if (i == 0) { p->hi = EN_NPIECES - 1; p->nlabels = EN_NPIECES; }
        if (i == 1) { p->hi = 4095; p->nlabels = 12; p->def = 0xAB5; }   /* Garden's D dorian */
        if (i == 2 || i == 3) p->nlabels = 2;
        if (i == 2) { p->flags = R4P_PF_AUTO; p->def = 1; }
        return;
    }
    {
        const en_param_t *q = en_param_info(e == AK_SCALE ? P_MODE : e);
        p->id = (uint16_t)(e == AK_SCALE ? P_MODE : e);
        p->page = (uint8_t)(1u + (i - AL_NPLAY) / 4u);
        p->name = AV_LABEL[(i - AL_NPLAY) / 4][(i - AL_NPLAY) % 4];
        p->lo = q->lo;
        p->hi = q->hi;
        p->def = q->def;
        if (e == AK_SCALE) {                         /* the modes, and CUSTOM: notes taken out or put in */
            p->kind = R4P_LK_ENUM;
            p->hi = EN_NMODES;
            p->nlabels = EN_NMODES + 1;
        } else if (e == P_ETIME || e == P_PADTONE || e == P_MIDI || e == P_ROOT) {
            p->kind = R4P_LK_ENUM;
            p->nlabels = (uint8_t)(q->hi - q->lo + 1);
        } else {
            p->kind = R4P_LK_NUMBER;
            p->flags = q->lo < 0 ? R4P_PF_BIPOLAR : 0;
            p->unit = e == P_TEMPO ? "BPM" : e == P_TUNE ? "ct" : e == P_BOCT || e == P_BASSOCT ? "oct" : "";
        }
    }
}
static void al_label(uint32_t i, uint32_t k, char *t)
{
    int e = al_engine(i);
    char b[16];
    if (e < 0) {
        if (i == 0) am_upper(t, en_piece_name((int)k), R4P_LM_STR);
        else if (i == 1) strcpy(t, EN_NOTE_NAME[k % 12u]);       /* ("Eb": as the engine names them) */
        else if (i == 2) strcpy(t, k ? "GROWING" : "RESTING");
        else strcpy(t, k ? "ON" : "OFF");
        return;
    }
    if (e == AK_SCALE) {
        am_upper(t, k < EN_NMODES ? EN_MODE_NAME[k] : "Custom", R4P_LM_STR);
        return;
    }
    en_param_text(e, en_param_info(e)->lo + (int)k, b);
    if (e == P_ROOT) strcpy(t, b); else am_upper(t, b, R4P_LM_STR);
}
static int al_get(uint32_t i)
{
    int e = al_engine(i);
    if (e == AK_SCALE)
        return AM_CUR->pool == EN_MODE_POOL[AM_CUR->par[P_MODE]] ? AM_CUR->par[P_MODE] : EN_NMODES;
    if (e >= 0)
        return am_par(e);
    switch (i) {
    case 0: return am.piece;
    case 1: return (int)al_abs_pool();
    case 2: return en_playing;
    case 3: return en_frozen;
    default: return 0;                               /* (the actions) */
    }
}
static void al_text(uint32_t i, int v, char *t)
{
    int e = al_engine(i);
    char b[24];
    uint32_t k, n = 0;
    t[0] = 0;
    if (e >= 0) {
        if (e == AK_SCALE || e == P_ETIME || e == P_PADTONE || e == P_MIDI || e == P_ROOT) {
            al_label(i, (uint32_t)(v - (e == AK_SCALE ? 0 : en_param_info(e)->lo)), t);
            return;
        }
        en_param_text(e, v, b);
        if (e == P_TUNE) u_cat(b, " ct");
        am_upper(t, b, R4P_LM_STR);
        return;
    }
    switch (i) {
    case 0: case 3: al_label(i, (uint32_t)v, t); break;
    case 1:
        for (k = 0; k < 12u; k++) n += ((uint32_t)v >> k) & 1u;
        u_int(n, b); u_cat(b, " NOTES");
        strcpy(t, b);
        break;
    case 2: strcpy(t, amr.grow_at && !v ? "WAKING" : v ? "GROWING" : "RESTING"); break;
    default: break;
    }
}
static int al_set(uint32_t i, int v)
{
    int e = al_engine(i);
    if (e == AK_SCALE) {
        if (v < EN_NMODES)                           /* (CUSTOM: the notes stay as they are) */
            am_set_mode(v);
        am_build();
        return R4P_LE_OK;
    }
    if (e >= 0) {
        am_set(e, v);
        am_build();
        return R4P_LE_OK;
    }
    switch (i) {
    case 0:
        am_piece(v);
        break;
    case 1: {                                        /* the notes (pitch classes): relative to the root inside */
        uint32_t rel = 0, k, root = (uint32_t)AM_CUR->par[P_ROOT];
        for (k = 0; k < 12u; k++)
            if (((uint32_t)v >> ((k + root) % 12u)) & 1u)
                rel |= 1u << k;
        if (!am_pool_ok(rel))
            return R4P_LE_REFUSED;                   /* the root out, or fewer than three notes */
        AM_CUR->pool = (uint16_t)rel;
        en_pool(rel);
        am_touch();
        am_build();
        break;
    }
    case 2:                                          /* GROW / REST: a running clock's START / STOP has the transport */
        if (am_clk_run && en_ext)
            return R4P_LE_CLOCK;
        amr.grow_at = 0;
        if (v && !en_playing) {
            en_seed(plat_ticks24());
            en_play(1);
            sp_toast("GROWING");
        } else if (!v && en_playing) {
            en_play(0);
            sp_toast("RESTING");
        }
        break;
    case 3:
        if ((v != 0) != (en_frozen != 0)) {
            sp_toast(v ? "FREEZE" : "FREEZE OFF");
            en_freeze(v);
        }
        break;
    case 4:
        en_next();
        sp_toast(en_playing ? "NEXT CHORD ON THE BAR" : "NEXT CHORD");
        break;
    case 5:
        am_save();
        sp_toast(amr.dirty ? "NOT SAVED" : "SAVED");
        break;
    case 6:
        am_reset_piece();
        break;
    default:
        break;
    }
    return R4P_LE_OK;
}
static void al_state(r4p_lstate_t *s)
{
    char b[24];
    int m = AM_CUR->par[P_MODE];
    s->flags = (uint8_t)((en_playing ? R4P_LS_PLAYING : 0) | (en_frozen ? R4P_LS_FROZEN : 0) | (en_ext ? R4P_LS_EXTCLOCK : 0) |
                         (amr.grow_at && !en_playing ? R4P_LS_WAKING : 0));
    s->n = 4;
    am_upper(s->text[0], en_piece_name(am.piece), R4P_LM_STR);
    b[0] = 0;
    am_chord_name(b);
    lm_str(s->text[1], b, R4P_LM_STR);
    am_upper(b, AM_CUR->pool == EN_MODE_POOL[m] ? EN_MODE_NAME[m] : "Custom", 12);
    strcpy(s->text[2], EN_NOTE_NAME[AM_CUR->par[P_ROOT] % 12]);
    u_cat(s->text[2], " ");
    u_cat(s->text[2], b);
    strcpy(s->text[3], amr.grow_at && !en_playing ? "WAKING" : en_frozen ? "FROZEN" : en_ext && en_playing ? "EXT CLOCK"
                       : en_playing ? "GROWING" : "RESTING");
}
static const lm_desc_t AL_DESC = {MODE_AMB, "AMB", 1 + AV_N, AL_PAGES, AL_N, 4, AL_STATE,
                                  al_param, al_label, al_get, al_text, al_set, al_state};
