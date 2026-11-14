/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: MIDI learn for control changes. 16 bindings (kept in the SETUP object, app.c setup_t), each a
 * CC on a channel to one of the Synth mode's tiles (a page, its sub-page, a knob, and for EDIT / ENV the operator):
 * learned by holding SEL and turning the tile's knob, then moving the controller (within 10 s); SAVE while armed
 * unlearns that tile. Beside them a fixed default map (ML_DEF, on unless SETUP > MIDI LEARN turns it off). Every one
 * picks up (synth.c syn_ml_pickup): it moves its value only after reaching it. The rules (which CCs, the pick-up)
 * follow Mutable Dreams' midi_learn.inc (Kid Bunka, MIT OR GPL-3.0-only); the code is new. The CCs come from the MIDI
 * in (the audio context) through a small ring and are applied in the main loop, as an edit on the panel is. The host
 * voice has none of it: RUN4 sets it over HV_PARAM. Part of app.c (after the SETUP page). */

static char *u_end(char *t) { while (*t) t++; return t; }
#define ML_T(page, sub, k) ((uint8_t)((page) << 4 | (sub) << 2 | (k)))
/* the default CCs: General MIDI's and the usual synth numbers where there is one */
static const struct { uint8_t cc, tgt; } ML_DEF[] = {
    {1, ML_T(SP_LFO, 0, 2)},   {7, ML_T(SP_HOME, 0, 3)},  {12, ML_T(SP_FX, 0, 0)},   {16, ML_T(SP_HOME, 1, 0)},
    {71, ML_T(SP_HOME, 0, 1)}, {73, ML_T(SP_HOME, 0, 2)}, {74, ML_T(SP_HOME, 0, 0)}, {76, ML_T(SP_LFO, 0, 0)},
    {78, ML_T(SP_LFO, 0, 1)},  {91, ML_T(SP_FX, 0, 3)},   {92, ML_T(SP_LFO, 0, 3)},  {93, ML_T(SP_FX, 0, 1)},
    {94, ML_T(SP_FX, 0, 2)}};
#define ML_NDEF (sizeof ML_DEF / sizeof ML_DEF[0])
#define ML_ARM_MS 10000u
typedef struct { int16_t last, setv; uint8_t picked; } ml_st_t;   /* a binding's pick-up */
static struct {
    uint8_t arm, arm_op;                     /* learning: the tile + 1 (0 = not), its operator */
    uint32_t arm_at;
    ml_st_t st[ML_N + ML_NDEF];              /* the bindings', then the defaults' */
    uint8_t slot, clear_arm;                 /* SETUP > MIDI LEARN: the binding shown; CLEAR armed */
    uint32_t applied;                        /* CCs that moved a value (the simulator's check) */
} ml;
#define ML_Q 32
static uint8_t mlq[ML_Q][3];                 /* channel, CC, value: the MIDI in's (audio context), for the main loop */
static volatile uint8_t mlq_w, mlq_r;

static void ml_reset_pickup(void)
{
    uint32_t i;
    for (i = 0; i < ML_N + ML_NDEF; i++)
        ml.st[i] = (ml_st_t){ML_NONE, ML_NONE, 0};
}
static void ml_init(void)
{
    memset(&ml, 0, sizeof ml);
    ml_reset_pickup();
    mlq_w = mlq_r = 0;
}
static void ml_cc_in(uint32_t ch, uint32_t cc, uint32_t v)   /* audio context: a CC for the main loop (full: dropped) */
{
    uint8_t w = mlq_w;
    if ((uint8_t)(w - mlq_r) >= ML_Q)
        return;
    mlq[w % ML_Q][0] = (uint8_t)ch;
    mlq[w % ML_Q][1] = (uint8_t)cc;
    mlq[w % ML_Q][2] = (uint8_t)v;
    mlq_w = (uint8_t)(w + 1u);
}
static const sp_param_t *ml_param(uint32_t tgt, uint32_t *off)   /* a tile that can be learned, or 0 */
{
    uint32_t page = tgt >> 4, sub = (tgt >> 2) & 3u, k = tgt & 3u;
    const sp_param_t *q;
    if (page >= SP_N || page == SP_SEQ || sub >= SP[page].nsub)
        return 0;
    q = &SP[page].p[sub][k];
    if (q->kind == R4P_KIND_EMPTY || q->src == S_MSETA || q->src == S_MSETB)
        return 0;
    *off = SP[page].off[sub][k];
    return q;
}
static void ml_name(uint32_t tgt, uint32_t op, char *t)        /* "BRIGHT", "RATIO OP2" */
{
    uint32_t off;
    const sp_param_t *q = ml_param(tgt, &off);
    char u[4] = "OP1";
    strcpy(t, q ? q->name : "?");
    if (q && q->src == S_OPB) { u[2] = (char)('1' + op % 6u); u_cat(t, " "); u_cat(t, u); }
}
/* a CC's value 0..127 onto tile tgt (operator op), picked up; st: its pick-up */
static void ml_apply(uint32_t tgt, uint32_t op, uint32_t val, ml_st_t *st)
{
    uint32_t off, keep = sui.op;
    const sp_param_t *q = ml_param(tgt, &off);
    int32_t v, cur, tol;
    if (!q)
        return;
    sui.op = (uint8_t)(op % 6u);                        /* (an operator's parameter: that operator's) */
    v = q->lo + ((int32_t)val * (q->hi - q->lo) + 63) / 127;
    cur = sp_get(q, off);
    tol = (q->hi - q->lo) / 32;
    if (st->setv != cur)
        st->picked = 0;                                 /* moved by something else meanwhile: picked up again */
    if (!st->picked && syn_ml_pickup(v, cur, st->last, tol < 1 ? 1 : tol))
        st->picked = 1;
    st->last = (int16_t)v;
    if (st->picked && v != cur) {
        if (q->src == S_MPOS && syn.mo_mode >= MO_DRIFT)
            syn_mo.rephase = 1;
        sp_set(q, off, v);
        if (q->src == S_ARPON && !syn.arp_on)
            syn_event(3, 0, 0, 0);
        sp_touch();
        sui.edit_at = plat_ms();
        ml.applied++;
    }
    st->setv = (int16_t)sp_get(q, off);
    sui.op = (uint8_t)keep;
    if (!ui.setup && sui.page == tgt >> 4 && sui.sub == ((tgt >> 2) & 3u) && (q->src != S_OPB || sui.op == op % 6u))
        sp_build();
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += setup.ml[i].cc != 0;
    return n;
}
static void ml_unlearn(uint32_t tgt, uint32_t op)              /* every binding of that tile */
{
    uint32_t i;
    for (i = 0; i < ML_N; i++)
        if (setup.ml[i].cc && setup.ml[i].tgt == tgt && (setup.ml[i].opch & 7u) == op)
            memset(&setup.ml[i], 0, sizeof setup.ml[i]);
}
static void ml_learn(uint32_t ch, uint32_t cc)                 /* the armed tile gets this CC */
{
    uint32_t tgt = ml.arm - 1u, op = ml.arm_op, i, f = ML_N;
    char t[32], u[12];
    if (!syn_ml_safe(cc)) {                             /* (kept armed: another controller may follow) */
        strcpy(t, "CC "); u_int(cc, u); u_cat(t, u); u_cat(t, " NOT LEARNABLE");
        sp_toast(t);
        return;
    }
    ml_unlearn(tgt, op);
    for (i = 0; i < ML_N; i++)
        if (setup.ml[i].cc == cc && (setup.ml[i].opch >> 3) == ch + 1u)
            memset(&setup.ml[i], 0, sizeof setup.ml[i]);   /* (the newest binding of a CC wins) */
    for (i = 0; i < ML_N && f == ML_N; i++)
        if (!setup.ml[i].cc)
            f = i;
    ml.arm = 0;
    if (f == ML_N) {
        sp_toast("16 LEARNED: CLEAR ONE");
        return;
    }
    setup.ml[f] = (ml_bind_t){(uint8_t)cc, (uint8_t)tgt, (uint8_t)(op | (ch + 1u) << 3)};
    ml.st[f] = (ml_st_t){ML_NONE, ML_NONE, 0};
    ctl.setup_dirty = 1;
    ctl.setup_owed = 1;
    strcpy(t, "CC "); u_int(cc, u); u_cat(t, u); u_cat(t, " > ");
    ml_name(tgt, op, u_end(t));
    sp_toast(t);
}
/* the main loop, Synth mode: the CCs that came in */
static void ml_frame(uint32_t now)
{
    if (ml.arm && now - ml.arm_at > ML_ARM_MS) {
        ml.arm = 0;
        sp_toast("LEARN OFF");
    }
    while (mlq_r != mlq_w) {
        const uint8_t *e = mlq[mlq_r % ML_Q];
        uint32_t ch = e[0], cc = e[1], v = e[2], i, hit = 0;
        mlq_r = (uint8_t)(mlq_r + 1u);
        if (ml.arm) {
            ml_learn(ch, cc);
            continue;
        }
        for (i = 0; i < ML_N; i++)
            if (setup.ml[i].cc == cc && (setup.ml[i].opch >> 3) == ch + 1u) {
                ml_apply(setup.ml[i].tgt, setup.ml[i].opch & 7u, v, &ml.st[i]);
                hit = 1;
            }
        if (!hit && setup.ml_def)
            for (i = 0; i < ML_NDEF; i++)
                if (ML_DEF[i].cc == cc)
                    ml_apply(ML_DEF[i].tgt, 0, v, &ml.st[ML_N + i]);
    }
}
/* SEL + K1..K4 on a Synth mode page: learn that tile */
static void ml_arm_tile(uint32_t k)
{
    uint32_t tgt = (uint32_t)ML_T(sui.page, sui.sub, k), off;
    char t[32] = "LEARN ";
    if (!ml_param(tgt, &off)) {
        sp_toast("NOT LEARNABLE");
        return;
    }
    ml.arm = (uint8_t)(tgt + 1u);
    ml.arm_op = sui.op;
    ml.arm_at = plat_ms();
    ml_name(tgt, sui.op, u_end(t));
    u_cat(t, ": MOVE A CC");
    sp_toast(t);
}
static void ml_unlearn_armed(void)            /* SAVE while learning: the tile's CC goes */
{
    char t[32];
    ml_unlearn(ml.arm - 1u, ml.arm_op);
    ml_name(ml.arm - 1u, ml.arm_op, t);
    u_cat(t, ": NO CC");
    ml.arm = 0;
    ctl.setup_dirty = 1;
    ctl.setup_owed = 1;
    sp_toast(t);
}
