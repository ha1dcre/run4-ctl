/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the pages. Every page is an r4p_page_t (title, accent colour, four tiles) drawn by ui.c exactly as a
 * page the host sent, so Synth mode looks like RUN4 mode. Part of the unity build, after synth.c and ui.c. */

enum { SP_HOME, SP_EDIT, SP_ENV, SP_LFO, SP_FX, SP_GLO, SP_ARP, SP_SEQ, SP_N };
enum { F_NUM, F_SIGNED, F_PCT, F_NAMES, F_ONOFF, F_ALG, F_NOTE, F_CHAN, F_SEMI, F_CENTS, F_BPM10, F_BEND, F_DET };

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
       S_MUTAMT, S_SCALE, S_ROOT, S_KEYMODE, S_MIDISCALE, S_CHORD, S_INV, S_SPREAD };

static const char *const N_WAVE[] = {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S&H"};
static const char *const N_ARP[] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "PLAYED"};
static const char *const N_RATE[] = {"1/8", "1/16", "1/16T"};
static const char *const N_DIV[] = {"1/8", "DOT 1/8", "1/4", "DOT 1/4", "1/2", "DOT 1/2"};
static const char *const N_REV[] = {"ROOM", "SPRING", "HALL"};
static const char *const N_MODE[] = {"RATIO", "FIXED"};
static const char *const N_OFF[] = {"OFF", "ON"};
static const char *const N_MUT[] = {"SMALL", "MEDIUM", "WILD"};
static const char *const N_SCALE[] = {"OFF", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD",
                                      "HARM MIN", "MAJ PENT", "MIN PENT", "BLUES"};      /* (OFF: chromatic) */
static const char *const N_ROOT[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_KEYMODE[] = {"SNAP", "IN KEY"};
static const char *const N_MIDISC[] = {"AS IS", "SCALE"};
static const char *const N_CHORD[] = {"OFF", "MAJ", "MIN", "7", "MAJ7", "MIN7", "SUS2", "SUS4", "POWER", "SCALE 3", "SCALE 4"};
static const char *const N_INV[] = {"ROOT", "1ST", "2ND", "3RD"};
static const char *const N_SPREAD[] = {"CLOSE", "OPEN", "WIDE"};

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
    {"GLOBAL", 3, {{P("TRANSPOSE", R4P_KIND_BI, F_SEMI, -24, 24, 1, S_TRANSPOSE), P("TUNE", R4P_KIND_BI, F_CENTS, -50, 50, 1, S_TUNE),
                    P("MIDI CH", R4P_KIND_UNI, F_CHAN, 0, 16, 0, S_CHAN), P("BEND RANGE", R4P_KIND_UNI, F_BEND, 0, 12, 0, S_BEND)},
                   {P("VELOCITY", R4P_KIND_UNI, F_PCT, 0, 100, 0, S_VELSENS), PN("MUTATE", F_NAMES, 0, 2, S_MUTAMT, N_MUT),
                    EMPTYP, EMPTYP},
                   {PN("SCALE", F_NAMES, 0, 10, S_SCALE, N_SCALE), PN("ROOT", F_NAMES, 0, 11, S_ROOT, N_ROOT),
                    PN("KEY MODE", F_NAMES, 0, 1, S_KEYMODE, N_KEYMODE), PN("MIDI IN", F_NAMES, 0, 1, S_MIDISCALE, N_MIDISC)}},
     {{0}, {0}, {0}}},
    {"ARP", 2, {{PN("ARP", F_NAMES, 0, 1, S_ARPON, N_OFF), PN("MODE", F_NAMES, 0, 4, S_ARPMODE, N_ARP),
                 PN("RATE", F_NAMES, 0, 2, S_ARPRATE, N_RATE), P("OCTAVES", R4P_KIND_UNI, F_NUM, 1, 3, 0, S_ARPOCT)},
                {P("GATE", R4P_KIND_UNI, F_PCT, 10, 100, 0, S_ARPGATE), PN("CHORD", F_NAMES, 0, 10, S_CHORD, N_CHORD),
                 PN("INVERSION", F_NAMES, 0, 3, S_INV, N_INV), PN("SPREAD", F_NAMES, 0, 2, S_SPREAD, N_SPREAD)}},
     {{0}, {0}, {0}}},
    {"SEQUENCER", 4, {{EMPTYP, EMPTYP, EMPTYP, EMPTYP}}, {{0}, {0}, {0}}},     /* its own pages: sq_build (below) */
};

#define SY_AR 127                            /* Synth mode's one accent, on the wire scale (x2 on screen): warm amber */
#define SY_AG 76
#define SY_AB 32
static uint8_t sy_touch_k;                   /* the knob turned last + 1: its tile in the accent for 2 s (the look, below) */
static uint32_t sy_touch_at;
static uint8_t sy_lockview;                  /* the tiles show a held step's locks: sy_lockhot = the locked ones (accent) */
static uint8_t sy_lockhot;
static int32_t sy_base[4] = {-1, -1, -1, -1};  /* a locked tile's base value (value14, a tick on its bar), -1 = none */
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
    case S_MUTAMT: return &syn.mut_amt;
    case S_SCALE: return &syn.scale;
    case S_ROOT: return &syn.root;
    case S_KEYMODE: return &syn.keymode;
    case S_MIDISCALE: return &syn.midi_scale;
    case S_CHORD: return &syn.chord;
    case S_INV: return &syn.inv;
    case S_SPREAD: return &syn.spread;
    default: return 0;
    }
}
static int sp_get(const sp_param_t *q, uint32_t off)
{
    int sg;
    uint8_t *p = sp_ptr(q, off, &sg);
    return !p ? 0 : sg ? (int)(int8_t)*p : (int)*p;
}
static void sp_set(const sp_param_t *q, uint32_t off, int v)
{
    int sg;
    uint8_t *p;
    v = v < q->lo ? q->lo : v > q->hi ? q->hi : v;
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

static void sq_build(void);
static void sq_turn(uint32_t k, int d);
/* the current page into ui.page (a host-less page) */
static void sp_build(void)
{
    r4p_page_t *g = &ui.page;
    const sp_page_t *pg = &SP[sui.page];
    uint32_t k;
    char u[12];
    if (sui.page == SP_SEQ) {
        sq_build();
        return;
    }
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
    }
    g->r = SY_AR; g->g = SY_AG; g->b = SY_AB;          /* one accent for all of Synth mode (the footer's dots, toasts) */
    sy_lockview = 0;
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
    sy_touch_k = (uint8_t)(k + 1u);
    sy_touch_at = plat_ms();
    if (sui.page == SP_SEQ) {
        sq_turn(k, d);
        return;
    }
    if (q->kind == R4P_KIND_EMPTY)
        return;
    if (q->kind == R4P_KIND_TOGGLE || (q->kind == R4P_KIND_ENUM && q->hi == 1))
        d = d > 0 ? 1 : -1;
    sp_set(q, pg->off[sui.sub][k], sp_get(q, pg->off[sui.sub][k]) + d);
    if (q->src == S_ARPON && !syn.arp_on) {
        syn_event(3, 0, 0, 0);
    }
    sp_touch();
    sui.edit_at = plat_ms();
    sp_build();
}
static void sq_disarm(void);
static void sp_page_press(uint32_t page)
{
    sq_disarm();
    sy_touch_k = 0;
    if (page == sui.page)
        sui.sub = (uint8_t)((sui.sub + 1u) % SP[page].nsub);
    else {
        sui.page = (uint8_t)page;
        sui.sub = 0;
        sq_ed.lview = 0;                      /* (a held step's lock page: the step's own again) */
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
static void sq_save(void)                     /* the changed pattern objects (2088 bytes each) */
{
    static sq_obj_t ob;
    uint32_t o;
    for (o = 0; o < SQ_OBJS; o++)
        if ((sq_dirty >> o) & 1u) {
            sq_obj_fill(o, &ob);
            if (plat_store_save(OBJ_SEQ_A + o, &ob, sizeof ob) == 0)
                sq_dirty = (uint8_t)(sq_dirty & ~(1u << o));
        }
}
static void syn_save_all(void)                /* settings, the changed banks and patterns into the store */
{
    uint32_t b;
    plat_store_save(OBJ_SYNSET, &syn, sizeof syn);
    for (b = 0; b < 2u; b++)
        if ((syn_bank_dirty >> b) & 1u)
            plat_store_save(OBJ_BANK_A + b, &syn_bank[b], sizeof syn_bank[b]);
    syn_bank_dirty = 0;
    sui.dirty_set = 0;
    sq_save();
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
    for (b = 0; b < SQ_OBJS; b++) {
        static sq_obj_t ob;
        int n = plat_store_load(OBJ_SEQ_A + b, &ob, sizeof ob);
        if (n > 0)
            sq_obj_take(b, &ob, (uint32_t)n);        /* (a version 1 object: the patterns, no locks, no chain) */
    }
    sq_dirty = 0;
    {
        int n = plat_store_load(OBJ_SYNSET, &t, sizeof t);
        if (n == (int)SYN_SET_V2_SIZE && t.ver == 2u) {   /* stored before the keys and the song: their defaults */
            syn_set_fix(&t, 1);
            n = (int)sizeof t;
        }
        if (n != (int)sizeof t)
            t.ver = 0;
    }
    if (t.ver == SYN_SET_VER && t.patch_idx < SYN_PATCHES &&
        t.seq_pat < SQ_PATS && t.seq_clock < SQC_N && t.seq_trn >= -24 && t.seq_trn <= 24 && t.arp_mode < ARP_N &&
        t.rev_type < REV_N) {
        syn = t;
        syn_set_fix(&syn, 0);
        fp_sanitize(syn.cur);
        syn_dirty = 1;
    }
}
/* MUTATE (SEL + EDIT): a variation of the sounding voice by GLO's MUTATE amount (synth.c syn_mutate); SEL + SAVE swaps
 * back to the voice before it and again to the variation (one level). Nothing is stored into a voice slot until SAVE;
 * the sounding voice is kept like any edit. */
static struct { uint8_t bak[FP_SIZE + 1], mut[FP_SIZE + 1]; uint8_t valid, undone; uint32_t rng; } smu = {{0}, {0}, 0, 0, 0x9E3779B9u};
static void sp_mutate(void)
{
    static const char *const AMT[3] = {"SMALL", "MEDIUM", "WILD"};
    char t[32], u[4];
    uint32_t chg, k, tries = 0;
    memcpy(smu.bak, syn.cur, sizeof smu.bak);
    smu.rng ^= plat_ticks24();
    do
        chg = syn_mutate(syn.cur, syn.mut_amt, &smu.rng);
    while (!chg && ++tries < 8u);
    memcpy(smu.mut, syn.cur, sizeof smu.mut);
    smu.valid = 1;
    smu.undone = 0;
    strcpy(t, "MUTATE");
    if (chg & 0x40u) u_cat(t, " ALG");
    if (chg & 0x3Fu) {
        u_cat(t, " OP");
        for (k = 0; k < 6u; k++)
            if ((chg >> k) & 1u) { u[0] = (char)('1' + k); u[1] = 0; u_cat(t, u); }
    }
    if (chg & 0x80u) u_cat(t, " LFO");
    if (strlen(t) > 24u) strcpy(t, AMT[syn.mut_amt % 3u]), u_cat(t, " MUTATE");
    sp_touch();
    sui.edit_at = plat_ms();
    sp_toast(t);
    sp_build();
}
static void sp_mutate_undo(void)
{
    if (!smu.valid) {
        sp_toast("NOTHING TO UNDO");
        return;
    }
    smu.undone ^= 1u;
    memcpy(syn.cur, smu.undone ? smu.bak : smu.mut, sizeof smu.bak);
    sp_touch();
    sui.edit_at = plat_ms();
    sp_toast(smu.undone ? "UNDO MUTATE" : "REDO MUTATE");
    sp_build();
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
    smu.valid = 0;                            /* (another voice: nothing of the last mutate to undo) */
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

/* ----------------------------------------------------------- the SEQ page --- */
/* Three views: a step held on the keys (STEP n: its NOTE, VELOCITY, GATE, CHANCE), step recording (REC armed and stopped:
 * STEP, NOTE, LENGTH, TEMPO), and the pattern's three sub-pages. The screen: a grid of the step page on the keys (16
 * cells, the notes as ticks by pitch, the playing step and the cursor marked), the four step pages below it, a line
 * of the pattern's settings, and the four knobs' tiles in one row (they sit in a row on the panel too). */
enum { SQV_PAGE, SQV_STEP, SQV_REC };
static struct {
    uint8_t copy_to;                         /* COPY TO armed: pattern + 1 (SAVE copies), 0 = not */
    uint8_t clear;                           /* CLEAR armed (SAVE clears) */
    uint32_t arm_at;
    uint8_t drawn_step, drawn_pat, drawn_run, drawn_view;   /* what the grid shows: redrawn when the play moves */
    uint8_t drawn_ci, drawn_rep;             /* ... and the song's entry */
    uint8_t chain_cur;                       /* the SONG sub-page's entry (0..len; len = a new one) */
    uint8_t del_arm;                         /* SONG: DELETE armed (SAVE deletes the entry) */
} sqi;
/* a held step's lock pages: the page button pressed while the step is held picks one (synth.c LK_*; 255 = none) */
static const struct { uint8_t page, sub, id[4]; const char *name; } LKV[4] = {
    {SP_HOME, 0, {LK_BRIGHT, LK_FB, LK_ENVSPD, LK_LEVEL}, "SYNTH"},
    {SP_LFO, 0, {LK_LFOSPD, LK_LFODLY, LK_PMD, LK_AMD}, "LFO"},
    {SP_FX, 0, {LK_DRIVE, LK_CHORUS, LK_DLYMIX, LK_REVMIX}, "FX"},
    {SP_FX, 1, {LK_DLYTIME, LK_DLYFB, 255, LK_REVSIZE}, "FX 2"}};
static const char *const N_SQRATE[SQR_N] = {"1/8", "1/16", "1/16T", "1/32"};
static const char *const N_CLOCK[SQC_N] = {"AUTO", "OUT", "INT"};

static int sq_view(void)
{
    if (sq_ed.held >= 0)
        return SQV_STEP;
    if (sq.rec && !sq.run)
        return SQV_REC;
    return SQV_PAGE;
}
static void sq_disarm(void)
{
    sqi.copy_to = 0;
    sqi.clear = 0;
    sqi.del_arm = 0;
}
static void sq_note_text(const sq_step_t *s, char *t)     /* "C4", "C4 +2" (a chord of three), "TIE", "--" */
{
    char u[12];
    t[0] = 0;
    if (sq_is_tie(s)) { u_cat(t, "TIE"); return; }
    if (!sq_n(s)) { u_cat(t, "--"); return; }
    u_cat(t, NOTE_N[s->note[0] % 12u]);
    u_int((unsigned)(s->note[0] / 12u) - 1u, u); u_cat(t, u);
    if (sq_n(s) > 1u) { u_cat(t, " +"); u_int(sq_n(s) - 1u, u); u_cat(t, u); }
}
static void sq_tile(uint32_t k, const char *name, uint8_t kind, uint16_t value, const char *text)
{
    r4p_slot_t *s = &ui.page.slot[k];
    strcpy(s->name, name);
    s->kind = kind;
    s->value = value;
    strcpy(s->text, text);
}
static uint16_t sq_uni(int v, int lo, int hi) { return (uint16_t)(hi > lo ? (v - lo) * R4P_V14_MAX / (hi - lo) : 0); }
static uint16_t sq_enum(int i, int n) { return (uint16_t)(n << 7 | i); }
static void sq_num(char *t, int v, const char *suffix)
{
    char u[12];
    t[0] = 0;
    if (v < 0) { u_cat(t, "-"); v = -v; }
    u_int((unsigned)v, u); u_cat(t, u);
    u_cat(t, suffix);
}
static void sq_pat_name(char *t, uint32_t p)              /* "P01" */
{
    t[0] = 'P'; t[1] = (char)('0' + (p + 1u) / 10u); t[2] = (char)('0' + (p + 1u) % 10u); t[3] = 0;
}
static void sq_step_tiles(const sq_step_t *s, const sq_pat_t *pt, int note_first)
{
    char t[16];
    (void)note_first;
    sq_note_text(s, t);
    sq_tile(0, "NOTE", R4P_KIND_UNI, sq_uni(sq_n(s) ? s->note[0] : 0, 0, 127), t);
    if (s->vel) sq_num(t, s->vel, ""); else strcpy(t, "PAT");
    sq_tile(1, "VELOCITY", R4P_KIND_UNI, sq_uni(s->vel ? s->vel : pt->vel, 0, 127), t);
    if (s->gate) sq_num(t, s->gate, "%"); else strcpy(t, "PAT");
    sq_tile(2, "GATE", R4P_KIND_UNI, sq_uni(s->gate ? s->gate : pt->gate, 0, 100), t);
    sq_num(t, sq_step_get(s, 3), "%");
    sq_tile(3, "CHANCE", R4P_KIND_UNI, sq_uni(sq_step_get(s, 3), 0, 100), t);
}
/* the held step's locks on lock page sq_ed.lview: each tile the locked value (in the accent, its base value a tick on
 * the bar) or the base value */
static void sq_lock_tiles(uint32_t pat)
{
    uint32_t k, lv = (sq_ed.lview - 1u) % 4u;
    sy_lockview = 1;
    sy_lockhot = 0;
    for (k = 0; k < 4u; k++) {
        const sp_param_t *q = &SP[LKV[lv].page].p[LKV[lv].sub][k];
        r4p_slot_t *sl = &ui.page.slot[k], b;
        uint32_t id = LKV[lv].id[k];
        int32_t base, lk;
        sy_base[k] = -1;
        if (id == 255u) {
            sq_tile(k, "", R4P_KIND_EMPTY, 0, "");
            continue;
        }
        base = sp_get(q, LKV[lv].page == SP_HOME || LKV[lv].page == SP_LFO ? SP[LKV[lv].page].off[LKV[lv].sub][k] : 0);
        lk = sq_lock_get(pat, (uint32_t)sq_ed.held, id);
        strcpy(sl->name, q->name);
        sp_value(q, lk != -1000 ? lk : base, sl);
        if (lk != -1000) {
            sp_value(q, base, &b);
            sy_base[k] = b.value;
            sy_lockhot |= (uint8_t)(1u << k);
        }
    }
}
/* K1..K4 on a lock page: the parameter locked on every held step; back at the base value the lock goes */
static void sq_lock_turn(uint32_t k, int d)
{
    uint32_t lv = (sq_ed.lview - 1u) % 4u, id = LKV[lv].id[k], sel = sq_sel_pat(), i, off;
    const sp_param_t *q = &SP[LKV[lv].page].p[LKV[lv].sub][k];
    int32_t base, cur, nv;
    int full = 0;
    if (id == 255u)
        return;
    off = LKV[lv].page == SP_HOME || LKV[lv].page == SP_LFO ? SP[LKV[lv].page].off[LKV[lv].sub][k] : 0u;
    base = sp_get(q, off);
    cur = sq_lock_get(sel, (uint32_t)sq_ed.held, id);
    cur = cur != -1000 ? cur : base;
    if (q->kind == R4P_KIND_ENUM)
        d = d > 0 ? 1 : -1;
    nv = cur + d;
    nv = nv < q->lo ? q->lo : nv > q->hi ? q->hi : nv;
    for (i = 0; i < SQ_STEPS; i++)
        if (sq_ed_is_held(i)) {
            if (nv == base)
                sq_lock_del(sel, i, id);
            else if (sq_lock_set(sel, i, id, nv) < 0)
                full = 1;
        }
    if (full)
        sp_toast("LOCK LANES FULL");
    sq_ed.held_used = 1;
}
/* the ALGORITHM encoder while a step is held: its algorithm locked (back at the voice's own: unlocked) */
static void sq_lock_alg(int d)
{
    uint32_t sel = sq_sel_pat(), i;
    int32_t base = syn.cur[FP_ALG] & 31, cur = sq_lock_get(sel, (uint32_t)sq_ed.held, LK_ALG), nv;
    char t[24] = "LOCK ALG ", u[8];
    int full = 0;
    cur = cur != -1000 ? cur : base;
    nv = cur + d;
    nv = nv < 0 ? 0 : nv > 31 ? 31 : nv;
    for (i = 0; i < SQ_STEPS; i++)
        if (sq_ed_is_held(i)) {
            if (nv == base) sq_lock_del(sel, i, LK_ALG);
            else if (sq_lock_set(sel, i, LK_ALG, nv) < 0) full = 1;
        }
    sq_ed.held_used = 1;
    if (full) strcpy(t, "LOCK LANES FULL");
    else if (nv == base) strcpy(t, "ALG UNLOCKED");
    else { u_int((unsigned)nv + 1u, u); u_cat(t, u); }
    sp_toast(t);
}
static void sq_tempo_tile(uint32_t k)
{
    char t[16], u[12];
    if (sq_ext()) {                          /* a MIDI clock plays: its tempo, measured */
        uint32_t b = syn_bpm10_now();
        t[0] = 0; u_int(b / 10u, u); u_cat(t, u); u_cat(t, "."); u_int(b % 10u, u); u_cat(t, u);
        sq_tile(k, "MIDI CLOCK", R4P_KIND_UNI, sq_uni((int)b, 400, 2400), t);
        return;
    }
    t[0] = 0; u_int(syn.bpm10 / 10u, u); u_cat(t, u); u_cat(t, "."); u_int(syn.bpm10 % 10u, u); u_cat(t, u);
    sq_tile(k, "TEMPO", R4P_KIND_UNI, sq_uni(syn.bpm10, 400, 2400), t);
}
static void sq_build(void)
{
    r4p_page_t *g = &ui.page;
    sq_pat_t *pt = sq_ed_pat();
    uint32_t sel = sq_sel_pat();
    int v = sq_view();
    char t[24], u[12];
    memset(g, 0, sizeof *g);
    g->r = SY_AR; g->g = SY_AG; g->b = SY_AB;
    sq_pat_name(g->track, sel);
    if (sq.run && sq.rec) {
        strcpy(t, "REC "); u_cat(t, g->track); strcpy(g->track, t);
    }
    sy_lockview = 0;
    if (v == SQV_STEP) {
        uint32_t nh = sq_ed_held_n();
        int32_t alg = sq_lock_get(sel, (uint32_t)sq_ed.held, LK_ALG);
        if (nh > 1u) {
            u_int(nh, u); strcpy(g->title, u); u_cat(g->title, " STEPS");
        } else {
            strcpy(g->title, "STEP "); u_int((unsigned)sq_ed.held + 1u, u); u_cat(g->title, u);
        }
        if (sq_ed.lview) {
            u_cat(g->title, " "); u_cat(g->title, LKV[(sq_ed.lview - 1u) % 4u].name);
            sq_lock_tiles(sel);
        } else {
            if (alg != -1000) { u_cat(g->title, " ALG "); u_int((unsigned)alg + 1u, u); u_cat(g->title, u); }
            sq_step_tiles(&pt->s[sq_ed.held], pt, 1);
        }
    } else if (v == SQV_REC) {
        strcpy(g->title, "STEP REC");
        sq_num(t, sq_ed.cursor + 1, "/"); u_int(pt->len, u); u_cat(t, u);
        sq_tile(0, "STEP", R4P_KIND_UNI, sq_uni(sq_ed.cursor, 0, pt->len > 1u ? pt->len - 1 : 1), t);
        sq_note_text(&pt->s[sq_ed.cursor % SQ_STEPS], t);
        sq_tile(1, "NOTE", R4P_KIND_UNI, sq_uni(sq_n(&pt->s[sq_ed.cursor % SQ_STEPS]) ? pt->s[sq_ed.cursor % SQ_STEPS].note[0] : 0, 0, 127), t);
        sq_num(t, pt->len, "");
        sq_tile(2, "LENGTH", R4P_KIND_UNI, sq_uni(pt->len, 1, 64), t);
        sq_tempo_tile(3);
    } else if (sui.sub == 3) {                   /* SONG: the chain */
        uint32_t c = sqi.chain_cur, len = sq_ch.len;
        strcpy(g->title, "SONG");
        g->page_count = 4;
        g->page_idx = 3;
        if (c < len) { u_int(c + 1u, u); strcpy(t, u); u_cat(t, "/"); u_int(len, u); u_cat(t, u); }
        else strcpy(t, len >= SQ_CHAIN ? "FULL" : "NEW");
        sq_tile(0, "ENTRY", R4P_KIND_UNI, sq_uni((int)c, 0, (int)(len < SQ_CHAIN ? len : SQ_CHAIN - 1u)), t);
        if (sqi.del_arm) strcpy(t, "DEL?");
        else if (c < len) sq_pat_name(t, sq_ch.pat[c]);
        else strcpy(t, "--");
        sq_tile(1, "PATTERN", R4P_KIND_ENUM, sq_enum(c < len ? sq_ch.pat[c] : 0, SQ_PATS), t);
        if (c < len) { strcpy(t, "x"); u_int(sq_ch.rep[c], u); u_cat(t, u); } else strcpy(t, "--");
        sq_tile(2, "REPEATS", R4P_KIND_UNI, sq_uni(c < len ? sq_ch.rep[c] : 1, 1, 16), t);
        sq_tile(3, "SONG", R4P_KIND_TOGGLE, syn.song, syn.song ? "ON" : "OFF");
    } else {
        strcpy(g->title, "SEQUENCER");
        g->page_count = 4;
        g->page_idx = sui.sub;
        if (sui.sub == 0) {
            sq_pat_name(t, sel);
            sq_tile(0, "PATTERN", R4P_KIND_ENUM, sq_enum((int)sel, SQ_PATS), t);
            sq_num(t, pt->len, "");
            sq_tile(1, "LENGTH", R4P_KIND_UNI, sq_uni(pt->len, 1, 64), t);
            sq_num(t, pt->swing, "%");
            sq_tile(2, "SWING", R4P_KIND_UNI, sq_uni(pt->swing, 50, 75), t);
            sq_tempo_tile(3);
        } else if (sui.sub == 1) {
            sq_tile(0, "RATE", R4P_KIND_ENUM, sq_enum(pt->rate, SQR_N), N_SQRATE[pt->rate % SQR_N]);
            sq_num(t, pt->gate, "%");
            sq_tile(1, "GATE", R4P_KIND_UNI, sq_uni(pt->gate, 5, 100), t);
            sq_num(t, pt->vel, "");
            sq_tile(2, "VELOCITY", R4P_KIND_UNI, sq_uni(pt->vel, 1, 127), t);
            sq_num(t, syn.seq_trn, " st");
            if (syn.seq_trn > 0) { strcpy(u, t); strcpy(t, "+"); u_cat(t, u); }
            sq_tile(3, "TRANSPOSE", R4P_KIND_BI, (uint16_t)(8192 + syn.seq_trn * 8191 / 24), t);
        } else {
            sq_num(t, sq_ed.cursor + 1, "/"); u_int(pt->len, u); u_cat(t, u);
            sq_tile(0, "STEP", R4P_KIND_UNI, sq_uni(sq_ed.cursor, 0, pt->len > 1u ? pt->len - 1 : 1), t);
            if (sqi.copy_to) sq_pat_name(t, sqi.copy_to - 1u); else strcpy(t, "--");
            sq_tile(1, "COPY TO", R4P_KIND_ENUM, sq_enum(sqi.copy_to, SQ_PATS + 1), t);
            sq_tile(2, "CLEAR", R4P_KIND_TOGGLE, sqi.clear, sqi.clear ? "SURE?" : "--");
            sq_tile(3, "CLOCK", R4P_KIND_ENUM, sq_enum((int)sq_clock_mode(), SQC_N), N_CLOCK[sq_clock_mode()]);
        }
    }
    ui.dirty |= D_HEAD | D_TILES | D_FOOT;
}
static void sq_arm_toast(void)
{
    char t[24], u[8];
    if (sqi.copy_to) {
        strcpy(t, "SAVE: COPY TO "); sq_pat_name(u, sqi.copy_to - 1u); u_cat(t, u);
    } else if (sqi.clear) {
        strcpy(t, "SAVE: CLEAR "); sq_pat_name(u, sq_sel_pat()); u_cat(t, u);
    } else {
        return;
    }
    sp_toast(t);
}
static void sq_turn(uint32_t k, int d)
{
    sq_pat_t *pt = sq_ed_pat();
    uint32_t sel = sq_sel_pat();
    int v = sq_view(), x;
    if (v == SQV_STEP) {
        if (sq_ed.lview) {
            sq_lock_turn(k, d);
        } else {
            uint32_t i;
            for (i = 0; i < SQ_STEPS; i++)
                if (sq_ed_is_held(i))
                    sq_ed_step_turn(i, k, d);
        }
    } else if (sui.sub == 3) {                   /* SONG: ENTRY, PATTERN, REPEATS, SONG on / off */
        uint32_t c = sqi.chain_cur, len = sq_ch.len;
        if (k == 0) {
            x = (int)c + d;
            x = x < 0 ? 0 : x > (int)(len < SQ_CHAIN ? len : SQ_CHAIN - 1u) ? (int)(len < SQ_CHAIN ? len : SQ_CHAIN - 1u) : x;
            sqi.chain_cur = (uint8_t)x;
            sqi.del_arm = 0;
        } else if (k == 1) {
            if (c >= len) {                      /* the new entry: the last one's pattern (or the one selected) to start */
                sq_chain_set(c, len ? sq_ch.pat[len - 1u] : sel, 1);
            } else {
                x = (int)sq_ch.pat[c] + (d > 0 ? 1 : -1);
                if (x < 0) {
                    sqi.del_arm = 1;
                    sp_toast("SAVE: DELETE ENTRY");
                } else {
                    sqi.del_arm = 0;
                    sq_chain_set(c, (uint32_t)(x > SQ_PATS - 1 ? SQ_PATS - 1 : x), sq_ch.rep[c]);
                }
            }
        } else if (k == 2 && c < len) {
            x = (int)sq_ch.rep[c] + d;
            sq_chain_set(c, sq_ch.pat[c], (uint32_t)(x < 1 ? 1 : x > 16 ? 16 : x));
        } else if (k == 3) {
            syn.song = (uint8_t)(d > 0);
            sui.dirty_set = 1;
        }
    } else if (v == SQV_REC || (sui.sub == 2 && k == 0)) {
        if (k == 0) {
            x = (int)sq_ed.cursor + d;
            x = x < 0 ? 0 : x >= pt->len ? pt->len - 1 : x;
            sq_ed.cursor = (uint8_t)x;
            sq_ed.view = (uint8_t)(x / SQ_PAGE);
            sq_ed.follow = 0;
            sq_ed.entry_open = 0;
        } else if (k == 1) {
            sq_ed_step_turn(sq_ed.cursor, 0, d);
        } else if (k == 2) {
            x = (int)pt->len + d;
            pt->len = (uint8_t)(x < 1 ? 1 : x > SQ_STEPS ? SQ_STEPS : x);
            sq_touch(sel);
        } else {
            x = (int)syn.bpm10 + 10 * d;
            syn.bpm10 = (uint16_t)(x < 400 ? 400 : x > 2400 ? 2400 : x);
            sui.dirty_set = 1;
        }
    } else if (sui.sub == 0) {
        if (k == 0) {
            x = (int)sel + (d > 0 ? 1 : -1);
            syn.seq_pat = (uint8_t)(x < 0 ? 0 : x >= SQ_PATS ? SQ_PATS - 1 : x);
            sq_ed.view = 0;
            sq_disarm();
            sui.dirty_set = 1;
        } else if (k == 1) {
            x = (int)pt->len + d;
            pt->len = (uint8_t)(x < 1 ? 1 : x > SQ_STEPS ? SQ_STEPS : x);
            sq_touch(sel);
        } else if (k == 2) {
            x = (int)pt->swing + d;
            pt->swing = (uint8_t)(x < 50 ? 50 : x > 75 ? 75 : x);
            sq_touch(sel);
        } else {
            x = (int)syn.bpm10 + 10 * d;
            syn.bpm10 = (uint16_t)(x < 400 ? 400 : x > 2400 ? 2400 : x);
            sui.dirty_set = 1;
        }
    } else if (sui.sub == 1) {
        if (k == 0) {
            x = (int)pt->rate + (d > 0 ? 1 : -1);
            pt->rate = (uint8_t)(x < 0 ? 0 : x >= SQR_N ? SQR_N - 1 : x);
        } else if (k == 1) {
            x = (int)pt->gate + 5 * d;
            pt->gate = (uint8_t)(x < 5 ? 5 : x > 100 ? 100 : x);
        } else if (k == 2) {
            x = (int)pt->vel + d;
            pt->vel = (uint8_t)(x < 1 ? 1 : x > 127 ? 127 : x);
        } else {
            x = (int)syn.seq_trn + d;
            syn.seq_trn = (int8_t)(x < -24 ? -24 : x > 24 ? 24 : x);
            sui.dirty_set = 1;
        }
        if (k < 3)
            sq_touch(sel);
    } else {
        if (k == 1) {
            x = (int)sqi.copy_to + (d > 0 ? 1 : -1);
            sqi.copy_to = (uint8_t)(x < 0 ? 0 : x > SQ_PATS ? SQ_PATS : x);
            sqi.clear = 0;
            sqi.arm_at = plat_ms();
            sq_arm_toast();
        } else if (k == 2) {
            sqi.clear = (uint8_t)(d > 0);
            sqi.copy_to = 0;
            sqi.arm_at = plat_ms();
            sq_arm_toast();
        } else if (k == 3) {
            x = (int)sq_clock_mode() + (d > 0 ? 1 : -1);
            syn.seq_clock = (uint8_t)(x < 0 ? 0 : x >= SQC_N ? SQC_N - 1 : x);
            sui.dirty_set = 1;
        }
    }
    sui.edit_at = plat_ms();
    sq_build();
}
/* SAVE on the SEQ page: an armed COPY TO / CLEAR happens; else 0 (SAVE stores the voice, as everywhere) */
static int sq_save_press(void)
{
    char t[24], u[8];
    if (sui.page == SP_SEQ && sq_ed.held >= 0) {          /* a step held: its locks go (all held steps') */
        uint32_t i;
        for (i = 0; i < SQ_STEPS; i++)
            if (sq_ed_is_held(i))
                sq_lock_clear_step(sq_sel_pat(), i);
        sq_ed.held_used = 1;
        sp_toast("LOCKS CLEARED");
        sq_build();
        return 1;
    }
    if (sui.page == SP_SEQ && sui.sub == 3 && sq_ed.held < 0) {   /* SONG: SAVE deletes (armed) or inserts a copy */
        if (sqi.del_arm) {
            sq_chain_delete(sqi.chain_cur);
            sqi.del_arm = 0;
            if (sqi.chain_cur > sq_ch.len) sqi.chain_cur = sq_ch.len;
            sp_toast("ENTRY DELETED");
        } else if (sqi.chain_cur < sq_ch.len && sq_ch.len < SQ_CHAIN) {
            sq_chain_insert(sqi.chain_cur);
            sqi.chain_cur++;
            sp_toast("ENTRY INSERTED");
        } else {
            sp_toast(sq_ch.len >= SQ_CHAIN ? "SONG FULL" : "TURN PATTERN TO ADD");
        }
        sui.edit_at = plat_ms();
        sq_build();
        return 1;
    }
    if (sui.page != SP_SEQ || (!sqi.copy_to && !sqi.clear))
        return 0;
    if (sqi.copy_to) {
        sq_copy(sq_sel_pat(), sqi.copy_to - 1u);
        strcpy(t, "COPIED TO "); sq_pat_name(u, sqi.copy_to - 1u); u_cat(t, u);
    } else {
        sq_clear(sq_sel_pat());
        strcpy(t, "CLEARED "); sq_pat_name(u, sq_sel_pat()); u_cat(t, u);
        sq_ed.cursor = 0;
        sq_ed.view = 0;
    }
    sq_disarm();
    sui.edit_at = plat_ms();
    sp_toast(t);
    sq_build();
    return 1;
}
static void sq_transpose_key(int note)        /* SEL + a key: the sequence plays from that key (C4 = +0) */
{
    char t[24];
    int x = note - 60;
    syn.seq_trn = (int8_t)(x < -24 ? -24 : x > 24 ? 24 : x);
    sui.dirty_set = 1;
    sui.edit_at = plat_ms();
    sq_num(t, syn.seq_trn, " ST");
    {
        char u[24] = "TRANSPOSE ";
        if (syn.seq_trn > 0) u_cat(u, "+");
        u_cat(u, t);
        sp_toast(u);
    }
    if (sui.page == SP_SEQ)
        sq_build();
}
/* the main loop, once a frame on the SEQ page: the view follows the playing step; the grid redraws when it moved */
static void sq_frame(uint32_t now)
{
    if ((sqi.copy_to || sqi.clear) && now - sqi.arm_at > 8000u) {
        sq_disarm();
        if (sui.page == SP_SEQ) sq_build();
    }
    if (sui.page != SP_SEQ || ui.setup)
        return;
    if (sq.run && sq.in_song && sq_ed.follow && sq_ed.held < 0 && syn.seq_pat != sq.pat) {
        syn.seq_pat = sq.pat;                 /* the song plays: the grid shows the pattern playing (until one is picked) */
        sq_build();
    }
    if (sq.run && sq_ed.follow && sq_ed.held < 0 && !(sq.rec && !sq.run) && sq.pat == sq_sel_pat())
        sq_ed.view = (uint8_t)(sq.step / SQ_PAGE);
    if (sq_ed.view * SQ_PAGE >= sq_ed_pat()->len)
        sq_ed.view = (uint8_t)((sq_ed_pat()->len - 1u) / SQ_PAGE);
    if (sqi.drawn_step != sq.step || sqi.drawn_pat != sq.pat || sqi.drawn_run != sq.run || sqi.drawn_view != sq_ed.view ||
        sqi.drawn_ci != sq.ci || sqi.drawn_rep != sq.rep_n) {
        sqi.drawn_step = sq.step;
        sqi.drawn_pat = sq.pat;
        sqi.drawn_run = sq.run;
        sqi.drawn_view = sq_ed.view;
        sqi.drawn_ci = sq.ci;
        sqi.drawn_rep = sq.rep_n;
        ui.dirty |= D_TILE0;                  /* (the grid) */
    }
}

/* ------------------------------------------------------------- the look --- */
/* Synth mode draws its own screen (ui.c calls sp_draw_* for it): black, greys and white, one warm accent for where you
 * are (the page, the knob just turned, the playhead). Every page: a header (title, voice, a strip of the eight pages),
 * a band 232 x 82 that shows the sound (the output's scope, the six operators, the envelopes, the LFO, the step grid),
 * and the four knobs' tiles in a row, as the knobs sit. The band's live views redraw at most 25 times a second and
 * only the rows that change. */
#define SY_BAR    RGB(196, 196, 202)          /* bars, traces */
#define SY_GLOW   RGB(62, 62, 70)             /* the trace's soft edge */
#define SY_MID    RGB(110, 110, 118)
#define BAND_Y 34
#define BAND_H 82
#define LIVE_MS 40u                            /* 25 frames a second at most */
enum { BV_SCOPE, BV_OPS, BV_ENV, BV_LFO, BV_GRID };
static struct {
    int16_t snap[SYN_SCOPE_N];
    int32_t disp;                              /* the scope's scale (a peak, released slowly) */
    uint8_t flat;                              /* the last trace was the idle line */
    uint32_t at;                               /* the last live frame (plat_ms) */
    uint32_t frames, bytes, skipped, ticks;    /* live frames drawn, bytes blitted, frames with nothing to send, host time */
    uint8_t lfo_odd;
} sy;
static uint16_t sy_accent(int pct) { return RGB(SY_AR * 2 * pct / 100, SY_AG * 2 * pct / 100, SY_AB * 2 * pct / 100); }
static int sp_band_view(void)
{
    if (ui.setup) return BV_SCOPE;
    switch (sui.page) {
    case SP_EDIT: return BV_OPS;
    case SP_ENV: return BV_ENV;
    case SP_LFO: return BV_LFO;
    case SP_SEQ: return BV_GRID;
    default: return BV_SCOPE;
    }
}

/* the header: title left, the voice right (grey), the eight pages as a strip under them (this one in the accent) */
static void sp_draw_head(void)
{
    char t[R4P_STR_MAX + 1];
    int32_t w = PG->track[0] ? pwidth(&FONT_B, PG->track) : 0, i;
    cv_begin(240, 34, R4_BG);
    if (w)
        ptext(232 - w, 7, &FONT_B, PG->track, R4_LABEL);
    fit(t, PG->title, &FONT_B, 232 - w - 18);
    ptext(8, 7, &FONT_B, t, R4_TEXT);
    for (i = 0; i < SP_N; i++)
        cv_rect(8 + i * 29, 28, 26, 2, !ui.setup && i == sui.page ? sy_accent(100) : R4_TILE);
    cv_blit(0, 0);
}

/* one knob's tile of the row (x .. x + 54, y 2..81 of a canvas 84 high): name, value, a thin bar; hot = in the accent;
 * base >= 0: a locked value's base value (value14 as the slot's), a white tick on the bar */
static void row_tile(int32_t x, const r4p_slot_t *s, int hot, int32_t base)
{
    static const char *const SHORT[][2] = {{"AFTERTOUCH", "AFTERTCH"}, {"KEY CHANNEL", "KEY CH"}, {"BEND RANGE", "BEND"},
                                           {"REVERB TYPE", "REV TYPE"}, {"REVERB SIZE", "REV SIZE"}, {"DELAY FDBK", "DLY FDBK"},
                                           {"DELAY TIME", "DLY TIME"}, {"USER VOICES", "VOICES"}};
    int32_t w = 55, bw = 44, bx = x + 7, by = 66, fill, n, i;
    uint16_t ac = hot ? sy_accent(100) : SY_BAR;
    const felucca_font_t *f = pwidth(&FONT_M, s->text) <= w - 9 ? &FONT_M : pwidth(&FONT_B, s->text) <= w - 9 ? &FONT_B : &FONT_XS;
    char nm[R4P_STR_MAX + 1];
    const char *name = s->name;
    if (s->kind == R4P_KIND_EMPTY) {
        cv_rect(x, 2, w, 1, R4_TILE);
        return;
    }
    cv_rect(x, 2, w, 80, R4_TILE);
    cv_rect(x, 2, 2, 80, hot ? sy_accent(100) : R4_EDGE);
    for (i = 0; i < (int32_t)(sizeof SHORT / sizeof SHORT[0]); i++)
        if (!strcmp(name, SHORT[i][0]))
            name = SHORT[i][1];
    if (pwidth(&FONT_XS, name) <= w - 9) {
        ptext(x + 7, 7, &FONT_XS, name, hot ? sy_accent(100) : R4_LABEL);
    } else {                                   /* a long name: the small capitals */
        fit(nm, name, &FONT_T, w - 8);
        ptext(x + 6, 9, &FONT_T, nm, hot ? sy_accent(100) : R4_LABEL);
    }
    ptext(x + 7, f == &FONT_M ? 28 : 33, f, s->text, R4_TEXT);
    switch (s->kind) {
    case R4P_KIND_UNI:
        cv_rect(bx, by + 2, bw, 2, R4_EDGE);
        fill = s->value * bw / R4P_V14_MAX;
        cv_rect(bx, by, fill, 6, ac);
        break;
    case R4P_KIND_BI:
        cv_rect(bx, by + 2, bw, 2, R4_EDGE);
        fill = ((int32_t)s->value - 8192) * (bw / 2) / 8191;
        if (fill >= 0) cv_rect(bx + bw / 2, by, fill, 6, ac);
        else cv_rect(bx + bw / 2 + fill, by, -fill, 6, ac);
        cv_rect(bx + bw / 2, by - 2, 1, 10, R4_LABEL);
        break;
    case R4P_KIND_ENUM:
        n = (s->value >> 7) & 0x7F;
        n = n < 2 ? 2 : n;
        for (i = 0; i < n && n <= 12; i++)
            cv_rect(bx + i * bw / n, by + 2, bw / n - 1 > 1 ? bw / n - 1 : 1, 2, R4_EDGE);
        if (n > 12)
            cv_rect(bx, by + 2, bw, 2, R4_EDGE);
        i = s->value & 0x7F;
        cv_rect(bx + i * bw / n, by, bw / n - 1 > 2 ? bw / n - 1 : 2, 6, ac);
        break;
    case R4P_KIND_TOGGLE:
        cv_rect(bx, by - 2, 24, 10, s->value ? ac : R4_EDGE);
        cv_rect(s->value ? bx + 15 : bx + 2, by, 7, 6, s->value ? R4_BG : R4_DIM);
        break;
    default:
        break;
    }
    if (base >= 0 && s->kind != R4P_KIND_TOGGLE) {        /* the base value under a lock */
        if (s->kind == R4P_KIND_ENUM) {
            n = (s->value >> 7) & 0x7F;
            n = n < 2 ? 2 : n;
            fill = (base & 0x7F) * bw / n + bw / n / 2;
        } else {
            fill = base * bw / R4P_V14_MAX;
        }
        cv_rect(bx + fill, by - 3, 1, 12, R4_TEXT);
    }
}
/* the four tiles in a row (y 116..199): the knob turned last in the accent */
static void sp_draw_tiles(void)
{
    uint32_t k, now = plat_ms();
    cv_begin(240, 84, R4_BG);
    for (k = 0; k < 4u; k++) {
        if (sy_lockview && !ui.setup)          /* a held step's lock page: the locked tiles in the accent */
            row_tile(4 + (int32_t)k * 59, &PG->slot[k], (sy_lockhot >> k) & 1u, sy_base[k]);
        else
            row_tile(4 + (int32_t)k * 59, &PG->slot[k], sy_touch_k == k + 1u && now - sy_touch_at < 2000u, -1);
    }
    cv_blit(0, 116);
}

/* ---- the band's views. Each draws into a canvas of band rows r0 .. r1 - 1 (band coordinates 0..81) ---- */
static void sy_rows(uint32_t r0, uint32_t r1)  /* a canvas for band rows r0 .. r1 - 1, drawn in band coordinates */
{
    cv_begin(232, r1 - r0, R4_BG);
    cv_oy = -(int32_t)r0;
    cv_y0 = 0;
    cv_y1 = (int32_t)(r1 - r0);
}
static void sy_send(uint32_t r0)
{
    sy.bytes += cv_w * cv_h * 2u;
    cv_oy = 0;
    cv_y0 = 0;
    cv_y1 = (int32_t)cv_h;
    lcd_blit(4, BAND_Y + r0, cv_w, cv_h, cv_px);
}

/* The scope (after Felucca's HOME scope, ui_graph.c graph_scope, GPL-3.0-only: a ring of every other output sample, a
 * rising zero crossing as the trigger, an auto-scaled trace). Here: the trigger wants the signal to have been below
 * -1/16 of the peak shortly before the crossing (no jitter on noise or harmonics), the scale follows the peak at once
 * and lets go slowly (a decaying note shrinks calmly to the idle line), and the trace is 2 px with a dim edge. A live
 * frame is drawn whole in RAM but sent as 15 strips 16 px wide, each only as tall as the old and the new trace span
 * there (a sine sends a fifth of the band); the idle line, once drawn, sends nothing. */
#define SC_W 232
#define SC_MID 41
#define SC_A 34
#define SC_G 16                                /* the strips' width */
static uint8_t sc_prev[SC_W];                  /* the rows the last trace took, per column (its top) */
static uint16_t sc_strip[2][SC_G * BAND_H];    /* two strips: one is filled while the other is sent */
static void sy_scope(int full)
{
    uint32_t w = syn_scope_w, i, trig = SYN_SCOPE_N - SC_W, g;
    int32_t peak = 0, hyst, x, py, y, flat, ys[SC_W];
    uint32_t t0 = plat_ticks24();
    for (i = 0; i < SYN_SCOPE_N; i++) {
        int32_t v = sy.snap[i] = syn_scope[(w + i) & (SYN_SCOPE_N - 1u)];
        v = v < 0 ? -v : v;
        if (v > peak) peak = v;
    }
    hyst = peak / 16 + 8;
    for (i = 8; i < SYN_SCOPE_N - SC_W - 1u; i++)
        if (sy.snap[i - 1] < 0 && sy.snap[i] >= 0) {
            uint32_t j;
            for (j = i - 8; j < i && sy.snap[j] > -hyst; j++)
                ;
            if (j < i) { trig = i; break; }
        }
    sy.disp = peak > sy.disp ? peak : sy.disp - sy.disp / 10;
    if (sy.disp < 1400)
        sy.disp = 1400;                            /* the floor: a whisper stays small, silence is a line */
    flat = peak < 40;
    if (flat && sy.flat && !full) {                /* still the idle line: nothing to send */
        sy.skipped++;
        return;
    }
    for (x = 0; x < SC_W; x++) {
        y = SC_MID - sy.snap[trig + (uint32_t)x] * SC_A / sy.disp;
        ys[x] = y < 3 ? 3 : y > BAND_H - 5 ? BAND_H - 5 : y;
    }
    cv_begin(SC_W, BAND_H, R4_BG);
    for (x = 0; x < SC_W; x += 4)                  /* the idle line, dotted */
        cv_rect(x, SC_MID, 2, 1, R4_EDGE);
    py = ys[0];
    for (x = 1; x < SC_W; x++) {                   /* the soft edge first, then the trace */
        cv_line(x - 1, py - 1, x, ys[x] - 1, SY_GLOW);
        cv_line(x - 1, py + 2, x, ys[x] + 2, SY_GLOW);
        py = ys[x];
    }
    py = ys[0];
    for (x = 1; x < SC_W; x++) {
        cv_line(x - 1, py, x, ys[x], flat ? SY_MID : R4_TEXT);
        cv_line(x - 1, py + 1, x, ys[x] + 1, flat ? SY_MID : SY_BAR);
        py = ys[x];
    }
    if (full) {
        sy.bytes += SC_W * BAND_H * 2u;
        cv_blit(4, BAND_Y);
    } else {
        for (g = 0; g < SC_W / SC_G + (SC_W % SC_G != 0u); g++) {
            int32_t x0 = (int32_t)(g * SC_G), x1 = x0 + SC_G > SC_W ? SC_W : x0 + SC_G, lo = BAND_H, hi = 0, r, c;
            uint16_t *d = sc_strip[g & 1u];
            for (x = x0 - 1; x <= x1; x++) {        /* (a column either side: the lines join there) */
                int32_t xx = x < 0 ? 0 : x >= SC_W ? SC_W - 1 : x, a = ys[xx], b = sc_prev[xx];
                if (a < lo) lo = a;
                if (b < lo) lo = b;
                if (a > hi) hi = a;
                if (b > hi) hi = b;
            }
            lo = lo - 2 < 0 ? 0 : lo - 2;
            hi = hi + 4 > BAND_H ? BAND_H : hi + 4;
            if (lo > SC_MID) lo = SC_MID;          /* (the idle line under it) */
            if (hi <= SC_MID) hi = SC_MID + 1;
            for (r = lo; r < hi; r++)
                for (c = x0; c < x1; c++)
                    d[(r - lo) * (x1 - x0) + (c - x0)] = cv_px[r * SC_W + c];
            lcd_blit((uint32_t)(4 + x0), (uint32_t)(BAND_Y + lo), (uint32_t)(x1 - x0), (uint32_t)(hi - lo), d);
            sy.bytes += (uint32_t)((x1 - x0) * (hi - lo) * 2);
        }
    }
    for (x = 0; x < SC_W; x++)
        sc_prev[x] = (uint8_t)ys[x];
    sy.flat = (uint8_t)flat;
    sy.frames++;
    sy.ticks += plat_ticks24() - t0;
}

/* the six operators: a column each (OP1 left), the patch's output level as an outline, the sounding level of the newest
 * note as the fill (it moves with the envelopes), carriers with a base line, the operator being edited in the accent,
 * its ratio (or FIX) under it */
static const syn_voice_t *sy_newest(void)
{
    const syn_voice_t *v = 0;
    uint32_t i;
    for (i = 0; i < SYN_VOICES; i++)
        if (syn_v[i].n.live && (!v || syn_v[i].age > v->age))
            v = &syn_v[i];
    return v;
}
static void sy_ops(int full)
{
    const syn_voice_t *v = sy_newest();
    uint32_t car = fm6_carriers(syn.cur[FP_ALG] & 31u), k;
    char t[12], u[12];
    if (!full && !v && sy.flat)
        return;
    sy_rows(full ? 0u : 14u, full ? BAND_H : 66u);
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = syn.cur + (5u - k) * FP_OP;
        int32_t x = 2 + (int32_t)k * 38, cw = 34, top = 16, h = 48, ol = op[FP_OL], lvl = 0;
        int cur = k == sui.op, carrier = (car >> (5u - k)) & 1u;
        uint16_t c = cur ? sy_accent(100) : SY_BAR;
        if (v) {
            int32_t g = v->n.op[5u - k].gain_out;
            g = g < 0 ? 0 : g > (1 << 24) ? (1 << 24) : g;
            lvl = (int32_t)((int64_t)g * h >> 24);
        }
        if (full) {
            t[0] = 'O'; t[1] = 'P'; t[2] = (char)('1' + k); t[3] = 0;
            ptext(x + 2, 0, &FONT_XS, t, cur ? sy_accent(100) : carrier ? R4_TEXT : R4_LABEL);
            if (op[FP_MODE]) strcpy(t, "FIX");
            else {
                uint32_t r100 = op[FP_FC] ? op[FP_FC] * 100u : 50u;
                r100 += r100 * op[FP_FF] / 100u;
                t[0] = 0; u_int(r100 / 100u, u); u_cat(t, u); u_cat(t, "."); u_int(r100 % 100u / 10u, u); u_cat(t, u);
                u_int(r100 % 10u, u); u_cat(t, u);
            }
            ptext(x + 2, 67, &FONT_XS, t, cur ? sy_accent(100) : R4_LABEL);
        }
        cv_rect(x, top, cw, h, R4_TILE);
        cv_rect(x, top + h - ol * h / 99, cw, 1, cur ? sy_accent(60) : SY_MID);      /* the patch's level */
        if (lvl)
            cv_rect(x + 3, top + h - lvl, cw - 6, lvl, c);
        if (carrier)
            cv_rect(x, top + h - 1, cw, 2, cur ? sy_accent(100) : R4_TEXT);
    }
    sy_send(full ? 0u : 14u);
    sy.flat = (uint8_t)!v;
    sy.frames++;
}

/* the envelopes: the four rates and levels of each operator as a line (the one edited bright, the others dim, a hold
 * for the sustain); while a note sounds the stage it is in is marked on the edited one */
static void sy_env_pts(const uint8_t *op, int32_t *xs, int32_t *ys)
{
    int32_t d[4], i, tot = 0, sc;
    for (i = 0; i < 4; i++) {
        int32_t r = 99 - op[FP_R1 + i];
        d[i] = 3 + r * r / 40;
        tot += d[i];
    }
    sc = tot > 0 ? tot : 1;
    xs[0] = 4;
    ys[0] = 72 - op[FP_L1 + 3] * 60 / 99;
    for (i = 0; i < 3; i++) {
        xs[i + 1] = xs[i] + d[i] * 180 / sc;
        ys[i + 1] = 72 - op[FP_L1 + i] * 60 / 99;
    }
    xs[4] = xs[3] + 36;                            /* the sustain */
    ys[4] = ys[3];
    xs[5] = xs[4] + d[3] * 180 / sc;
    ys[5] = ys[0];
}
static void sy_env(int full)
{
    const syn_voice_t *v = sy_newest();
    int32_t xs[6], ys[6], i;
    uint32_t k;
    char t[8];
    if (!full && !v && sy.flat)
        return;
    sy_rows(0, BAND_H);
    for (k = 0; k < 6u; k++) {
        if (k == sui.op)
            continue;
        sy_env_pts(syn.cur + (5u - k) * FP_OP, xs, ys);
        for (i = 0; i < 5; i++)
            cv_line(xs[i], ys[i], xs[i + 1], ys[i + 1], R4_EDGE);
    }
    sy_env_pts(syn.cur + (5u - sui.op) * FP_OP, xs, ys);
    for (i = 0; i < 5; i++) {
        int on = v && ((i < 3 && v->n.env[5u - sui.op].ix == (uint8_t)i) || (i == 3 && v->n.env[5u - sui.op].ix == 3u && v->key) ||
                       (i == 4 && !v->key));
        cv_line(xs[i], ys[i], xs[i + 1], ys[i + 1], on ? sy_accent(100) : R4_TEXT);
        cv_line(xs[i], ys[i] + 1, xs[i + 1], ys[i + 1] + 1, on ? sy_accent(100) : SY_BAR);
    }
    for (i = 1; i < 5; i++)
        cv_rect(xs[i] - 1, ys[i] - 1, 3, 3, R4_TEXT);
    t[0] = 'O'; t[1] = 'P'; t[2] = (char)('1' + sui.op); t[3] = 0;
    ptext(4, 0, &FONT_XS, t, sy_accent(100));
    sy_send(0);
    sy.flat = (uint8_t)!v;
    sy.frames++;
}

/* the LFO: two cycles of its wave and a dot where it is now */
static int32_t sy_lfo_at(uint32_t wave, uint32_t ph)       /* 0..1 as 0..1024 (the shapes of fm6_lfo_sample) */
{
    switch (wave) {
    case 0: return (int32_t)((ph >> 31) ? (~ph >> 21) : (ph >> 21));
    case 1: return (int32_t)(1023u - (ph >> 22));
    case 2: return (int32_t)(ph >> 22);
    case 3: return ph >> 31 ? 0 : 1023;
    case 4: return 512 + fm6_sin((int32_t)(ph >> 8)) / 64;
    default: return (int32_t)(((ph >> 30) * 389u + 211u) & 1023u);   /* S&H: steps (the shape, not its values) */
    }
}
static void sy_lfo(int full)
{
    uint32_t wave = syn.cur[FP_LFW] % 6u, x;
    int32_t py = 0, y, dx, dy;
    if (!full && (sy.lfo_odd ^= 1u))
        return;                                    /* (12.5 frames a second: the whole band moves) */
    sy_rows(0, BAND_H);
    for (x = 0; x < SC_W; x += 4)
        cv_rect((int32_t)x, 41, 2, 1, R4_EDGE);
    for (x = 0; x < SC_W; x++) {
        y = 70 - sy_lfo_at(wave, (uint32_t)(x * (0x200000000ull / SC_W))) * 58 / 1024;
        if (x)
            cv_line((int32_t)x - 1, py, (int32_t)x, y, R4_TEXT), cv_line((int32_t)x - 1, py + 1, (int32_t)x, y + 1, SY_BAR);
        py = y;
    }
    dx = (int32_t)((syn_lfo.phase >> 16) * (uint32_t)(SC_W / 2) >> 16);
    dy = 70 - sy_lfo_at(wave, syn_lfo.phase) * 58 / 1024;
    cv_rect(dx - 3, dy - 3, 7, 7, sy_accent(100));
    cv_rect(dx + SC_W / 2 - 2, dy - 2, 5, 5, sy_accent(50));
    ptext(4, 0, &FONT_XS, N_WAVE[wave], R4_LABEL);
    sy_send(0);
    sy.frames++;
}

/* the step grid (the SEQ page) */
static void sq_draw_grid(void)
{
    const sq_pat_t *pt = sq_ed_pat();
    uint32_t sel = sq_sel_pat(), i, k, len = pt->len, view = sq_ed.view, lo = 127, hi = 0, pg;
    int v = sq_view(), playing = sq.run && sq.pat == sel;
    char t[40], u[12];
    for (i = 0; i < len; i++)                 /* the pattern's note range: the ticks' scale */
        for (k = 0; k < sq_n(&pt->s[i]); k++) {
            if (pt->s[i].note[k] < lo) lo = pt->s[i].note[k];
            if (pt->s[i].note[k] > hi) hi = pt->s[i].note[k];
        }
    if (hi < lo) { lo = 48; hi = 72; }
    if (hi - lo < 12u) { uint32_t c = (hi + lo) / 2u; lo = c < 6u ? 0u : c - 6u; hi = lo + 12u; }
    cv_begin(240, 82, R4_BG);
    for (i = 0; i < SQ_PAGE; i++) {
        uint32_t idx = view * SQ_PAGE + i;
        int32_t x = 10 + (int32_t)i * 13 + (int32_t)(i / 4u) * 4, y = 4, h = 40;
        const sq_step_t *s = &pt->s[idx];
        int on = sq_is_on(s), has = sq_has(s);
        if (idx >= len) {
            cv_rect(x + 5, y + h / 2, 2, 2, R4_EDGE);
            continue;
        }
        if (on && sq_is_tie(s)) {
            cv_rect(x, y, 12, h, R4_TILE);
            cv_rect(x - 2, y + h / 2 - 2, 14, 5, SY_MID);           /* a TIE: the note goes on through it */
        } else {
            cv_rect(x, y, 12, h, on && has ? SY_BAR : R4_TILE);
            if (!(on && has))
                cv_rect(x, y, 12, 1, R4_EDGE);
            for (k = 0; k < sq_n(s); k++) {   /* the notes: a tick each, by pitch */
                int32_t ty = y + h - 4 - (int32_t)((s->note[k] - lo) * (uint32_t)(h - 6) / (hi - lo));
                cv_rect(x + 2, ty, 8, 2, on ? R4_BG : R4_DIM);
            }
            if (on && has && s->chance)
                cv_rect(x + 9, y + 1, 2, 2, R4_BG);              /* a chance: a notch in the corner */
        }
        if (playing && sq.step == idx)
            cv_rect(x, y + h + 2, 12, 4, sy_accent(100));
        if ((v == SQV_REC && sq_ed.cursor == idx) || (v == SQV_PAGE && sui.sub == 2 && sq_ed.cursor == idx && !playing))
            cv_rect(x, y + h + 2, 12, 4, R4_RED);
        if (sq_step_locks(sel, idx))
            cv_rect(x + 2, y - 3, 8, 2, sy_accent(100));      /* a parameter lock on it: a mark over the cell */
        if (v == SQV_STEP && sq_ed_is_held(idx)) {
            cv_rect(x - 1, y - 1, 14, 1, sy_accent(100)); cv_rect(x - 1, y + h, 14, 1, sy_accent(100));
            cv_rect(x - 1, y - 1, 1, h + 2, sy_accent(100)); cv_rect(x + 12, y - 1, 1, h + 2, sy_accent(100));
        }
    }
    for (pg = 0; pg < SQ_STEPS / SQ_PAGE; pg++) {     /* the four step pages (the black keys) */
        int32_t x = 10 + (int32_t)pg * 56, y = 54;
        int in = pg * SQ_PAGE < len;
        cv_rect(x, y, 52, 10, pg == view ? SY_BAR : in ? R4_EDGE : R4_TILE);
        cv_rect(x + 1, y + 1, 50, 8, R4_BG);
        for (i = 0; i < SQ_PAGE && in; i++) {
            uint32_t idx = pg * SQ_PAGE + i;
            uint16_t c = idx >= len ? R4_BG : sq_plays(&pt->s[idx]) ? R4_LABEL : R4_TILE;
            if (playing && sq.step == idx)
                c = sy_accent(100);
            cv_rect(x + 2 + (int32_t)i * 3, y + 2, 2, 6, c);
        }
    }
    t[0] = 0;                                 /* LEN 16  1/16  SW 50%  +0 ST  (NEXT P02) */
    u_cat(t, "LEN "); u_int(len, u); u_cat(t, u);
    u_cat(t, "  "); u_cat(t, N_SQRATE[pt->rate % SQR_N]);
    u_cat(t, "  SW "); u_int(pt->swing, u); u_cat(t, u); u_cat(t, "%");
    if (syn.seq_trn) { u_cat(t, "  "); if (syn.seq_trn > 0) u_cat(t, "+"); sq_num(u, syn.seq_trn, " ST"); u_cat(t, u); }
    ptext(10, 67, &FONT_XS, t, R4_LABEL);
    if (sq.run && sq.in_song) {               /* the song: entry n of len, loop r of its repeats */
        strcpy(t, "SONG "); u_int(sq.ci + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int(sq_ch.len, u); u_cat(t, u);
        u_cat(t, " x"); u_int(sq.rep_n + 1u, u); u_cat(t, u); u_cat(t, "/"); u_int(sq_ch.rep[sq.ci % SQ_CHAIN], u); u_cat(t, u);
        ptext(230 - pwidth(&FONT_XS, t), 67, &FONT_XS, t, sy_accent(100));
    } else if (sq.run && sq.pat != sel) {
        strcpy(t, "NEXT "); sq_pat_name(u, sel); u_cat(t, u);
        ptext(230 - pwidth(&FONT_XS, t), 67, &FONT_XS, t, sy_accent(100));
    } else if (sq_ext() || sq_clock_mode() == SQC_OUT) {
        strcpy(t, sq_ext() ? "CLOCK IN" : "CLOCK OUT");
        ptext(230 - pwidth(&FONT_XS, t), 67, &FONT_XS, t, R4_DIM);
    }
    sy.bytes += 240u * 82u * 2u;
    cv_blit(0, BAND_Y);
}

/* the SONG sub-page's band: the chain, 8 entries a row (pattern, repeats), the one playing lit, the cursor in red */
static void sq_draw_chain(void)
{
    uint32_t i;
    char t[8], u[8];
    cv_begin(240, 82, R4_BG);
    for (i = 0; i < SQ_CHAIN; i++) {
        int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 1 + (int32_t)(i / 8u) * 20;
        int playing = sq.run && sq.in_song && sq.ci == i, has = i < sq_ch.len;
        if (has) {
            cv_rect(x, y, 27, 18, playing ? sy_accent(100) : R4_TILE);
            u_int(sq_ch.pat[i] + 1u, u);
            strcpy(t, sq_ch.pat[i] < 9u ? "0" : ""); u_cat(t, u);
            ptext(x + 3, y + 3, &FONT_XS, t, playing ? R4_BG : R4_TEXT);
            if (sq_ch.rep[i] > 1u) {
                strcpy(t, "x"); u_int(sq_ch.rep[i], u); u_cat(t, u);
                ptext(x + 26 - pwidth(&FONT_T, t), y + 9, &FONT_T, t, playing ? R4_BG : R4_LABEL);
            }
        } else if (i == sq_ch.len) {
            cv_rect(x, y + 8, 27, 1, R4_EDGE);
            cv_rect(x + 13, y + 4, 1, 9, R4_EDGE);
        } else {
            cv_rect(x + 12, y + 8, 3, 2, R4_TILE);
        }
        if (i == sqi.chain_cur) {             /* the cursor */
            cv_rect(x, y + 18, 27, 1, R4_RED);
            cv_rect(x, y - 1 < 0 ? 0 : y - 1, 27, 1, R4_RED);
        }
    }
    sy.bytes += 240u * 82u * 2u;
    cv_blit(0, BAND_Y);
}
/* the band: full (a page change, the toast gone) or a live frame */
static void sp_draw_band(int full)
{
    switch (sp_band_view()) {
    case BV_GRID: if (sui.sub == 3 && sq_ed.held < 0 && !(sq.rec && !sq.run)) sq_draw_chain(); else sq_draw_grid(); break;
    case BV_OPS: sy_ops(full); break;
    case BV_ENV: sy_env(full); break;
    case BV_LFO: sy_lfo(full); break;
    default: sy_scope(full); break;
    }
    if (full)
        lcd_fill(0, BAND_Y, 4, BAND_H, R4_BG), lcd_fill(236, BAND_Y, 4, BAND_H, R4_BG);
}
/* the main loop, once a frame: a live frame when it is due (25 a second at most); the accent of a knob let go fades */
static void sp_live(uint32_t now)
{
    int v = sp_band_view();
    if (!ui.local || now - sy.at < LIVE_MS)
        return;
    sy.at = now;
    if (v == BV_SCOPE || v == BV_OPS || v == BV_ENV || v == BV_LFO)
        ui.dirty |= D_LIVE;
    if (sy_touch_k && now - sy_touch_at >= 2000u) {           /* the turned knob's accent goes */
        sy_touch_k = 0;
        ui.dirty |= D_TILE0 << 2;
    }
}
