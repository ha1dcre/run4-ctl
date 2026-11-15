/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, protocol 1.4: Synth mode described to the host (src/lm.c, docs/PROTOCOL.md "Synth mode's description"),
 * from the pages' own table (synth_ui.c SP[]): the voice, the macros, the morph, the six operators (each its own page),
 * LFO, effects, GLOBAL, ARP, and the sequencer's transport; 1.5: the page shown, and the host's pick of one. Part of
 * the unity build (app.c), after synth_ui.c. */

#define SL_N (3 + 8 + 96 + 8 + 8 + 10 + 8 + 2)
static const char *const SL_PAGES[14] = {"VOICE", "SYNTH", "MORPH", "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "LFO",
                                         "EFFECTS", "GLOBAL", "ARP", "SEQ"};
static const char *const SL_STATE[3] = {"VOICE", "PATTERN", "STATE"};
enum { SLX_VOICE = 150, SLX_ALG, SLX_SAVE, SLX_PLAY, SLX_TEMPO };   /* the ids outside the pages' table */
typedef struct { uint16_t id; uint8_t page, sp, sub, k, op; } sl_ref_t;    /* sp 255: one of SLX_* */
static void sl_ref(uint32_t i, sl_ref_t *r)
{
    static const uint8_t GLO_SUB[10] = {0, 0, 0, 0, 1, 1, 2, 2, 2, 2}, GLO_K[10] = {0, 1, 2, 3, 0, 1, 0, 1, 2, 3};
    memset(r, 0, sizeof *r);
    if (i < 3) { r->sp = 255; r->id = (uint16_t)(SLX_VOICE + i); return; }
    i -= 3;
    if (i < 8) { r->sp = SP_HOME; r->sub = (uint8_t)(i / 4); r->k = (uint8_t)(i % 4); r->page = (uint8_t)(1 + i / 4); r->id = (uint16_t)i; return; }
    i -= 8;
    if (i < 96) {                                    /* OPn: OPERATOR's eight, then ENVELOPE's eight */
        uint32_t j = i % 16u;
        r->op = (uint8_t)(i / 16u);
        r->sp = j < 8u ? SP_EDIT : SP_ENV;
        r->sub = (uint8_t)((j % 8u) / 4u);
        r->k = (uint8_t)(j % 4u);
        r->page = (uint8_t)(3u + r->op);
        r->id = (uint16_t)(16u + i);
        return;
    }
    i -= 96;
    if (i < 16) { r->sp = i < 8 ? SP_LFO : SP_FX; r->sub = (uint8_t)((i % 8) / 4); r->k = (uint8_t)(i % 4); r->page = (uint8_t)(i < 8 ? 9 : 10); r->id = (uint16_t)(112 + i); return; }
    i -= 16;
    if (i < 10) { r->sp = SP_GLO; r->sub = GLO_SUB[i]; r->k = GLO_K[i]; r->page = 11; r->id = (uint16_t)(128 + r->sub * 4 + r->k); return; }
    i -= 10;
    if (i < 8) { r->sp = SP_ARP; r->sub = (uint8_t)(i / 4); r->k = (uint8_t)(i % 4); r->page = 12; r->id = (uint16_t)(140 + i); return; }
    i -= 8;
    r->sp = 255;
    r->page = 13;
    r->id = (uint16_t)(SLX_PLAY + (i ? 1 : 0));
}
static const sp_param_t *sl_q(const sl_ref_t *r) { return &SP[r->sp].p[r->sub][r->k]; }
static uint8_t sl_off(const sl_ref_t *r) { return SP[r->sp].off[r->sub][r->k]; }
static int sl_morph_set(const sp_param_t *q) { return q->src == S_MSETA || q->src == S_MSETB; }
static void sl_param(uint32_t i, lm_param_t *p)
{
    sl_ref_t r;
    sl_ref(i, &r);
    memset(p, 0, sizeof *p);
    p->id = r.id;
    p->page = r.page;
    p->step = 1;
    p->unit = "";
    if (r.sp == 255) {
        switch (r.id) {
        case SLX_VOICE: p->name = "VOICE"; p->lo = 1; p->hi = SYN_PATCHES; p->def = 1; break;
        case SLX_ALG: p->name = "ALGORITHM"; p->lo = 1; p->hi = 32; p->def = 1; break;
        case SLX_SAVE: p->name = "SAVE VOICE"; p->kind = R4P_LK_ACTION; p->hi = 1; break;
        case SLX_PLAY: p->name = "PLAY"; p->kind = R4P_LK_TOGGLE; p->hi = 1; p->nlabels = 2; p->flags = R4P_PF_AUTO; break;
        default: p->name = "TEMPO"; p->lo = 400; p->hi = 2400; p->def = 1200; p->unit = "BPM/10"; break;
        }
        return;
    }
    {
        const sp_param_t *q = sl_q(&r);
        p->name = q->name;
        p->lo = q->lo;
        p->hi = q->hi;
        p->def = q->lo > 0 ? q->lo : q->hi < 0 ? q->hi : 0;
        if (sl_morph_set(q)) {
            p->kind = R4P_LK_ACTION;
            p->lo = 0; p->hi = 1; p->def = 0;
        } else if (q->kind == R4P_KIND_ENUM) {
            p->kind = R4P_LK_ENUM;
            p->nlabels = (uint8_t)(q->hi - q->lo + 1);
        } else if (q->kind == R4P_KIND_TOGGLE) {
            p->kind = R4P_LK_TOGGLE;
            p->nlabels = 2;
        } else {
            p->kind = R4P_LK_NUMBER;
            p->flags = q->kind == R4P_KIND_BI ? R4P_PF_BIPOLAR : 0;
            p->unit = q->fmt == F_PCT ? "%" : q->fmt == F_SEMI || q->fmt == F_BEND ? "st" : q->fmt == F_CENTS ? "ct" : "";
            if (q->fmt == F_DET) p->def = 7;
        }
        if (q->src == S_MPOS)
            p->flags |= R4P_PF_AUTO;                 /* (a drift moves it) */
    }
}
static void sl_label(uint32_t i, uint32_t k, char *t)
{
    sl_ref_t r;
    sl_ref(i, &r);
    if (r.sp == 255) {
        strcpy(t, k ? "PLAYING" : "STOPPED");
        return;
    }
    {
        const sp_param_t *q = sl_q(&r);
        if (q->kind == R4P_KIND_TOGGLE)
            strcpy(t, k ? "ON" : "OFF");
        else
            strcpy(t, q->names && k < q->nnames ? q->names[k] : "?");
    }
}
static int sl_get(uint32_t i)
{
    sl_ref_t r;
    int v;
    uint8_t op = sui.op;
    sl_ref(i, &r);
    if (r.sp == 255) {
        switch (r.id) {
        case SLX_VOICE: return syn.patch_idx + 1;
        case SLX_ALG: return (syn.cur[FP_ALG] & 31) + 1;
        case SLX_PLAY: return sq.run != 0;
        case SLX_TEMPO: return syn.bpm10;
        default: return 0;
        }
    }
    if (sl_morph_set(sl_q(&r)))
        return 0;
    sui.op = r.op;                                   /* (the operator pages read the operator they are for) */
    v = sp_get(sl_q(&r), sl_off(&r));
    sui.op = op;
    return v;
}
static void sl_voice_label(uint32_t idx, char *t)    /* "F1 E.PIANO 1", "U5 MY VOICE" */
{
    char nm[12], u[12];
    uint32_t k;
    t[0] = 0;
    u_cat(t, idx < SYN_NFACTORY ? "F" : "U");
    u_int(idx < SYN_NFACTORY ? idx + 1u : idx - SYN_NFACTORY + 1u, u);
    u_cat(t, u);
    if (!syn_patch_used(idx)) { u_cat(t, " EMPTY"); return; }
    syn_patch_name(idx, nm);
    for (k = 10; k > 0 && (nm[k - 1] == ' ' || nm[k - 1] == 0); k--)
        nm[k - 1] = 0;
    u_cat(t, " ");
    u_cat(t, nm);
}
static void sl_text(uint32_t i, int v, char *t)
{
    sl_ref_t r;
    sl_ref(i, &r);
    t[0] = 0;
    if (r.sp == 255) {
        switch (r.id) {
        case SLX_VOICE: sl_voice_label((uint32_t)(v - 1), t); break;
        case SLX_ALG: u_int((unsigned)v, t); break;
        case SLX_PLAY: sl_label(i, (uint32_t)v, t); break;
        case SLX_TEMPO: { char u[12]; u_int((unsigned)v / 10u, u); u_cat(t, u); u_cat(t, "."); u_int((unsigned)v % 10u, u); u_cat(t, u); break; }
        default: break;
        }
        return;
    }
    if (!sl_morph_set(sl_q(&r)))
        sp_text(sl_q(&r), v, t);
}
static int sl_set(uint32_t i, int v)
{
    sl_ref_t r;
    sl_ref(i, &r);
    if (r.sp == 255) {
        switch (r.id) {
        case SLX_VOICE:
            if (!syn_patch_used((uint32_t)(v - 1)))
                return R4P_LE_REFUSED;               /* an empty user slot */
            if ((uint32_t)(v - 1) != syn.patch_idx) {
                char t[24];
                syn_event(3, 0, 0, 0);
                syn_load_patch((uint32_t)(v - 1));
                smu.valid = 0;
                syn_patch_label(t);
                sp_toast(t);
                sui.dirty_set = 1;
            }
            break;
        case SLX_ALG: syn.cur[FP_ALG] = (uint8_t)(v - 1); sp_touch(); break;
        case SLX_SAVE: sp_save_patch(); lm_forget(0); break;   /* (the voice's name: told again) */
        case SLX_PLAY:
            if (sq_ext())
                return R4P_LE_CLOCK;                 /* a running MIDI clock has the transport */
            if ((v != 0) != (sq.run != 0)) {
                sq_ed.follow = 1;
                syn_event(8, (uint8_t)v, 0, 0);
            }
            break;
        default: syn.bpm10 = (uint16_t)v; sp_touch(); break;
        }
    } else {
        const sp_param_t *q = sl_q(&r);
        uint8_t op = sui.op;
        sui.op = r.op;
        if (sl_morph_set(q)) {
            syn_morph_take(&syn, q->src == S_MSETB);
            sp_toast(q->src == S_MSETB ? "B SET" : "A SET");
        } else {
            sp_set(q, sl_off(&r), v);
            if (q->src == S_MPOS && syn.mo_mode >= MO_DRIFT)
                syn_mo.rephase = 1;                  /* (a drift goes on from there) */
            if (q->src == S_ARPON && !syn.arp_on)
                syn_event(3, 0, 0, 0);
        }
        sui.op = op;
        sp_touch();
    }
    sui.edit_at = plat_ms();
    sp_build();
    return R4P_LE_OK;
}
static void sl_state(r4p_lstate_t *s)
{
    char t[24], u[12];
    s->flags = (uint8_t)((sq.run ? R4P_LS_PLAYING : 0) | (sq.rec ? R4P_LS_RECORDING : 0) | (sq_ext() ? R4P_LS_EXTCLOCK : 0));
    s->n = 3;
    sl_voice_label(syn.patch_idx, t);
    lm_str(s->text[0], t, R4P_LM_STR);
    strcpy(t, "PATTERN "); u_int(syn.seq_pat + 1u, u); u_cat(t, u);
    strcpy(s->text[1], t);
    strcpy(s->text[2], sq_ext() ? "EXT CLOCK" : sq.run ? "PLAYING" : "STOPPED");
}
/* 1.5: the description page of a device page (SL_PAGES' order: HOME's two sub-pages are SYNTH and MORPH, EDIT and ENV
 * one page per operator); VOICE, page 0, is never the device's */
static uint32_t sl_page_of(uint32_t sp, uint32_t sub, uint32_t op)
{
    static const uint8_t SLP[SP_N] = {1, 3, 3, 9, 10, 11, 12, 13};
    return sp == SP_HOME ? 1u + sub : sp == SP_EDIT || sp == SP_ENV ? 3u + op : SLP[sp % SP_N];
}
static void sl_view(uint8_t *page, uint16_t ids[R4P_LVIEW_MAX], uint8_t *n)
{
    uint32_t k, i;
    sl_ref_t r;
    *page = (uint8_t)sl_page_of(sui.page, sui.sub, sui.op);
    *n = 0;
    if (sui.page == SP_SEQ)
        return;                                      /* (its knobs edit steps: not described) */
    for (k = 0; k < 4u; k++) {                       /* each knob: the parameter sl_ref puts there, or none (EMPTY) */
        ids[k] = R4P_LVIEW_NONE;
        for (i = 3; i < SL_N; i++) {
            sl_ref(i, &r);
            if (r.sp == sui.page && r.sub == sui.sub && r.k == k && (r.op == sui.op || !sp_op_page())) {
                ids[k] = r.id;
                break;
            }
        }
    }
    *n = 4;
}
/* LVIEW_SET: the page as its button gets there (an operator page keeps OPERATOR / ENVELOPE and its sub-page, the
 * others keep their sub-page when shown already); 0 = not now (SETUP open, a step held: the player's) */
static int sl_show(uint32_t page)
{
    static const uint8_t SLS[5] = {SP_LFO, SP_FX, SP_GLO, SP_ARP, SP_SEQ};
    uint32_t sp, sub, op = sui.op;
    if (page == 1u || page == 2u) {
        sp = SP_HOME;
        sub = page - 1u;
        if (sub >= SP[SP_HOME].nsub)
            return 1;
    } else if (page >= 3u && page <= 8u) {
        op = page - 3u;
        sp = sp_op_page() ? sui.page : SP_EDIT;
        sub = sp_op_page() ? sui.sub : 0u;
    } else if (page >= 9u && page <= 13u) {
        sp = SLS[page - 9u];
        sub = sui.page == sp ? sui.sub : 0u;
    } else {
        return 1;                                    /* (VOICE: no page of the device's) */
    }
    if (sp == sui.page && sub == sui.sub && op == sui.op)
        return 1;                                    /* (shown already) */
    if (ui.setup || (sui.page == SP_SEQ && sq_ed.held >= 0))
        return 0;
    if (ml.arm) {                                    /* (learning: a page button leaves it) */
        ml.arm = 0;
        sp_toast("LEARN OFF");
    }
    ctl_card_off();
    sui.op = (uint8_t)op;
    if (sp != sui.page || sub != sui.sub)
        sp_page_show(sp, sub);
    else
        sp_select_op(op);
    return 1;
}
static const lm_desc_t SL_DESC = {MODE_SYNTH, "SYNTH", 14, SL_PAGES, SL_N, 3, SL_STATE,
                                  sl_param, sl_label, sl_get, sl_text, sl_set, sl_state, sl_view, sl_show};
