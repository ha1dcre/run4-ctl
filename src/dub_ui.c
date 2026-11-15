/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, DUB mode: DUB-1's generative dub techno (src/dub/dub.c, Santiago Begueria, after AMB-1 by Charles Vestal;
 * GPL-3.0-only) with RUN4 CTL's pages. It plays itself; you steer it: PRESETS the piece, the keys take notes out of
 * the scale and put them back, PLAY plays / rests, REC tapped is dub hold and held the effects grid on the white keys,
 * EDIT tapped the chord slots and held the next chord, SEL tapped PERFORM (the keys mute, solo and make the dub moves,
 * the knobs are the four master actions Filter, Dub, Energy, Space), the page buttons the layers' pages (pressed
 * again: their sound pages, then their FM or pattern pages). The controls are DUB-1 1.2's (its firmware/src/app/ui.c
 * play model); this file is new code. The store (OBJ_DUB): every piece as it was left (with its ping loops), the piece
 * playing, Tune and MIDI out. Part of the unity build, after amb_ui.c (or synth_ui.c) and before app.c's input. */
#include "dub/dub.h"

enum { DV_DUB, DV_CHORD, DV_TEXTURE, DV_BASS, DV_DRUMS, DV_SPACE, DV_GLOBAL, DV_NPAGES,
       DV_KEY = DV_NPAGES, DV_CSND, DV_CFM, DV_PSND, DV_PFM, DV_BSND, DV_BFM, DV_DSND, DV_PAT, DV_CHORDS, DV_PERF, DV_MIX,
       DV_N };
static const char *const DV_NAME[DV_N] = {"DUB", "CHORD", "TEXTURE", "BASS", "DRUMS", "SPACE", "GLOBAL", "KEY",
                                          "CHORD SOUND", "CHORD FM", "PING SOUND", "PING FM", "BASS SOUND", "BASS FM",
                                          "DRUM SOUND", "PATTERN", "CHORDS", "PERFORM", "MIX"};
#define DB_NONE 0xFFu
static const uint8_t DV_BTN[DV_NPAGES] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO};
/* the knobs' own codes, past the engine's parameters (dub.h DP_*) */
enum { DK_NONE = 200, DK_SLOT, DK_CQ, DK_CX, DK_LANE, DK_LCH, DK_FILTER, DK_DUB, DK_ENERGY, DK_SPACE, DK_MODE };
_Static_assert(DP_NPARAMS <= DK_NONE, "the knobs' own codes come after the parameters");
static const uint8_t DV_KNOB[DV_N][4] = {
    {DK_FILTER, DP_EVOLVE, DP_MOTION, DP_DRIFT}, {DP_CHORD, DP_STABS, DP_DECAY, DP_CUTOFF},
    {DP_PINGS, DP_DENSITY, DP_CLOUD, DP_CRACKLE}, {DP_BASS, DP_BASSOCT, DP_GROOVE, DP_FIFTH},
    {DP_DRUMS, DP_HITS, DP_SWING, DP_DUCK}, {DP_ECHO, DP_ETIME, DP_FEEDBACK, DP_REVERB},
    {DP_TUNE, DP_MIDI, DK_NONE, DK_NONE}, {DP_TEMPO, DP_ROOT, DK_MODE, DP_SWING},
    {DP_CATT, DP_CCOL, DP_CRES, DP_CENV}, {DP_CALG, DP_CFRAT, DP_CFB, DP_CFDEC},
    {DP_PTON, DP_PRAT, DP_PLEN, DP_PBEND}, {DP_PALG, DP_PFRAT, DP_PFB, DP_PFDEC},
    {DP_BBITE, DP_BSHAPE, DP_BLEN, DP_BSLIDE}, {DP_BALG, DP_BFRAT, DP_BFB, DP_BFDEC},
    {DP_KTUNE, DP_KLEN, DP_KPUNCH, DP_HTONE}, {DK_LANE, DP_DACCENT, DK_LCH, DP_DMODE},
    {DK_SLOT, DK_CQ, DK_CX, DP_PMODE}, {DK_FILTER, DK_DUB, DK_ENERGY, DK_SPACE},
    {DP_CHORD, DP_PINGS, DP_BASS, DP_DRUMS}};
static const char *const DV_LABEL[DV_N][4] = {
    {"FILTER", "EVOLVE", "MOTION", "DRIFT"}, {"LEVEL", "STABS", "DECAY", "CUTOFF"},
    {"PINGS", "DENSITY", "CLOUD", "CRACKLE"}, {"LEVEL", "OCTAVE", "GROOVE", "FIFTH"},
    {"LEVEL", "HITS", "SWING", "DUCK"}, {"ECHO", "ECHO TIME", "FEEDBACK", "REVERB"},
    {"TUNE", "MIDI OUT", "", ""}, {"TEMPO", "ROOT", "MODE", "SWING"},
    {"ATTACK", "COLOUR", "RESO", "ENV"}, {"ALGO", "RATIO", "FEEDBACK", "MOD DEC"},
    {"TONE", "RATIO", "LENGTH", "BEND"}, {"ALGO", "RATIO", "FEEDBACK", "MOD DEC"},
    {"BITE", "SHAPE", "LENGTH", "SLIDE"}, {"ALGO", "RATIO", "FEEDBACK", "MOD DEC"},
    {"KICK TUNE", "KICK LEN", "PUNCH", "HATS"}, {"LANE", "ACCENT", "CHANCE", "PLAY"},
    {"SLOT", "CHORD", "TYPE", "PLAY"}, {"FILTER", "DUB", "ENERGY", "SPACE"},
    {"CHORD", "PINGS", "BASS", "DRUMS"}};
static const char *const DB_LANE[DU_NDRUM] = {"KICK", "SHAKER", "OPEN", "RIM", "CLAP"};
static volatile uint8_t db_clk_run;                  /* a MIDI clock's START / CONTINUE came, no STOP since (app.c midi_pump) */

static int db_parent(int v)                          /* a sound, FM or pattern page: its layer's page; KEY: DUB */
{
    return v == DV_CSND || v == DV_CFM ? DV_CHORD : v == DV_PSND || v == DV_PFM ? DV_TEXTURE : v == DV_BSND || v == DV_BFM
         ? DV_BASS : v == DV_DSND || v == DV_PAT ? DV_DRUMS : v == DV_KEY ? DV_DUB : -1;
}
static int db_page_of(int v) { return v < DV_NPAGES ? v : db_parent(v) >= 0 ? db_parent(v) : -1; }

/* ------------------------------------------------------------- the store --- */
/* OBJ_DUB, layout 1 (2396 B, in the pool: RAM .data / .bss is the scarcer). A store of another magic, layout or size,
 * or values out of their ranges, gives the recipes. `boot`: DUB mode was the mode when the FM-1 was last left (SETUP
 * keeps SYNTH then, as for AMB, so an older firmware boots Synth mode with SETUP intact). Each piece keeps its ping
 * loops (DUB-1 1.1's), packed a step a byte. */
#define DB_MAGIC 0x42443452u                 /* "R4DB" */
#define DB_VER 1u
typedef struct { int16_t par[DP_NPARAMS]; uint16_t pool; uint8_t pings[DU_PINGS_N]; uint8_t pings_ok, rsv; } db_slot_t;
typedef struct {
    uint32_t magic;
    uint8_t ver, piece, boot, rsv0;
    int16_t tune;                            /* Tune and MIDI out belong to the player, not the piece */
    uint8_t midi, rsv1;
    db_slot_t slot[DU_NPIECES];              /* each piece as it was left; slot[piece] is the one playing */
} db_store_t;
_Static_assert(sizeof(db_store_t) == 2396u, "DUB store layout");
_Static_assert(sizeof(db_store_t) <= sizeof r4_io && sizeof(db_store_t) <= PLAT_STORE_MAX, "the DUB store fits the scratch");
static db_store_t db PLAT_POOL;
static struct {
    uint8_t view, perf, perf_from, chords_from;  /* the page; PERFORM on; the page it came from; CHORDS' too */
    uint8_t slot, lane;                      /* the CHORDS page's slot, the PATTERN page's lane */
    uint8_t dirty, want_play, reset_done;    /* changed since saved; it plays when the mode comes in; SAVE's hold done */
    uint8_t sel_down, sel_used, rec_used;    /* SEL tapped alone: PERFORM; REC's press used by the grid's keys */
    int8_t mac[DU_NMAC];                     /* the master actions, -50..50: for playing, never saved */
    uint32_t mutes, solo, moves, fx;         /* PERFORM: kick and hats muted, the solos, the moves held; the grid's */
    uint32_t edit_at, play_at, save_at, live_at, sel_at, rec_at, edit_dn, sig;
    uint32_t saves, frames, bytes;           /* host-simulator checks */
} dbr = {.want_play = 1};
#define DB_CUR (&db.slot[db.piece % DU_NPIECES])
#define DB_PLAY_MS 700u                      /* entering DUB mode, it plays after this (a mode passed through stays quiet) */
#define DB_RESET_MS 2000u                    /* SAVE held this long: the piece back to its recipe */
#define DB_SAVE_MS 4000u                     /* a change saved this long after, while it rests */
#define DB_GRID_MS 300u                      /* REC held this long: the effects grid (a shorter press: dub hold) */
#define DB_NEXT_MS 500u                      /* EDIT held this long: the next chord (a tap: the CHORDS page) */

static int db_pool_ok(uint32_t p)
{
    uint32_t n = 0, i;
    for (i = 0; i < 12u; i++)
        n += (p >> i) & 1u;
    return (p & 1u) && !(p >> 12) && n >= 3u;
}
static int db_slot_ok(const db_slot_t *s)
{
    int i;
    for (i = 0; i < DP_NPARAMS; i++) {
        const du_param_t *q = du_param_info(i);
        if (s->par[i] < q->lo || s->par[i] > q->hi)
            return 0;
    }
    return db_pool_ok(s->pool) && s->pings_ok <= 1u;
}
static void db_recipe(int i, db_slot_t *s)
{
    du_piece(i, s->par, &s->pool);
    memset(s->pings, 0, sizeof s->pings);
    s->pings_ok = 0;
    s->rsv = 0;
}
static void db_defaults(void)
{
    int i;
    memset(&db, 0, sizeof db);
    db.magic = DB_MAGIC;
    db.ver = DB_VER;
    for (i = 0; i < DU_NPIECES; i++)
        db_recipe(i, &db.slot[i]);
    db.tune = 0;
    db.midi = 0;                             /* MIDI OUT off: nothing reaches RUN4 or a DAW unasked */
}
static void db_load(void)                    /* (through the store's scratch: the object is too big for the stack) */
{
    db_store_t *t = (db_store_t *)(void *)r4_io;
    int n = plat_store_load(OBJ_DUB, t, sizeof *t), i, ok;
    ok = n == (int)sizeof *t && t->magic == DB_MAGIC && t->ver == DB_VER && t->piece < DU_NPIECES && t->boot <= 1u &&
         t->tune >= -50 && t->tune <= 50 && t->midi <= 2u;
    for (i = 0; ok && i < DU_NPIECES; i++)
        ok = db_slot_ok(&t->slot[i]);
    if (ok)
        memcpy(&db, t, sizeof db);
    else
        db_defaults();
    dbr.dirty = 0;
}
static void db_keep(void)                    /* the piece playing's ping loops, into its slot */
{
    du_pings_get(DB_CUR->pings);
    DB_CUR->pings_ok = 1;
}
static void db_save(void)
{
    if (sy_hold_store)                       /* (a USB backup streams: saved after it) */
        return;
    db_keep();
    if (plat_store_save(OBJ_DUB, &db, sizeof db) == 0)
        dbr.dirty = 0;
    dbr.saves++;
}
static int db_par(int p)
{
    if (p == DP_TUNE) return db.tune;
    if (p == DP_MIDI) return db.midi;
    return DB_CUR->par[p];
}
static void db_apply(void)                   /* the piece playing, every value, to the engine (and its loops, if kept) */
{
    int p;
    for (p = 0; p < DP_NPARAMS; p++)
        du_set(p, db_par(p));
    du_pool(DB_CUR->pool);
    du_pings(DB_CUR->pings_ok ? DB_CUR->pings : 0);
    for (p = 0; p < DU_NMAC; p++)
        du_macro(p, dbr.mac[p]);
}

/* ----------------------------------------------------------------- page --- */
static void db_touch(void)
{
    dbr.dirty = 1;
    dbr.edit_at = plat_ms();
}
static const char *db_scale_name(void)
{
    int m = DB_CUR->par[DP_MODE];
    return DB_CUR->pool == DU_MODE_POOL[m] ? DU_MODE_NAME[m] : "Custom";
}
static int db_scale_idx(void)
{
    int m = DB_CUR->par[DP_MODE];
    return DB_CUR->pool == DU_MODE_POOL[m] ? m : DU_NMODES;
}
static void db_upper(char *t, const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s[i] && i < max; i++)
        t[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    t[i] = 0;
}
static int db_choice(int p)                  /* a parameter shown as a choice (labels, one step a detent) */
{
    return p == DP_ETIME || p == DP_DRIFT || p == DP_GROOVE || p == DP_MIDI || p == DP_ROOT || p == DP_CVOICE ||
           p == DP_PVOICE || p == DP_BVOICE || p == DP_KIT || p == DP_PMODE || p == DP_DMODE || p == DP_CALG ||
           p == DP_PALG || p == DP_BALG || (p >= DP_CR0 && p <= DP_CX7);
}
/* a knob's value (k: a parameter or a DK_*), its range, its text (upper case, as the tiles and the host show it) */
static int db_kget(int k)
{
    if (k < DP_NPARAMS) return db_par(k);
    switch (k) {
    case DK_SLOT: return dbr.slot;
    case DK_CQ: return DB_CUR->par[DP_CQ0 + dbr.slot];
    case DK_CX: return DB_CUR->par[DP_CX0 + dbr.slot];
    case DK_LANE: return dbr.lane;
    case DK_LCH: return DB_CUR->par[DP_DCH0 + dbr.lane];
    case DK_FILTER: case DK_DUB: case DK_ENERGY: case DK_SPACE: return dbr.mac[k - DK_FILTER];
    case DK_MODE: return db_scale_idx();
    default: return 0;
    }
}
static void db_krange(int k, int *lo, int *hi)
{
    if (k < DP_NPARAMS) { *lo = du_param_info(k)->lo; *hi = du_param_info(k)->hi; return; }
    *lo = 0;
    *hi = k == DK_SLOT ? DU_NPROG - 1 : k == DK_CQ ? DU_NCQ - 1 : k == DK_CX ? DU_NCX - 1 : k == DK_LANE ? DU_NDRUM - 1
        : k == DK_LCH ? 100 : k == DK_MODE ? DU_NMODES : 0;
    if (k >= DK_FILTER && k <= DK_SPACE) { *lo = -50; *hi = 50; }
}
static void db_ktext(int k, int v, char *t)  /* (t: 24 bytes) */
{
    char b[24];
    b[0] = 0;
    if (k < DP_NPARAMS) {
        du_param_text(k, v, b);
        if (k == DP_TUNE) u_cat(b, " ct");
        if (k == DP_ROOT || (k >= DP_CR0 && k <= DP_CR7)) { strcpy(t, b); return; }   /* ("Eb": as the engine names them) */
    } else if (k == DK_SLOT) {
        u_int((unsigned)v + 1u, b);
    } else if (k == DK_CQ || k == DK_CX) {
        du_param_text((k == DK_CQ ? DP_CQ0 : DP_CX0) + dbr.slot, v, b);
    } else if (k == DK_LANE) {
        strcpy(b, DB_LANE[(unsigned)v < DU_NDRUM ? v : 0]);
    } else if (k == DK_LCH) {
        u_int((unsigned)v, b); u_cat(b, "%");
    } else if (k == DK_FILTER) {             /* off in the middle; SHUT (the low-pass) down, OPEN up, 0..100 */
        if (!v) strcpy(b, "OFF");
        else { strcpy(b, v < 0 ? "SHUT " : "OPEN "); u_int((unsigned)(v < 0 ? -v : v) * 2u, b + 5); }
    } else if (k >= DK_DUB && k <= DK_SPACE) {   /* 0: the piece as it is; more or less of it, -100..+100 */
        b[0] = v < 0 ? '-' : '+';
        u_int((unsigned)(v < 0 ? -v : v) * 2u, v ? b + 1 : b);
    } else if (k == DK_MODE) {
        strcpy(b, v < DU_NMODES ? DU_MODE_NAME[v] : "Custom");
    }
    db_upper(t, b, R4P_LM_STR);
}
static void db_value(int k, r4p_slot_t *s)   /* a knob's tile */
{
    int lo, hi, v = db_kget(k);
    if (k == DK_NONE) {
        s->kind = R4P_KIND_EMPTY;
        s->value = 0;
        s->text[0] = 0;
        return;
    }
    db_krange(k, &lo, &hi);
    if ((k < DP_NPARAMS && db_choice(k)) || k == DK_SLOT || k == DK_CQ || k == DK_CX || k == DK_LANE || k == DK_MODE) {
        s->kind = R4P_KIND_ENUM;
        s->value = (uint16_t)((hi - lo + 1) << 7 | (v - lo));
    } else if (lo < 0) {
        s->kind = R4P_KIND_BI;
        s->value = (uint16_t)(v < 0 ? 8192 - v * 8192 / lo : 8192 + v * 8191 / hi);
    } else {
        s->kind = R4P_KIND_UNI;
        s->value = (uint16_t)(hi > lo ? (v - lo) * R4P_V14_MAX / (hi - lo) : 0);
    }
    db_ktext(k, v, s->text);
}
static int db_fm_page(int v)                 /* an FM page for a voice that is not FM: it says so */
{
    return (v == DV_CFM && !du_voice_fm(0)) || (v == DV_BFM && !du_voice_fm(2));
}
static void db_build(void)                   /* the page into ui.page (as a host-less page: synth_ui.c draws the tiles) */
{
    r4p_page_t *g = &ui.page;
    uint32_t k, v = dbr.view % DV_N;
    memset(g, 0, sizeof *g);
    strcpy(g->title, DV_NAME[v]);
    if (db_fm_page((int)v))
        strcpy(g->title, v == DV_CFM ? "CHORD FM: NOT FM" : "BASS FM: NOT FM");
    db_upper(g->track, du_piece_name(db.piece), R4P_STR_MAX);
    g->r = SY_AR; g->g = SY_AG; g->b = SY_AB;
    g->page_count = 0;                       /* (no sub-pages: the header's strip shows the seven pages) */
    sy_lockview = 0;
    for (k = 0; k < 4u; k++) {
        strcpy(g->slot[k].name, DV_LABEL[v][k]);
        db_value(DV_KNOB[v][k], &g->slot[k]);
    }
    ui.octave = (int8_t)DB_CUR->par[DP_POCT]; /* the footer's OCT: the pings' octave here */
    ui.dirty |= D_HEAD | D_TILES | D_FOOT | D_TILE0;
}
static void db_set_mode(int m);
static void db_set(int p, int v)             /* a parameter, clamped, to the engine and the store */
{
    const du_param_t *q = du_param_info(p);
    v = v < q->lo ? q->lo : v > q->hi ? q->hi : v;
    if (p == DP_TUNE) db.tune = (int16_t)v;
    else if (p == DP_MIDI) db.midi = (uint8_t)v;
    else DB_CUR->par[p] = (int16_t)v;
    du_set(p, v);
    db_touch();
}
static void db_kset(int k, int v)            /* a knob's value: a parameter, or what the page looks at, or a master action */
{
    int lo, hi;
    if (k == DK_NONE)
        return;
    db_krange(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (k < DP_NPARAMS) {
        db_set(k, v);
    } else if (k == DK_SLOT) {
        dbr.slot = (uint8_t)v;
    } else if (k == DK_CQ || k == DK_CX) {
        db_set((k == DK_CQ ? DP_CQ0 : DP_CX0) + dbr.slot, v);
        if (k == DK_CQ && v && !DB_CUR->par[DP_PMODE])
            db_set(DP_PMODE, 1);             /* a chord written: the slots play */
    } else if (k == DK_LANE) {
        dbr.lane = (uint8_t)v;
    } else if (k == DK_LCH) {
        db_set(DP_DCH0 + dbr.lane, v);
    } else if (k >= DK_FILTER && k <= DK_SPACE) {
        dbr.mac[k - DK_FILTER] = (int8_t)v;  /* (for playing: nothing saved) */
        du_macro(k - DK_FILTER, v);
    } else if (k == DK_MODE) {
        if (v < DU_NMODES)                   /* (CUSTOM: the notes stay as they are) */
            db_set_mode(v);
    }
}
static void db_set_mode(int m)               /* a mode: its scale */
{
    m = m < 0 ? 0 : m >= DU_NMODES ? DU_NMODES - 1 : m;
    DB_CUR->par[DP_MODE] = (int16_t)m;
    DB_CUR->pool = DU_MODE_POOL[m];
    du_set(DP_MODE, m);
    du_pool(DB_CUR->pool);
    db_touch();
}
static void db_mode_step(int d)              /* ALGORITHM (or the KEY page's MODE): the next mode, from CUSTOM too */
{
    char t[24];
    int m = db_scale_idx();
    db_set_mode((m == DU_NMODES ? (d > 0 ? -1 : DU_NMODES) : m) + (d > 0 ? 1 : -1));
    db_upper(t, DU_MODE_NAME[DB_CUR->par[DP_MODE]], 20);
    sp_toast(t);
}
static void db_turn(uint32_t k, int d)
{
    int p = DV_KNOB[dbr.view % DV_N][k & 3u], lo, hi, v;
    sy_touch_k = (uint8_t)(k + 1u);
    sy_touch_at = plat_ms();
    if (p == DK_NONE)
        return;
    db_krange(p, &lo, &hi);
    if (p == DK_MODE) {
        db_mode_step(d);
    } else {
        if (hi - lo <= 11 || (p < DP_NPARAMS && db_choice(p)) || p == DK_SLOT || p == DK_LANE)
            d = d > 0 ? 1 : -1;              /* a choice: one step a detent whatever the speed */
        v = db_kget(p) + d;
        if (p >= DK_FILTER && p <= DK_SPACE) {   /* the master actions stop at the middle on the way through */
            int o = db_kget(p);
            if ((o > 0 && v < 0) || (o < 0 && v > 0))
                v = 0;
        }
        db_kset(p, v);
    }
    db_build();
}
static void db_view(int v)
{
    dbr.view = (uint8_t)(v < 0 ? 0 : v >= DV_N ? DV_N - 1 : v);
    sy_touch_k = 0;
    db_build();
}
static void db_piece(int i)                  /* the piece: as it was left (its loops too) */
{
    char t[32] = "";
    db_keep();
    db.piece = (uint8_t)((i % DU_NPIECES + DU_NPIECES) % DU_NPIECES);
    db_apply();
    du_seed(plat_ticks24());
    du_regrow();
    db_touch();
    db_upper(t, du_piece_name(db.piece), 20);
    sp_toast(t);
    db_build();
}
static void db_toggle(int pc)                /* a key (or a note in): its note out of the scale, or in */
{
    int rel = ((pc - DB_CUR->par[DP_ROOT]) % 12 + 12) % 12;
    uint32_t p = DB_CUR->pool ^ (1u << rel);
    char t[24];
    if (!rel) { sp_toast("THE ROOT STAYS"); return; }
    if (!db_pool_ok(p)) { sp_toast("THREE NOTES AT LEAST"); return; }
    DB_CUR->pool = (uint16_t)p;
    du_pool(p);
    db_touch();
    strcpy(t, (p >> rel) & 1u ? "ADDED " : "REMOVED ");
    db_upper(t + strlen(t), DU_NOTE_NAME[pc % 12], 3);
    sp_toast(t);
    db_build();
}
static void db_play(int on)
{
    dbr.play_at = 0;
    if (on && !du_playing) {
        du_seed(plat_ticks24());
        du_play(1);
        sp_toast("PLAYING");
    } else if (!on && du_playing) {
        du_play(0);
        sp_toast("RESTING");
    }
}
static void db_hold(int on)                  /* dub hold (DUB-1's REC tap; the engine's freeze) */
{
    if ((on != 0) != (du_frozen != 0)) {
        sp_toast(on ? "DUB HOLD ON" : "DUB HOLD OFF");
        du_freeze(on);
    }
}
static void db_next(void)
{
    du_next();
    sp_toast(du_playing ? "NEXT CHORD ON THE BAR" : "NEXT CHORD");
}

/* ------------------------------------------------------------ PERFORM --- */
/* SEL tapped (alone, let go within a second): the keys from F3 up in three sets, a dark key between: white 1-5 the
 * layers (chord, pings, bass: their level to 0 or 100; kick, hats: muted), black 1-5 the same layers soloed, white 6
 * held the MIX on the knobs, white 7-11 the dub moves while held (throw, open, shut, drop, echo); the knobs the master
 * actions. SEL tapped again: off (the moves let go, the mutes stay). As DUB-1 1.2's SEL */
#define DB_PERF_N 15
static const uint8_t DB_PERF_KEY[DB_PERF_N] = {0, 2, 4, 6, 7, 11, 12, 14, 16, 18, 1, 3, 5, 8, 10};
static const char *const DB_PERF_NAME[DB_PERF_N] = {"CHORD", "PINGS", "BASS", "KICK", "HATS", "THROW", "OPEN", "SHUT",
                                                    "DROP", "ECHO", "CHORD", "PINGS", "BASS", "KICK", "HATS"};
#define DB_PERF_SHIFT 9u                     /* white 6 (D4), held: the mix */
static const uint8_t DB_LEVEL[3] = {DP_CHORD, DP_PINGS, DP_BASS};
static void db_mutes(void) { du_mute(dbr.mutes | (dbr.solo ? 0x1Fu & ~dbr.solo : 0u)); }
static int db_sounds(int i)                  /* layer i (0..4) heard: its level up, not muted, not soloed out */
{
    if (dbr.solo && !(dbr.solo >> i & 1u))
        return 0;
    return i < 3 ? DB_CUR->par[DB_LEVEL[i]] > 0 : !(dbr.mutes >> i & 1u);
}
static int db_perf_on(void) { return dbr.perf; }
static void db_perf(int on)
{
    char t[24];
    dbr.perf = (uint8_t)(on != 0);
    if (on) {
        if (dbr.view != DV_MIX && dbr.view != DV_PERF)
            dbr.perf_from = dbr.view;
        db_view(DV_PERF);
    } else {
        if (dbr.moves) {                     /* the moves let go; the mutes stay */
            dbr.moves = 0;
            du_perf(0);
        }
        if (dbr.view == DV_MIX || dbr.view == DV_PERF)
            db_view(dbr.perf_from);
    }
    strcpy(t, on ? "PERFORM ON" : "PERFORM OFF");
    sp_toast(t);
}
static void db_perf_key(uint32_t key, int down)
{
    char t[24];
    int i;
    if (key == DB_PERF_SHIFT) {
        db_view(down ? DV_MIX : DV_PERF);
        return;
    }
    for (i = 0; i < DB_PERF_N && DB_PERF_KEY[i] != key; i++)
        ;
    if (i >= DB_PERF_N)
        return;
    strcpy(t, DB_PERF_NAME[i]);
    if (i >= 10) {                           /* a solo: on or off, a press each */
        uint32_t m = 1u << (i - 10);
        if (!down)
            return;
        dbr.solo ^= m;
        db_mutes();
        if (dbr.solo & m) { strcpy(t, "SOLO "); u_cat(t, DB_PERF_NAME[i]); }
        else if (dbr.solo) u_cat(t, " SOLO OFF");   /* (one let go, others still soloed) */
        else strcpy(t, "SOLO OFF");
        sp_toast(t);
    } else if (i < 3) {                      /* the chord, pings, bass: their level to 0 or 100 */
        int v;
        if (!down)
            return;
        v = DB_CUR->par[DB_LEVEL[i]] > 0 ? 0 : 100;
        db_set(DB_LEVEL[i], v);
        u_cat(t, v ? " ON" : " OFF");
        sp_toast(t);
    } else if (i < 5) {                      /* the kick, the hats (one Drums knob for both): a mute */
        if (!down)
            return;
        dbr.mutes ^= 1u << i;
        db_mutes();
        if ((dbr.mutes & (DU_M_KICK | DU_M_HATS)) == (DU_M_KICK | DU_M_HATS))
            db_set(DP_DRUMS, 0);
        else if (DB_CUR->par[DP_DRUMS] == 0)   /* (one back while the other plays: the level up again) */
            db_set(DP_DRUMS, 100);
        u_cat(t, dbr.mutes >> i & 1u ? " OFF" : " ON");
        sp_toast(t);
    } else {                                 /* a move, as long as the key is held */
        if (down) dbr.moves |= 1u << (i - 5);
        else dbr.moves &= ~(1u << (i - 5));
        du_perf(dbr.moves);
        if (down)
            sp_toast(t);
    }
    db_build();
}

/* ---------------------------------------------------- the effects grid --- */
/* REC held: the white keys from F3 up are the sixteen effects (the echo, the filter, the space, rhythm and noise in
 * four groups of four), each as long as its key is held, even once REC is let go. A tap on REC alone is dub hold */
static const uint8_t DB_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static int db_white(uint32_t key)            /* the white key's index 0..15, or -1 */
{
    int i;
    for (i = 0; i < 16; i++)
        if (DB_WHITE[i] == key)
            return i;
    return -1;
}
static int db_grid_up(uint32_t btn)
{
    return dbr.fx || ((btn >> B_REC & 1u) && plat_ms() - dbr.rec_at >= DB_GRID_MS);
}
static int db_fx_key(uint32_t key, int down) /* 1: the key was an effect's */
{
    int e = db_white(key);
    char t[24];
    if (e < 0)
        return 0;
    if (down) {
        dbr.fx |= 1u << e;
        dbr.rec_used = 1;                    /* (REC's release: no dub hold now) */
        db_upper(t, DU_FX_NAME[e], 20);
        sp_toast(t);
    } else {
        dbr.fx &= ~(1u << e);
    }
    du_fx(dbr.fx);
    return 1;
}

/* ------------------------------------------------------ chords, pattern --- */
static void db_slot_key(uint32_t key)        /* the CHORDS page: a key, the slot's root (an empty slot: minor) */
{
    char nm[16], t[24];
    db_set(DP_CR0 + dbr.slot, (int)((5u + key) % 12u));
    if (!DB_CUR->par[DP_CQ0 + dbr.slot])
        db_kset(DK_CQ, DU_CQ_MINOR);
    if (!DB_CUR->par[DP_PMODE])
        db_set(DP_PMODE, 1);
    du_chord_label(DB_CUR->par[DP_CR0 + dbr.slot], DB_CUR->par[DP_CQ0 + dbr.slot], DB_CUR->par[DP_CX0 + dbr.slot], nm);
    strcpy(t, "SLOT ");
    u_int(dbr.slot + 1u, t + 5);
    u_cat(t, ": ");
    u_cat(t, nm);
    sp_toast(t);
    db_build();
}
static uint32_t db_lane_steps(int d)
{
    return DB_CUR->par[DP_DMODE] ? (uint32_t)(uint16_t)DB_CUR->par[DP_DPAT0 + d] : du_drum_pat[d];
}
static void db_pattern_key(uint32_t key, uint32_t keys)   /* the PATTERN page: a white key, a step; a black one held: accent */
{
    int st = db_white(key), d = dbr.lane, k;
    uint32_t m, black = keys;
    char t[24];
    if (st < 0)
        return;
    for (k = 0; k < 16; k++)
        black &= ~(1u << DB_WHITE[k]);
    if (!DB_CUR->par[DP_DMODE]) {            /* the first edit: what Auto plays, taken over */
        for (k = 0; k < DU_NDRUM; k++)
            db_set(DP_DPAT0 + k, (int)(int16_t)(uint16_t)du_drum_pat[k]);
        db_set(DP_DMODE, 1);
    }
    m = (uint32_t)(uint16_t)DB_CUR->par[DP_DPAT0 + d];
    if (black) {                             /* accent it (on, too), or not */
        db_set(DP_DACC0 + d, (int)(int16_t)(uint16_t)((uint32_t)(uint16_t)DB_CUR->par[DP_DACC0 + d] ^ (1u << st)));
        db_set(DP_DPAT0 + d, (int)(int16_t)(uint16_t)(m | 1u << st));
    } else {
        db_set(DP_DPAT0 + d, (int)(int16_t)(uint16_t)(m ^ 1u << st));
    }
    strcpy(t, DB_LANE[d]);
    u_cat(t, " ");
    u_int((unsigned)st + 1u, t + strlen(t));
    u_cat(t, black ? ((uint32_t)(uint16_t)DB_CUR->par[DP_DACC0 + d] >> st & 1u ? " ACCENT" : " NO ACCENT")
                   : ((uint32_t)(uint16_t)DB_CUR->par[DP_DPAT0 + d] >> st & 1u ? " ON" : " OFF"));
    sp_toast(t);
    db_build();
}

/* ------------------------------------------------------------ the mode --- */
static void db_init(void)                    /* at boot: the engine's state (not its buffers: another mode may own them) */
{
    du_init();
    db_load();
    db_apply();
    du_regrow();
}
static void db_enter(void)                   /* (app.c mode_switch, the render held) */
{
    du_enter();                              /* the arena and the plate: DUB's now */
    du_panic();
    db_apply();
    dbr.play_at = plat_ms() + DB_PLAY_MS;
    dbr.reset_done = 1;                      /* (a SAVE still held from before does not reset) */
    dbr.sel_down = 0;
    dbr.sig = 0;
    db_build();
}
static void db_leave(void)                   /* (app.c mode_switch, the render held): stop, MIDI off, save */
{
    static int32_t none[2];
    dbr.want_play = (uint8_t)(du_playing || dbr.play_at);
    dbr.play_at = 0;
    dbr.moves = dbr.fx = 0;
    du_perf(0);
    du_fx(0);
    du_play(0);
    du_freeze(0);
    du_panic();
    du_render(none, 0, 0);                   /* (drains them now: the STOP and notes off go out) */
    if (dbr.dirty)
        db_save();
    ui.octave = ctl.octave;
}
static void db_boot_flag(int dub)            /* the boot mode's half kept here (see db_store_t) */
{
    if (db.boot != (uint8_t)dub) {
        db.boot = (uint8_t)dub;
        if (!sy_hold_store) db_save(); else dbr.dirty = 1;
    }
}

/* ------------------------------------------------------------- input --- */
static void db_button(uint32_t k)            /* a press (app.c: SEL's combinations are taken before) */
{
    int v = dbr.view, i;
    if (k == B_REC) {                        /* (acts on the release: a tap is dub hold, held the grid) */
        dbr.rec_at = plat_ms();
        dbr.rec_used = 0;
        return;
    }
    if (k == B_EDIT) {                       /* (acts on the release: a tap the CHORDS page, held the next chord) */
        dbr.edit_dn = plat_ms();
        return;
    }
    if (db_parent(v) < 0 && v < DV_NPAGES && k == DV_BTN[v] && v != DV_SPACE && v != DV_GLOBAL) {
        db_view(v == DV_DUB ? DV_KEY : v == DV_CHORD ? DV_CSND : v == DV_TEXTURE ? DV_PSND : v == DV_BASS ? DV_BSND
                : DV_DSND);              /* the layer's button again: its sound page (HOME again: KEY) */
        return;
    }
    if (db_parent(v) >= 0 && k == DV_BTN[db_parent(v)]) {   /* again: the FM (or pattern) page; then back */
        int pv = db_parent(v);
        db_view(v == DV_CSND ? DV_CFM : v == DV_PSND ? DV_PFM : v == DV_BSND ? DV_BFM : v == DV_DSND ? DV_PAT : pv);
        return;
    }
    for (i = 0; i < DV_NPAGES; i++)          /* a page's button: its page */
        if (DV_BTN[i] == k) {
            db_view(i);
            return;
        }
    switch (k) {
    case B_PLAY:
        db_play(!du_playing);
        break;
    case B_OCTDN: case B_OCTUP: {
        char t[24] = "PING OCTAVE ", u[12];
        db_set(DP_POCT, DB_CUR->par[DP_POCT] + (k == B_OCTUP ? 1 : -1));
        du_param_text(DP_POCT, DB_CUR->par[DP_POCT], u);
        u_cat(t, u);
        sp_toast(t);
        db_build();
        break;
    }
    case B_SAVE:
        dbr.reset_done = 0;
        dbr.save_at = plat_ms();
        db_save();
        sp_toast(dbr.dirty ? "NOT SAVED" : "SAVED");
        break;
    default:
        break;
    }
}
static void db_release(uint32_t k, uint32_t now)   /* a release (app.c, every release in DUB mode) */
{
    if (k == B_SEL) {                        /* SEL tapped alone: PERFORM on / off */
        if (dbr.sel_down && !dbr.sel_used && now - dbr.sel_at < HELP_MS && !ui.setup)
            db_perf(!dbr.perf);
        dbr.sel_down = 0;
    } else if (k == B_REC && dbr.rec_at) {   /* REC tapped: dub hold; held past the grid's time: nothing */
        if (!dbr.rec_used && now - dbr.rec_at < DB_GRID_MS)
            db_hold(!du_frozen);
        dbr.rec_at = 0;
        dbr.sig = 0;
        ui.dirty |= D_TILE0;
    } else if (k == B_EDIT && dbr.edit_dn) { /* EDIT: held, the next chord; tapped, the CHORDS page or back from it */
        if (now - dbr.edit_dn >= DB_NEXT_MS) {
            db_next();
        } else if (dbr.view == DV_CHORDS) {
            db_view(dbr.chords_from);
        } else {
            dbr.chords_from = dbr.view;
            db_view(DV_CHORDS);
        }
        dbr.edit_dn = 0;
    }
}
static void db_sel_press(uint32_t now)       /* SEL went down (app.c): perhaps a tap */
{
    dbr.sel_down = 1;
    dbr.sel_used = 0;
    dbr.sel_at = now;
}
static void db_keys(uint32_t keys, uint32_t btn)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int down;
        if (!((keys ^ ctl.keys) & bit))
            continue;
        ctl.keys ^= bit;
        down = (keys & bit) != 0;
        if (btn >> B_SEL & 1u)
            dbr.sel_used = 1;
        if (((btn >> B_REC & 1u) && down) || (!down && dbr.fx))
            if (db_fx_key(k, down))
                continue;                    /* REC held: the effects (and their keys let go) */
        if (btn >> B_REC & 1u)
            continue;                        /* (REC held: the other keys do nothing) */
        if (dbr.view == DV_PAT && !dbr.perf) {
            if (down) db_pattern_key(k, keys);
        } else if (dbr.view == DV_CHORDS && !dbr.perf) {
            if (down) db_slot_key(k);
        } else if (dbr.perf) {
            db_perf_key(k, down);
        } else if (down) {
            db_toggle((int)((5u + k) % 12u));   /* key 0 is F3 */
        }
    }
}
/* notes from MIDI in (channel 1) toggle as the keys do: queued by the audio context, taken here */
static volatile uint8_t db_nq[16], db_nqh, db_nqt;
static void db_note_in(uint8_t note)          /* audio context */
{
    uint8_t h = (uint8_t)((db_nqh + 1u) & 15u);
    if (h != db_nqt) {
        db_nq[db_nqh] = note;
        db_nqh = h;
    }
}
static void db_reset_piece(void)             /* the piece back to its recipe (SAVE held; protocol 1.4 RESET PIECE) */
{
    char t[32] = "RECIPE: ";
    db_recipe(db.piece, DB_CUR);
    db_apply();
    du_regrow();
    db_touch();
    db_upper(t + strlen(t), du_piece_name(db.piece), 20);
    sp_toast(t);
    db_build();
}
static int db_layer_voice(int v)             /* ALGORITHM on a layer's page: its voice (the kit for the drums) */
{
    v = db_page_of(v);
    return v == DV_CHORD ? DP_CVOICE : v == DV_TEXTURE ? DP_PVOICE : v == DV_BASS ? DP_BVOICE : v == DV_DRUMS ? DP_KIT : -1;
}
static int enc_get(int k);
static void db_frame(uint32_t keys, uint32_t btn, uint32_t now)
{
    int k;
    if ((btn >> B_SEL & 1u) && (btn & ~(1u << B_SEL)))
        dbr.sel_used = 1;
    db_keys(keys, btn);
    while (db_nqt != db_nqh) {
        db_toggle(db_nq[db_nqt] % 12);
        db_nqt = (uint8_t)((db_nqt + 1u) & 15u);
    }
    for (k = 0; k < R4P_NENC; k++) {
        int d = enc_get(k);
        if (!d)
            continue;
        if (btn >> B_SEL & 1u)
            dbr.sel_used = 1;
        if (ui.setup) { setup_enc((uint32_t)k, d); continue; }
        if (k >= 3) {
            if (!db_grid_up(btn))            /* (the grid on the screen: the knobs rest) */
                db_turn((uint32_t)k - 3u, d);
        } else if (k == 0 && (btn >> B_HOME & 1u)) {     /* HOME held + SELECT: the tempo from any page */
            char t[24] = "TEMPO ", u[12];
            db_set(DP_TEMPO, DB_CUR->par[DP_TEMPO] + d);
            u_int((unsigned)DB_CUR->par[DP_TEMPO], u); u_cat(t, u);
            sp_toast(t);
            db_build();
        } else if (k == 0) {                              /* SELECT: the pages, from the page the view belongs to */
            int v = dbr.view == DV_CHORDS ? dbr.chords_from : dbr.view;
            if (v == DV_PERF || v == DV_MIX)                  /* (from PERFORM: on from the page it came from) */
                v = dbr.perf_from;
            v = db_page_of(v) + (d > 0 ? 1 : -1);
            if (v >= 0 && v < DV_NPAGES)
                db_view(v);
        } else if (k == 1) {                              /* ALGORITHM: the layer's voice, else the scale */
            int p = db_layer_voice(dbr.view);
            if (p >= 0) {
                const du_param_t *q = du_param_info(p);
                char t[24], u[16];
                int n = q->hi - q->lo + 1, v = DB_CUR->par[p] + (d > 0 ? 1 : -1);
                v = ((v - q->lo) % n + n) % n + q->lo;
                db_set(p, v);
                du_param_text(p, v, u);
                strcpy(t, p == DP_KIT ? "KIT " : "VOICE ");
                db_upper(t + strlen(t), u, 12);
                sp_toast(t);
                db_build();
            } else {
                db_mode_step(d);
                db_build();
            }
        } else {
            db_piece((int)db.piece + (d > 0 ? 1 : -1));
        }
    }
    if ((btn >> B_SAVE & 1u) && !dbr.reset_done && now - dbr.save_at >= DB_RESET_MS) {
        dbr.reset_done = 1;                  /* held: the piece back to its recipe (not saved until the next save) */
        db_reset_piece();
        ctl_card_off();
    }
    if (dbr.play_at && (int32_t)(now - dbr.play_at) >= 0) {
        dbr.play_at = 0;
        if (dbr.want_play && !du_playing) {
            du_seed(plat_ticks24());
            du_play(1);
        }
    }
    if (dbr.dirty && !du_playing && now - dbr.edit_at > DB_SAVE_MS)   /* a flash write stops the sound: only resting */
        db_save();
    {   /* the footer: the tempo, bar.beat, playing, dub hold */
        r4p_status_t st;
        uint32_t b = du_bars;
        st.bpm10 = (uint16_t)(DB_CUR->par[DP_TEMPO] * 10);
        st.bar = (uint16_t)(b / 16u + 1u);
        st.beat = (uint8_t)((b / 4u) % 4u);
        st.flags = (uint8_t)((du_playing ? R4P_ST_PLAY : 0u) | (du_frozen ? R4P_ST_REC : 0u));
        if (memcmp(&st, &ui.st, sizeof st)) {
            ui.st = st;
            ui.dirty |= D_FOOT;
        }
    }
    if (now - dbr.live_at >= 100u) {         /* the band: ten frames a second at most, only when it changed */
        dbr.live_at = now;
        ui.dirty |= D_LIVE;
    }
    if (sy_touch_k && now - sy_touch_at >= 2000u) {
        sy_touch_k = 0;
        ui.dirty |= D_TILE0 << 2;
    }
}
static void db_lights(uint32_t *lb, uint32_t *lk, uint32_t *db_, uint32_t *dk)
{
    uint32_t i, abs_pool = 0, btn = ctl.btn;
    int pg = db_page_of(dbr.view);
    (void)db_;
    if (pg >= 0 && (dbr.view < DV_NPAGES || ctl.blink))   /* (a sound, FM or pattern page, KEY: its button blinks) */
        *lb |= 1u << DV_BTN[pg];
    if (dbr.view == DV_CHORDS) *lb |= 1u << B_EDIT;
    if (dbr.perf) *lb |= 1u << B_SEL;
    if (du_playing) *lb |= 1u << B_PLAY;
    else if (dbr.play_at) *lb |= ctl.blink ? 1u << B_PLAY : 0u;
    if (du_frozen) *lb |= 1u << B_REC;
    for (i = 0; i < 12u; i++)                 /* the scale, as pitch classes */
        if ((DB_CUR->pool >> i) & 1u)
            abs_pool |= 1u << ((i + (uint32_t)DB_CUR->par[DP_ROOT]) % 12u);
    if (db_grid_up(btn)) {                    /* the grid: the sixteen white keys, the effects held lit */
        for (i = 0; i < 16u; i++)
            *((dbr.fx >> i & 1u) ? lk : dk) |= 1u << DB_WHITE[i];
    } else if (dbr.view == DV_PAT && !dbr.perf) {   /* the pattern: the lane's steps lit, the other white keys dim */
        uint32_t p = db_lane_steps(dbr.lane);
        for (i = 0; i < 16u; i++)
            *((p >> i & 1u) ? lk : dk) |= 1u << DB_WHITE[i];
    } else if (dbr.view == DV_CHORDS && !dbr.perf) {   /* the chords: the slot's root lit, the scale dim */
        for (i = 0; i < NKEYS; i++) {
            uint32_t pc = (5u + i) % 12u;
            if (DB_CUR->par[DP_CQ0 + dbr.slot] && pc == (uint32_t)DB_CUR->par[DP_CR0 + dbr.slot]) *lk |= 1u << i;
            else if ((abs_pool >> pc) & 1u) *dk |= 1u << i;
        }
    } else if (dbr.perf) {                   /* PERFORM: the layers heard, the solos (blinking while on), the moves
                                              * (dark while held), white 6 dim (dark while held) */
        if (dbr.view != DV_MIX) *dk |= 1u << DB_PERF_SHIFT;
        for (i = 0; i < 5u; i++) {
            *(db_sounds((int)i) ? lk : dk) |= 1u << DB_PERF_KEY[i];
            if (!(dbr.moves >> i & 1u)) *lk |= 1u << DB_PERF_KEY[5 + i];
            if (dbr.solo >> i & 1u) { if (ctl.blink) *lk |= 1u << DB_PERF_KEY[10 + i]; }
            else *dk |= 1u << DB_PERF_KEY[10 + i];
        }
    } else {
        for (i = 0; i < NKEYS; i++) {
            uint32_t pc = (5u + i) % 12u;
            if ((abs_pool >> pc) & 1u)
                *(pc == (uint32_t)DB_CUR->par[DP_ROOT] % 12u || ((ctl.keys >> i) & 1u) ? lk : dk) |= 1u << i;
        }
    }
}

/* ------------------------------------------------------------- screen --- */
/* the header: the page left, the piece right (grey), the seven pages as a strip under them, a dot while unsaved */
static void db_draw_head(void)
{
    char t[R4P_STR_MAX + 1];
    int32_t w = PG->track[0] ? pwidth(&FONT_B, PG->track) : 0, i, x;
    int pg = db_page_of(dbr.view);
    cv_begin(240, 34, R4_BG);
    if (w)
        ptext(232 - w, 7, &FONT_B, PG->track, R4_LABEL);
    fit(t, PG->title, &FONT_B, 232 - w - 26);
    x = ptext(8, 7, &FONT_B, t, R4_TEXT);
    if (dbr.dirty && !ui.setup)
        cv_rect(x + 5, 15, 4, 4, sy_accent(100));
    for (i = 0; i < DV_NPAGES; i++)
        cv_rect(8 + i * 33, 28, 30, 2, !ui.setup && i == pg ? sy_accent(100) : R4_TILE);
    cv_blit(0, 0);
}
static void db_chord_name(char *nm)          /* the chord playing, its root first ("Fm7"); nm: 16 bytes */
{
    int pcs[4], n = 0, j;
    uint32_t i;
    pcs[n++] = du_chord_root % 12;
    for (i = 0; i < DU_NPAD; i++) {
        int pc = du_chord[i] % 12, dup = 0;
        for (j = 0; j < n; j++) dup |= pcs[j] == pc;
        if (!dup && n < 4) pcs[n++] = pc;
    }
    du_chord_name(pcs, n, nm);
}
static const int8_t DB_RING[12][2] = {{0, -32}, {16, -28}, {28, -16}, {32, 0}, {28, 16}, {16, 28}, {0, 32}, {-16, 28},
                                      {-28, 16}, {-32, 0}, {-28, -16}, {-16, -28}};
static int db_band_kind(void)                /* what the band shows: 1 the grid, 2 the pattern, 3 the chords, 0 the music */
{
    if (db_grid_up(ctl.btn)) return 1;
    if (dbr.view == DV_PAT) return 2;
    if (dbr.view == DV_CHORDS) return 3;
    return 0;
}
static uint32_t db_band_sig(void)
{
    uint32_t h = 2166136261u, i;
    int kind = db_band_kind();
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIX(kind); MIX(ui.setup); MIX(dbr.play_at != 0);
    MIX(du_playing | du_frozen << 1 | du_ext << 2 | du_pulse_rest << 3);
    if (kind == 1) {
        MIX(dbr.fx);
    } else if (kind == 2) {
        MIX(dbr.lane); MIX(du_drum_pos[0]); MIX(DB_CUR->par[DP_DMODE]);
        for (i = 0; i < DU_NDRUM; i++) { MIX(db_lane_steps((int)i)); MIX(DB_CUR->par[DP_DACC0 + i]); }
    } else if (kind == 3) {
        MIX(dbr.slot); MIX(du_chord_slot); MIX(du_prog_pos); MIX(DB_CUR->par[DP_PMODE]);
        for (i = 0; i < DU_NPROG; i++) { MIX(DB_CUR->par[DP_CR0 + i]); MIX(DB_CUR->par[DP_CQ0 + i]); MIX(DB_CUR->par[DP_CX0 + i]); }
    } else {
        MIX(DB_CUR->pool); MIX(DB_CUR->par[DP_ROOT]); MIX(du_section); MIX(du_phrase_bar); MIX(dbr.perf);
        MIX(dbr.mutes); MIX(dbr.solo); MIX(dbr.moves); MIX(du_drum_pos[0]);
        for (i = 0; i < DU_NPAD; i++) MIX(du_chord[i]);
        for (i = 0; i < 6u; i++) MIX(du_lane[i]);
        MIX((int)(du_pad_level * 16.0f)); MIX((int)(du_bass_level * 16.0f)); MIX((int)(du_duck * 16.0f));
        MIX((int)(du_drift * 64.0f));
        for (i = 0; i < DU_NLOOP; i++) MIX(du_loop_glow[i] > 0.4f);
        for (i = 0; i < DU_NDRUM; i++) MIX(du_drum_glow[i] > 0.4f);
    }
#undef MIX
    return h;
}
static void db_draw_grid(void)               /* REC held: the sixteen effects, four groups of four */
{
    uint32_t i;
    for (i = 0; i < DU_NFX; i++) {
        int32_t x = 2 + (int32_t)(i % 4u) * 58, y = 2 + (int32_t)(i / 4u) * 20;
        int on = (dbr.fx >> i & 1u) != 0;
        char t[16];
        db_upper(t, DU_FX_NAME[i], 12);
        cv_rect(x, y, 54, 18, on ? sy_accent(100) : R4_TILE);
        if (!(i & 3u))
            cv_rect(x, y, 2, 18, on ? sy_accent(100) : SY_MID);   /* (each group's first: its edge) */
        ptext(x + 6, y + 5, &FONT_T, t, on ? R4_BG : R4_LABEL);
    }
}
static void db_draw_pattern(void)            /* the PATTERN page: the five lanes, the edited one in the accent */
{
    uint32_t d, s;
    for (d = 0; d < DU_NDRUM; d++) {
        int32_t y = 4 + (int32_t)d * 13;
        uint32_t p = db_lane_steps((int)d), acc = (uint32_t)(uint16_t)DB_CUR->par[DP_DACC0 + d];
        int ed = d == dbr.lane;
        ptext(2, y + 1, &FONT_T, DB_LANE[d], ed ? sy_accent(100) : R4_LABEL);
        for (s = 0; s < 16u; s++) {
            int32_t x = 52 + (int32_t)s * 11;
            int hit = (p >> s & 1u) != 0, here = du_playing && du_drum_pos[0] == s;
            uint16_t c = hit ? (ed ? sy_accent(100) : SY_MID) : R4_TILE;
            cv_rect(x, y, 9, 9, here ? SY_BAR : c);
            if (here && hit) cv_rect(x + 2, y + 2, 5, 5, ed ? sy_accent(100) : SY_MID);
            if (hit && DB_CUR->par[DP_DMODE] && (acc >> s & 1u)) cv_rect(x, y - 2, 9, 1, R4_TEXT);   /* an accent */
            if (!(s & 3u)) cv_rect(x, y + 10, 1, 2, R4_EDGE);
        }
    }
    ptext(52, 72, &FONT_T, DB_CUR->par[DP_DMODE] ? "THE PATTERN PLAYS" : "AUTO: A KEY TAKES IT OVER", R4_LABEL);
}
static void db_draw_slots(void)              /* the CHORDS page: eight slots, the edited one framed, the playing one lit */
{
    uint32_t i;
    for (i = 0; i < DU_NPROG; i++) {
        int32_t x = 2 + (int32_t)(i % 4u) * 58, y = 2 + (int32_t)(i / 4u) * 36;
        int ed = i == dbr.slot, pl = DB_CUR->par[DP_PMODE] && du_chord_slot == (int8_t)i;
        char nm[16], n[4];
        du_chord_label(DB_CUR->par[DP_CR0 + i], DB_CUR->par[DP_CQ0 + i], DB_CUR->par[DP_CX0 + i], nm);
        if (ed) cv_rect(x - 1, y - 1, 56, 34, sy_accent(100));
        cv_rect(x, y, 54, 32, pl ? SY_MID : R4_TILE);
        u_int(i + 1u, n);
        ptext(x + 4, y + 3, &FONT_T, n, ed ? sy_accent(100) : R4_LABEL);
        ptext(x + 4, y + 14, &FONT_B, nm, DB_CUR->par[DP_CQ0 + i] ? R4_TEXT : R4_DIM);
    }
    ptext(2, 74, &FONT_T, DB_CUR->par[DP_PMODE] ? "THE SLOTS PLAY IN TURN" : "PLAY: RANDOM (THE SCALE'S CHORDS)", R4_LABEL);
}
static void db_draw_music(void)              /* the ring and the chord, the phrase, the bar's lanes, the levels */
{
    static const char *const SEC[DU_NSEC] = {"FULL", "BREAK", "GROOVE", "THIN"};
    uint32_t i, k, abs_pool = 0, chord = 0;
    int32_t cx = 40, cy = 41, x, y;
    char nm[16];
    const char *state;
    float lv[4];
    for (i = 0; i < 12u; i++)
        if ((DB_CUR->pool >> i) & 1u)
            abs_pool |= 1u << ((i + (uint32_t)DB_CUR->par[DP_ROOT]) % 12u);
    for (i = 0; i < DU_NPAD; i++)
        chord |= 1u << (du_chord[i] % 12u);
    for (i = 0; i < 12u; i++) {               /* the ring */
        int in = (abs_pool >> i) & 1u, ch = (chord >> i) & 1u;
        x = cx + DB_RING[i][0];
        y = cy + DB_RING[i][1];
        if (i == (uint32_t)DB_CUR->par[DP_ROOT] % 12u) {
            cv_rect(x - 5, y - 5, 10, 10, R4_TEXT);
            cv_rect(x - 4, y - 4, 8, 8, R4_BG);
        }
        if (ch && in) cv_rect(x - 3, y - 3, 6, 6, sy_accent(100));
        else if (in) cv_rect(x - 2, y - 2, 4, 4, SY_MID);
        else cv_rect(x - 1, y - 1, 2, 2, R4_EDGE);
    }
    db_chord_name(nm);
    ptext(cx - pwidth(&FONT_B, nm) / 2, cy - 14, &FONT_B, nm, R4_TEXT);
    state = dbr.play_at ? "WAKING" : du_frozen ? "DUB HOLD" : du_ext && du_playing ? "EXT CLOCK" : du_playing
          ? SEC[du_section % DU_NSEC] : "RESTING";
    ptext(cx - pwidth(&FONT_T, state) / 2, cy + 6, &FONT_T, state, du_playing && !du_frozen ? R4_LABEL : sy_accent(100));
    for (i = 0; i < 8u; i++) {                /* the phrase: its eight bars */
        uint16_t c = !du_playing ? R4_TILE : i == du_phrase_bar ? sy_accent(100) : i < du_phrase_bar ? SY_MID : R4_EDGE;
        cv_rect(86 + (int32_t)i * 18, 2, 16, 4, c);
    }
    for (i = 0; i < 6u; i++) {                /* the bar as it plays: the stabs, the kick, the hats, the rim, the clap, the bass */
        uint32_t p = du_lane[i];
        int muted = dbr.perf && ((i == 0 && !db_sounds(0)) || (i == 1 && !db_sounds(3)) || ((i == 2 || i == 3) && !db_sounds(4)) ||
                                 (i == 5 && !db_sounds(2)));
        y = 10 + (int32_t)i * 7;
        for (k = 0; k < 16u; k++) {
            int hit = (p >> k & 1u) != 0, here = du_playing && du_drum_pos[0] == k;
            x = 86 + (int32_t)k * 9;
            if (here) cv_rect(x, y, 7, 5, hit ? sy_accent(100) : SY_BAR);
            else if (hit) cv_rect(x, y, 7, 5, muted ? R4_EDGE : SY_MID);
            else cv_rect(x + 3, y + 2, 1, 1, R4_EDGE);
        }
    }
    lv[0] = du_pad_level; lv[1] = 0; lv[2] = du_bass_level; lv[3] = 0;
    for (i = 0; i < DU_NLOOP; i++) if (du_loop_glow[i] > lv[1]) lv[1] = du_loop_glow[i];
    for (i = 0; i < DU_NDRUM; i++) if (du_drum_glow[i] > lv[3]) lv[3] = du_drum_glow[i];
    for (i = 0; i < 4u; i++) {                /* the layers' levels (chord, pings, bass, drums), the duck */
        int32_t w = (int32_t)(lv[i] * 60.0f);
        y = 54 + (int32_t)i * 5;
        w = w < 0 ? 0 : w > 60 ? 60 : w;
        cv_rect(86, y, 60, 3, R4_TILE);
        if (w) cv_rect(86, y, w, 3, SY_BAR);
    }
    {
        int32_t w = (int32_t)(du_duck * 60.0f);
        w = w < 0 ? 0 : w > 60 ? 60 : w;
        cv_rect(86, 75, 60, 2, R4_TILE);
        if (w) cv_rect(146 - w, 75, w, 2, sy_accent(100));   /* (the duck: gain taken, from the right) */
    }
    if (dbr.perf) {                           /* PERFORM: the moves held, or the word */
        char t[24] = "";
        for (i = 0; i < 5u; i++)
            if (dbr.moves >> i & 1u) { strcpy(t, DB_PERF_NAME[5 + i]); break; }
        if (dbr.solo) { if (!t[0]) strcpy(t, "SOLO"); }
        ptext(154, 56, &FONT_T, "PERFORM", sy_accent(100));
        if (t[0]) ptext(154, 68, &FONT_T, t, R4_TEXT);
    } else if (dbr.mutes) {
        ptext(154, 56, &FONT_T, dbr.mutes == (DU_M_KICK | DU_M_HATS) ? "DRUMS OFF" : dbr.mutes & DU_M_KICK ? "KICK OFF"
              : "HATS OFF", R4_LABEL);
    }
    x = (int32_t)(du_drift * 146.0f);         /* to the next chord */
    cv_rect(86, 80, 146, 1, R4_TILE);
    if (!du_frozen && x > 0) cv_rect(86, 80, x > 146 ? 146 : x, 1, SY_MID);
}
static void db_draw_band(int full)
{
    uint32_t sig = db_band_sig();
    int kind;
    if (!full && sig == dbr.sig)
        return;
    dbr.sig = sig;
    kind = db_band_kind();
    cv_begin(232, 82, R4_BG);
    if (kind == 1) db_draw_grid();
    else if (kind == 2) db_draw_pattern();
    else if (kind == 3) db_draw_slots();
    else db_draw_music();
    dbr.bytes += 232u * 82u * 2u;
    dbr.frames++;
    cv_blit(4, BAND_Y);
    if (full)
        lcd_fill(0, BAND_Y, 4, BAND_H, R4_BG), lcd_fill(236, BAND_Y, 4, BAND_H, R4_BG);
}
