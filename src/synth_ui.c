/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the pages. Every page is an r4p_page_t (title, accent colour, four tiles) drawn by ui.c exactly as a
 * page the host sent, so Synth mode looks like RUN4 mode. Part of the unity build, after synth.c and ui.c. */

enum { SP_HOME, SP_EDIT, SP_ENV, SP_LFO, SP_FX, SP_GLO, SP_ARP, SP_SEQ, SP_N };
enum { F_NUM, F_SIGNED, F_PCT, F_NAMES, F_ONOFF, F_ALG, F_NOTE, F_CHAN, F_SEMI, F_CENTS, F_STEP, F_BPM10, F_BEND, F_DET };

typedef struct {
    const char *name;
    uint8_t kind;                            /* R4P_KIND_* */
    uint8_t fmt;                             /* F_* */
    int16_t lo, hi;
    uint8_t sgn;                             /* the storage is an int8_t */
    uint8_t src;                             /* which storage: see sp_ref */
    const char *const *names;                /* F_NAMES */
    uint8_t nnames;
} sp_param_t;

/* where a parameter lives. SRC_CUR: a byte of the patch (offset in `off`, plus the operator's block); SRC_SYN: a field of syn_set_t */
enum { S_NONE, S_OPB, S_PAT, S_BRIGHT, S_ENVSPD, S_LEVEL, S_DRIVE, S_CHORUS, S_DLYMIX, S_DLYDIV, S_DLYFB, S_REVMIX, S_REVTYPE,
       S_REVSIZE, S_TRANSPOSE, S_TUNE, S_CHAN, S_BEND, S_VELSENS, S_ARPON, S_ARPMODE, S_ARPRATE, S_ARPOCT, S_ARPGATE,
       S_CURSOR, S_STEPNOTE, S_STEPON, S_BPM, S_SEQLEN, S_SEQGATE, S_SEQVEL };

static const char *const N_WAVE[] = {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S&H"};
static const char *const N_ARP[] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "PLAYED"};
static const char *const N_RATE[] = {"1/8", "1/16", "1/16T"};
static const char *const N_DIV[] = {"1/8", "DOT 1/8", "1/4", "DOT 1/4", "1/2", "DOT 1/2"};
static const char *const N_REV[] = {"ROOM", "SPRING", "HALL"};
static const char *const N_MODE[] = {"RATIO", "FIXED"};
static const char *const N_OFF[] = {"OFF", "ON"};

#define P(nm, kd, fm, lo, hi, sg, sr) {nm, kd, fm, lo, hi, sg, sr, 0, 0}
#define PN(nm, fm, lo, hi, sr, arr) {nm, R4P_KIND_ENUM, fm, lo, hi, 0, sr, arr, (uint8_t)(sizeof arr / sizeof arr[0])}
#define EMPTYP {"", R4P_KIND_EMPTY, 0, 0, 0, 0, S_NONE, 0, 0}

typedef struct { const char *title; uint8_t nsub; const sp_param_t p[3][4]; uint8_t off[3][4]; } sp_page_t;
/* `off` = the byte inside the operator's 21-byte block (S_OPB) or inside the patch (S_PAT) */
static const sp_page_t SP[SP_N] = {
    {"SYNTH", 1, {{P("BRIGHT", R4P_KIND_BI, F_SIGNED, -64, 63, 1, S_BRIGHT), P("FEEDBACK", R4P_KIND_UNI, F_NUM, 0, 7, 0, S_PAT),
                   P("ENV SPEED", R4P_KIND_BI, F_SIGNED, -64, 63, 1, S_ENVSPD), P("LEVEL", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_LEVEL)}},
     {{0, FP_FB, 0, 0}, {0}, {0}}},
    {"OPERATOR", 2, {{P("RATIO", R4P_KIND_UNI, F_NUM, 0, 31, 0, S_OPB), P("FINE", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB),
                      P("LEVEL", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB), P("DETUNE", R4P_KIND_BI, F_DET, 0, 14, 0, S_OPB)},
                     {PN("MODE", F_NAMES, 0, 1, S_OPB, N_MODE), P("VEL SENS", R4P_KIND_UNI, F_NUM, 0, 7, 0, S_OPB),
                      P("RATE SCALE", R4P_KIND_UNI, F_NUM, 0, 7, 0, S_OPB), P("AMP MOD", R4P_KIND_UNI, F_NUM, 0, 3, 0, S_OPB)}},
     {{FP_FC, FP_FF, FP_OL, FP_DET}, {FP_MODE, FP_KVS, FP_RS, FP_AMS}, {0}}},
    {"ENVELOPE", 2, {{P("RATE 1", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB), P("RATE 2", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB),
                      P("RATE 3", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB), P("RATE 4", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB)},
                     {P("LEVEL 1", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB), P("LEVEL 2", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB),
                      P("LEVEL 3", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB), P("LEVEL 4", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_OPB)}},
     {{FP_R1, FP_R1 + 1, FP_R1 + 2, FP_R1 + 3}, {FP_L1, FP_L1 + 1, FP_L1 + 2, FP_L1 + 3}, {0}}},
    {"LFO", 2, {{P("SPEED", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_PAT), P("DELAY", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_PAT),
                 P("PITCH MOD", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_PAT), P("AMP MOD", R4P_KIND_UNI, F_NUM, 0, 99, 0, S_PAT)},
                {PN("WAVE", F_NAMES, 0, 5, S_PAT, N_WAVE), P("PITCH SENS", R4P_KIND_UNI, F_NUM, 0, 7, 0, S_PAT),
                 P("LFO SYNC", R4P_KIND_TOGGLE, F_ONOFF, 0, 1, 0, S_PAT), P("OSC SYNC", R4P_KIND_TOGGLE, F_ONOFF, 0, 1, 0, S_PAT)}},
     {{FP_LFS, FP_LFD, FP_LPMD, FP_LAMD}, {FP_LFW, FP_LPMS, FP_LKS, FP_OKS}, {0}}},
    {"EFFECTS", 2, {{P("DRIVE", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_DRIVE), P("CHORUS", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_CHORUS),
                     P("DELAY", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_DLYMIX), P("REVERB", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_REVMIX)},
                    {PN("DELAY TIME", F_NAMES, 0, 5, S_DLYDIV, N_DIV), P("DELAY FDBK", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_DLYFB),
                     PN("REVERB TYPE", F_NAMES, 0, 2, S_REVTYPE, N_REV), P("REVERB SIZE", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_REVSIZE)}},
     {{0}, {0}, {0}}},
    {"GLOBAL", 2, {{P("TRANSPOSE", R4P_KIND_BI, F_SEMI, -24, 24, 1, S_TRANSPOSE), P("TUNE", R4P_KIND_BI, F_CENTS, -50, 50, 1, S_TUNE),
                    P("MIDI CH", R4P_KIND_UNI, F_CHAN, 0, 16, 0, S_CHAN), P("BEND RANGE", R4P_KIND_UNI, F_BEND, 0, 12, 0, S_BEND)},
                   {P("VELOCITY", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_VELSENS), EMPTYP, EMPTYP, EMPTYP}},
     {{0}, {0}, {0}}},
    {"ARP", 2, {{PN("ARP", F_NAMES, 0, 1, S_ARPON, N_OFF), PN("MODE", F_NAMES, 0, 4, S_ARPMODE, N_ARP),
                 PN("RATE", F_NAMES, 0, 2, S_ARPRATE, N_RATE), P("OCTAVES", R4P_KIND_UNI, F_NUM, 1, 3, 0, S_ARPOCT)},
                {P("GATE", R4P_KIND_UNI, F_PCT, 10, 100, 0, S_ARPGATE), EMPTYP, EMPTYP, EMPTYP}},
     {{0}, {0}, {0}}},
    {"SEQUENCER", 2, {{P("STEP", R4P_KIND_UNI, F_STEP, 0, 15, 0, S_CURSOR), P("NOTE", R4P_KIND_UNI, F_NOTE, 0, 127, 0, S_STEPNOTE),
                       PN("STEP", F_NAMES, 0, 1, S_STEPON, N_OFF), P("TEMPO", R4P_KIND_UNI, F_BPM10, 400, 2400, 0, S_BPM)},
                      {P("LENGTH", R4P_KIND_UNI, F_NUM, 1, 16, 0, S_SEQLEN), P("GATE", R4P_KIND_UNI, F_PCT, 10, 100, 0, S_SEQGATE),
                       P("VELOCITY", R4P_KIND_UNI, F_NUM, 1, 127, 0, S_SEQVEL), EMPTYP}},
     {{0}, {0}, {0}}},
};

static struct {
    uint8_t page, sub, op;                   /* the page shown, its sub-page, the operator the EDIT / ENV pages edit */
    uint8_t dirty_set;                       /* the settings changed since saved */
    uint32_t edit_at;                        /* when (plat_ms) */
} sui;

static const uint8_t SP_BLACK6[6] = {1, 3, 5, 8, 10, 13};     /* the lowest six black keys pick OP1 .. OP6 on EDIT / ENV */
static int sp_op_page(void) { return sui.page == SP_EDIT || sui.page == SP_ENV; }

/* a parameter's storage as an int */
static uint8_t *sp_ptr(const sp_param_t *q, uint32_t off, int *is_signed)
{
    *is_signed = q->sgn;
    switch (q->src) {
    case S_OPB: return syn.cur + (5u - sui.op) * FP_OP + off;     /* OP1 is the last block of the patch */
    case S_PAT: return syn.cur + off;
    case S_BRIGHT: return (uint8_t *)&syn.bright;
    case S_ENVSPD: return (uint8_t *)&syn.env_spd;
    case S_LEVEL: return &syn.level;
    case S_DRIVE: return &syn.drive;
    case S_CHORUS: return &syn.chorus;
    case S_DLYMIX: return &syn.dly_mix;
    case S_DLYDIV: return &syn.dly_div;
    case S_DLYFB: return &syn.dly_fb;
    case S_REVMIX: return &syn.rev_mix;
    case S_REVTYPE: return &syn.rev_type;
    case S_REVSIZE: return &syn.rev_size;
    case S_TRANSPOSE: return (uint8_t *)&syn.transpose;
    case S_TUNE: return (uint8_t *)&syn.tune;
    case S_CHAN: return &syn.chan;
    case S_BEND: return &syn.bend;
    case S_VELSENS: return &syn.vel_sens;
    case S_ARPON: return &syn.arp_on;
    case S_ARPMODE: return &syn.arp_mode;
    case S_ARPRATE: return &syn.arp_rate;
    case S_ARPOCT: return &syn.arp_oct;
    case S_ARPGATE: return &syn.arp_gate;
    case S_CURSOR: return &syn_sq.cursor;
    case S_STEPNOTE: return &syn.step[syn_sq.cursor & 15u].note;
    case S_STEPON: return &syn.step[syn_sq.cursor & 15u].on;
    case S_SEQLEN: return &syn.seq_len;
    case S_SEQGATE: return &syn.seq_gate;
    case S_SEQVEL: return &syn.seq_vel;
    default: return 0;
    }
}
static int sp_get(const sp_param_t *q, uint32_t off)
{
    int sg;
    if (q->src == S_BPM)
        return syn.bpm10;
    {
        uint8_t *p = sp_ptr(q, off, &sg);
        return !p ? 0 : sg ? (int)(int8_t)*p : (int)*p;
    }
}
static void sp_set(const sp_param_t *q, uint32_t off, int v)
{
    int sg;
    uint8_t *p;
    v = v < q->lo ? q->lo : v > q->hi ? q->hi : v;
    if (q->src == S_BPM) {
        syn.bpm10 = (uint16_t)v;
        return;
    }
    p = sp_ptr(q, off, &sg);
    if (p)
        *p = (uint8_t)v;
}

static const char *const NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void sp_text(const sp_param_t *q, int v, char *t)
{
    char u[12];
    t[0] = 0;
    switch (q->fmt) {
    case F_NAMES: u_cat(t, v >= 0 && v < q->nnames ? q->names[v] : "?"); break;
    case F_ONOFF: u_cat(t, v ? "ON" : "OFF"); break;
    case F_PCT: u_int((unsigned)v, u); u_cat(t, u); u_cat(t, "%"); break;
    case F_SIGNED: case F_SEMI: case F_CENTS: case F_DET: {
        int s = q->fmt == F_DET ? v - 7 : v;
        if (s > 0) u_cat(t, "+");
        if (s < 0) u_cat(t, "-");
        u_int((unsigned)(s < 0 ? -s : s), u); u_cat(t, u);
        if (q->fmt == F_SEMI) u_cat(t, " st");
        if (q->fmt == F_CENTS) u_cat(t, " ct");
        break;
    }
    case F_NOTE: u_cat(t, NOTE_N[v % 12]); u_int((unsigned)(v / 12) - 1u, u); u_cat(t, u); break;
    case F_CHAN:
        if (!v) u_cat(t, "OMNI");
        else { u_cat(t, "CH "); u_int((unsigned)v, u); u_cat(t, u); }
        break;
    case F_STEP: u_int((unsigned)v + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int(syn.seq_len, u); u_cat(t, u); break;
    case F_BPM10: u_int((unsigned)v / 10u, u); u_cat(t, u); u_cat(t, "."); u_int((unsigned)v % 10u, u); u_cat(t, u); break;
    case F_BEND: u_int((unsigned)v, u); u_cat(t, u); u_cat(t, " st"); break;
    default: u_int((unsigned)v, u); u_cat(t, u); break;
    }
}
static void sp_value(const sp_param_t *q, int v, r4p_slot_t *s)
{
    int span = q->hi - q->lo;
    s->kind = q->kind;
    switch (q->kind) {
    case R4P_KIND_ENUM: s->value = (uint16_t)(((q->hi - q->lo + 1) << 7) | (v - q->lo)); break;
    case R4P_KIND_TOGGLE: s->value = (uint16_t)(v != 0); break;
    case R4P_KIND_BI:
        if (q->lo < 0)                                    /* 0 in the middle, each side its own scale */
            s->value = (uint16_t)(v < 0 ? 8192 - v * 8192 / q->lo : 8192 + v * 8191 / q->hi);
        else
            s->value = (uint16_t)(span ? v * R4P_V14_MAX / span : 0);
        break;
    default: s->value = (uint16_t)(span ? (v - q->lo) * R4P_V14_MAX / span : 0); break;
    }
    sp_text(q, v, s->text);
}

static void syn_patch_label(char *t)          /* "F3" / "U05" and the name */
{
    char nm[11], u[12];
    uint32_t i = syn.patch_idx;
    t[0] = 0;
    if (i < SYN_NFACTORY) { u_cat(t, "F"); u_int(i + 1u, u); u_cat(t, u); }
    else { u_cat(t, "U"); u_int(i - SYN_NFACTORY + 1u, u); u_cat(t, u); }
    u_cat(t, " ");
    memcpy(nm, syn.cur + FP_NAME, 10);
    nm[10] = 0;
    for (i = 10; i > 0 && (nm[i - 1] == ' ' || nm[i - 1] == 0); i--)
        nm[i - 1] = 0;
    u_cat(t, nm);
}

/* the current page into ui.page (a host-less page) */
static void sp_build(void)
{
    r4p_page_t *g = &ui.page;
    const sp_page_t *pg = &SP[sui.page];
    uint32_t k;
    char u[12];
    memset(g, 0, sizeof *g);
    if (sp_op_page()) {
        strcpy(g->title, pg->title);
        u_cat(g->title, " ");
        u_int(sui.op + 1u, u);
        u_cat(g->title, u);
        strcpy(g->track, "ALG ");
        u_int((unsigned)(syn.cur[FP_ALG] & 31u) + 1u, u);
        u_cat(g->track, u);
    } else {
        strcpy(g->title, pg->title);
        syn_patch_label(g->track);
        if (sui.page == SP_SEQ && syn_sq.seq_rec)
            strcpy(g->track, "REC");
    }
    g->r = 127; g->g = 70; g->b = 20;                  /* the Synth accent: orange (RUN4's own colour) */
    if (sui.page == SP_FX) { g->r = 30; g->g = 105; g->b = 100; }
    if (sui.page == SP_SEQ || sui.page == SP_ARP) { g->r = 70; g->g = 100; g->b = 127; }
    g->page_count = pg->nsub;
    g->page_idx = sui.sub;
    for (k = 0; k < 4u; k++) {
        const sp_param_t *q = &pg->p[sui.sub][k];
        r4p_slot_t *s = &g->slot[k];
        strcpy(s->name, q->name);
        if (q->kind == R4P_KIND_EMPTY)
            continue;
        sp_value(q, sp_get(q, pg->off[sui.sub][k]), s);
    }
    ui.dirty |= D_HEAD | D_TILES | D_FOOT;
}
static void sp_touch(void)
{
    sui.dirty_set = 1;
    syn_dirty = 1;
}
static void sp_turn(uint32_t k, int d)
{
    const sp_page_t *pg = &SP[sui.page];
    const sp_param_t *q = &pg->p[sui.sub][k];
    if (q->kind == R4P_KIND_EMPTY)
        return;
    if (q->kind == R4P_KIND_TOGGLE || (q->kind == R4P_KIND_ENUM && q->hi == 1))
        d = d > 0 ? 1 : -1;
    if (q->src == S_BPM)
        d *= 10;
    sp_set(q, pg->off[sui.sub][k], sp_get(q, pg->off[sui.sub][k]) + d);
    if (q->src == S_CURSOR && syn.seq_len && syn_sq.cursor >= syn.seq_len)
        syn_sq.cursor = (uint8_t)(syn.seq_len - 1u);
    if (q->src == S_ARPON && !syn.arp_on) {
        syn_event(3, 0, 0, 0);
    }
    sp_touch();
    sui.edit_at = plat_ms();
    sp_build();
}
static void sp_page_press(uint32_t page)
{
    if (page == sui.page)
        sui.sub = (uint8_t)((sui.sub + 1u) % SP[page].nsub);
    else {
        sui.page = (uint8_t)page;
        sui.sub = 0;
    }
    sp_build();
}
static void sp_select_op(uint32_t op)
{
    sui.op = (uint8_t)(op > 5u ? 5u : op);
    sp_build();
}

/* ------------------------------------------------- patches and storage --- */
static void sp_toast(const char *a)
{
    ui_toast(a, 12);
}
static void syn_save_all(void)                /* settings and the changed banks into the store */
{
    uint32_t b;
    plat_store_save(OBJ_SYNSET, &syn, sizeof syn);
    for (b = 0; b < 2u; b++)
        if ((syn_bank_dirty >> b) & 1u)
            plat_store_save(OBJ_BANK_A + b, &syn_bank[b], sizeof syn_bank[b]);
    syn_bank_dirty = 0;
    sui.dirty_set = 0;
}
static void syn_load_all(void)
{
    uint32_t b;
    syn_set_t t;
    for (b = 0; b < 2u; b++) {
        syn_bank_t bk;
        if (plat_store_load(OBJ_BANK_A + b, &bk, sizeof bk) == (int)sizeof bk && bk.magic == SYN_BANK_MAGIC)
            syn_bank[b] = bk;
    }
    if (plat_store_load(OBJ_SYNSET, &t, sizeof t) == (int)sizeof t && t.ver == SYN_SET_VER && t.patch_idx < SYN_PATCHES &&
        t.seq_len >= 1 && t.seq_len <= 16 && t.arp_mode < ARP_N && t.rev_type < REV_N) {
        syn = t;
        fp_sanitize(syn.cur);
        syn_dirty = 1;
    }
}
static void sp_patch_step(int d)              /* PRESETS: the next patch that has a voice */
{
    int i = syn.patch_idx, n = 0;
    char t[24];
    while (n++ < SYN_PATCHES) {
        i = (i + (d > 0 ? 1 : -1) + SYN_PATCHES) % SYN_PATCHES;
        if (syn_patch_used((uint32_t)i))
            break;
    }
    syn_event(3, 0, 0, 0);
    syn_load_patch((uint32_t)i);
    syn_patch_label(t);
    sp_toast(t);
    sp_build();
    sui.dirty_set = 1;
    sui.edit_at = plat_ms();
}
static void sp_save_patch(void)               /* SAVE: into this user slot, or the first free one */
{
    uint32_t i = syn.patch_idx;
    char t[24] = "SAVED ", u[12];
    if (i < SYN_NFACTORY || i >= SYN_PATCHES) {
        for (i = SYN_NFACTORY; i < SYN_PATCHES; i++)
            if (!syn_patch_used(i))
                break;
        if (i >= SYN_PATCHES)
            i = SYN_NFACTORY;                 /* all full: the first user slot */
    }
    syn_store_patch(i);
    syn_save_all();
    u_cat(t, "U"); u_int(i - SYN_NFACTORY + 1u, u); u_cat(t, u);
    sp_toast(t);
    sp_build();
}
