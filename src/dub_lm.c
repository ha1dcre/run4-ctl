/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, protocol 1.4 / 1.5: DUB mode described to the host (src/lm.c, docs/PROTOCOL.md "DUB's description"). Page
 * 0 PLAY: the piece, the notes of the scale, the transport, dub hold and the actions; pages 1..19 the device's pages
 * (DUB-1's views: the seven pages, their sound / FM / pattern pages, KEY, CHORDS, PERFORM, MIX), their knobs in knob
 * order, and a few values the device sets with ALGORITHM and OCT- / OCT+ (the voices, the pings' octave) or keeps
 * behind a knob that picks (the CHORDS page's eight slots, the PATTERN page's five chances) or on its keys (the
 * PATTERN page's steps and accents: each lane's 16-bit mask as two 8-step MASKs, 1-8 and 9-16). Ids: the engine's
 * parameter numbers (dub.h DP_*), 128.. the PLAY page's and the device's own knobs, 160..179 the steps and accents
 * (160 + 4 lane: its steps 1-8, 9-16, its accents 1-8, 9-16), + 256 a second place of the same value (KEY's SWING,
 * PERFORM's FILTER, the MIX page). Part of the unity build (app.c), after dub_ui.c. */

enum { DR_PIECE = 220, DR_NOTES, DR_PLAY, DR_HOLD, DR_NEXT, DR_SAVE, DR_RESET, DR_KICK, DR_HATS, DR_SOLO,
       DR_PAT = 230 };                       /* DR_PAT + 4 lane + h: the lane's steps (h 0: 1-8, 1: 9-16), accents (2, 3) */
_Static_assert(DR_PAT + 4 * DU_NDRUM <= 256, "the pattern's refs fit a uint8_t");
#define DL_ALIAS 256u
#define DL_PAT_ID 160u
#define DL_NPAGES 20
static const char *const DL_PAGES[DL_NPAGES] = {"PLAY", "DUB", "KEY", "CHORD", "CHORD SOUND", "CHORD FM", "TEXTURE",
                                                "PING SOUND", "PING FM", "BASS", "BASS SOUND", "BASS FM", "DRUMS",
                                                "DRUM SOUND", "PATTERN", "SPACE", "GLOBAL", "CHORDS", "PERFORM", "MIX"};
static const uint8_t DL_PAGE_OF[DV_N] = {1, 3, 6, 9, 12, 15, 16, 2, 4, 5, 7, 8, 10, 11, 13, 14, 17, 18, 19};   /* DV_* */
static const char *const DL_STATE[4] = {"PIECE", "CHORD", "SCALE", "STATE"};
typedef struct { uint8_t page, ref, alias; const char *name; } dl_ent_t;   /* ref: DP_*, DK_* or DR_* */
#define CS(i, n) {17, DP_CR0 + i, 0, "ROOT " n}, {17, DP_CQ0 + i, 0, "CHORD " n}, {17, DP_CX0 + i, 0, "TYPE " n}
#define PT(d, n) {14, DR_PAT + 4 * d, 0, n " 1-8"}, {14, DR_PAT + 4 * d + 1, 0, n " 9-16"}, \
                 {14, DR_PAT + 4 * d + 2, 0, n " ACC 1-8"}, {14, DR_PAT + 4 * d + 3, 0, n " ACC 9-16"}
static const dl_ent_t DL[] = {
    {0, DR_PIECE, 0, "PIECE"}, {0, DR_NOTES, 0, "NOTES"}, {0, DR_PLAY, 0, "PLAY"}, {0, DR_HOLD, 0, "DUB HOLD"},
    {0, DR_NEXT, 0, "NEXT CHORD"}, {0, DR_SAVE, 0, "SAVE"}, {0, DR_RESET, 0, "RESET PIECE"},
    {1, DK_FILTER, 0, "FILTER"}, {1, DP_EVOLVE, 0, "EVOLVE"}, {1, DP_MOTION, 0, "MOTION"}, {1, DP_DRIFT, 0, "DRIFT"},
    {2, DP_TEMPO, 0, "TEMPO"}, {2, DP_ROOT, 0, "ROOT"}, {2, DK_MODE, 0, "MODE"}, {2, DP_SWING, 1, "SWING"},
    {3, DP_CHORD, 0, "LEVEL"}, {3, DP_STABS, 0, "STABS"}, {3, DP_DECAY, 0, "DECAY"}, {3, DP_CUTOFF, 0, "CUTOFF"},
    {3, DP_CVOICE, 0, "VOICE"},
    {4, DP_CATT, 0, "ATTACK"}, {4, DP_CCOL, 0, "COLOUR"}, {4, DP_CRES, 0, "RESO"}, {4, DP_CENV, 0, "ENV"},
    {5, DP_CALG, 0, "ALGO"}, {5, DP_CFRAT, 0, "RATIO"}, {5, DP_CFB, 0, "FEEDBACK"}, {5, DP_CFDEC, 0, "MOD DEC"},
    {6, DP_PINGS, 0, "PINGS"}, {6, DP_DENSITY, 0, "DENSITY"}, {6, DP_CLOUD, 0, "CLOUD"}, {6, DP_CRACKLE, 0, "CRACKLE"},
    {6, DP_PVOICE, 0, "VOICE"}, {6, DP_POCT, 0, "OCTAVE"},
    {7, DP_PTON, 0, "TONE"}, {7, DP_PRAT, 0, "RATIO"}, {7, DP_PLEN, 0, "LENGTH"}, {7, DP_PBEND, 0, "BEND"},
    {8, DP_PALG, 0, "ALGO"}, {8, DP_PFRAT, 0, "RATIO"}, {8, DP_PFB, 0, "FEEDBACK"}, {8, DP_PFDEC, 0, "MOD DEC"},
    {9, DP_BASS, 0, "LEVEL"}, {9, DP_BASSOCT, 0, "OCTAVE"}, {9, DP_GROOVE, 0, "GROOVE"}, {9, DP_FIFTH, 0, "FIFTH"},
    {9, DP_BVOICE, 0, "VOICE"},
    {10, DP_BBITE, 0, "BITE"}, {10, DP_BSHAPE, 0, "SHAPE"}, {10, DP_BLEN, 0, "LENGTH"}, {10, DP_BSLIDE, 0, "SLIDE"},
    {11, DP_BALG, 0, "ALGO"}, {11, DP_BFRAT, 0, "RATIO"}, {11, DP_BFB, 0, "FEEDBACK"}, {11, DP_BFDEC, 0, "MOD DEC"},
    {12, DP_DRUMS, 0, "LEVEL"}, {12, DP_HITS, 0, "HITS"}, {12, DP_SWING, 0, "SWING"}, {12, DP_DUCK, 0, "DUCK"},
    {12, DP_KIT, 0, "KIT"},
    {13, DP_KTUNE, 0, "KICK TUNE"}, {13, DP_KLEN, 0, "KICK LEN"}, {13, DP_KPUNCH, 0, "PUNCH"}, {13, DP_HTONE, 0, "HATS"},
    {14, DK_LANE, 0, "LANE"}, {14, DP_DACCENT, 0, "ACCENT"}, {14, DP_DCH0, 0, "KICK CHANCE"},
    {14, DP_DCH0 + 1, 0, "SHAKER CHANCE"}, {14, DP_DCH0 + 2, 0, "OPEN CHANCE"}, {14, DP_DCH0 + 3, 0, "RIM CHANCE"},
    {14, DP_DCH0 + 4, 0, "CLAP CHANCE"}, {14, DP_DMODE, 0, "PLAY"},
    PT(0, "KICK"), PT(1, "SHAKER"), PT(2, "OPEN"), PT(3, "RIM"), PT(4, "CLAP"),
    {15, DP_ECHO, 0, "ECHO"}, {15, DP_ETIME, 0, "ECHO TIME"}, {15, DP_FEEDBACK, 0, "FEEDBACK"}, {15, DP_REVERB, 0, "REVERB"},
    {16, DP_TUNE, 0, "TUNE"}, {16, DP_MIDI, 0, "MIDI OUT"},
    {17, DK_SLOT, 0, "SLOT"}, CS(0, "1"), CS(1, "2"), CS(2, "3"), CS(3, "4"),
    CS(4, "5"), CS(5, "6"), CS(6, "7"), CS(7, "8"), {17, DP_PMODE, 0, "PLAY"},
    {18, DK_FILTER, 1, "FILTER"}, {18, DK_DUB, 0, "DUB"}, {18, DK_ENERGY, 0, "ENERGY"}, {18, DK_SPACE, 0, "SPACE"},
    {18, DR_KICK, 0, "KICK"}, {18, DR_HATS, 0, "HATS"}, {18, DR_SOLO, 0, "SOLO"},
    {19, DP_CHORD, 1, "CHORD"}, {19, DP_PINGS, 1, "PINGS"}, {19, DP_BASS, 1, "BASS"}, {19, DP_DRUMS, 1, "DRUMS"},
};
#undef CS
#undef PT
#define DL_N ((uint32_t)(sizeof DL / sizeof DL[0]))
_Static_assert(sizeof DL / sizeof DL[0] <= LM_MAX, "DUB's description fits the value shadow");

static uint16_t dl_id(const dl_ent_t *e)
{
    uint32_t r = e->ref, id = r < DP_NPARAMS ? r : r == DK_MODE ? DP_MODE : r >= DR_PAT ? DL_PAT_ID + (r - DR_PAT)
                            : r >= DR_PIECE ? 128u + (r - DR_PIECE)
                            : 140u + (r - DK_SLOT);   /* SLOT 140, ... LANE 143, FILTER 145 .. SPACE 148 */
    return (uint16_t)(id + (e->alias ? DL_ALIAS : 0u));
}
static int dl_eng(uint32_t r) { return r < DP_NPARAMS; }
static int dl_choice_n(uint32_t r)           /* the labels of a choice (0: not one) */
{
    if (dl_eng(r) && db_choice((int)r)) return du_param_info((int)r)->hi - du_param_info((int)r)->lo + 1;
    return r == DK_MODE ? DU_NMODES + 1 : r == DK_SLOT ? DU_NPROG : r == DK_LANE ? DU_NDRUM : r == DR_PIECE ? DU_NPIECES : 0;
}
static uint32_t dl_pat_mask(uint32_t r)      /* the 16 bits a pattern ref's half is of (as the lane plays: Auto's too) */
{
    uint32_t q = r - DR_PAT, d = q >> 2;
    return q & 2u ? (uint32_t)(uint16_t)DB_CUR->par[DP_DACC0 + d] : db_lane_steps((int)d);
}
static uint32_t dl_pat_get(uint32_t r) { return (dl_pat_mask(r) >> (((r - DR_PAT) & 1u) * 8u)) & 0xFFu; }
static void dl_param(uint32_t i, lm_param_t *p)
{
    const dl_ent_t *e = &DL[i < DL_N ? i : 0];
    uint32_t r = e->ref;
    int lo, hi;
    memset(p, 0, sizeof *p);
    p->id = dl_id(e);
    p->page = e->page;
    p->name = e->name;
    p->step = 1;
    p->unit = "";
    if (r >= DR_PAT) {                       /* the PATTERN page's steps and accents: eight a MASK */
        p->kind = R4P_LK_MASK;
        p->hi = 255;
        p->nlabels = 8;
        return;
    }
    if (r >= DR_PIECE) {                     /* the PLAY page's, PERFORM's mutes and solos */
        static const uint8_t K[] = {R4P_LK_ENUM, R4P_LK_MASK, R4P_LK_TOGGLE, R4P_LK_TOGGLE, R4P_LK_ACTION,
                                    R4P_LK_ACTION, R4P_LK_ACTION, R4P_LK_TOGGLE, R4P_LK_TOGGLE, R4P_LK_MASK};
        p->kind = K[r - DR_PIECE];
        p->hi = 1;
        if (r == DR_PIECE) { p->hi = DU_NPIECES - 1; p->nlabels = DU_NPIECES; }
        if (r == DR_NOTES) { p->hi = 4095; p->nlabels = 12; p->def = 0x5AD; }   /* Basic's F dorian */
        if (r == DR_PLAY) { p->flags = R4P_PF_AUTO; p->def = 1; }
        if (r == DR_PLAY || r == DR_HOLD || r == DR_KICK || r == DR_HATS) p->nlabels = 2;
        if (r == DR_KICK || r == DR_HATS) p->def = 1;
        if (r == DR_SOLO) { p->hi = 31; p->nlabels = 5; }
        return;
    }
    db_krange((int)r, &lo, &hi);
    p->lo = (int16_t)lo;
    p->hi = (int16_t)hi;
    p->def = (int16_t)(dl_eng(r) ? du_param_info((int)r)->def : r == DK_MODE ? DM_DORIAN : 0);
    if (dl_choice_n(r)) {
        p->kind = R4P_LK_ENUM;
        p->nlabels = (uint8_t)dl_choice_n(r);
    } else {
        p->kind = R4P_LK_NUMBER;
        p->flags = lo < 0 ? R4P_PF_BIPOLAR : 0;
        p->unit = r == DP_TEMPO ? "BPM" : r == DP_TUNE ? "ct" : r == DP_POCT || r == DP_BASSOCT ? "oct" : r == DP_KTUNE
                ? "st" : (r >= DP_DCH0 && r <= DP_DCH0 + 4) ? "%" : "";
    }
}
static void dl_label(uint32_t i, uint32_t k, char *t)
{
    static const char *const SOLO[5] = {"CHORD", "PINGS", "BASS", "KICK", "HATS"};
    uint32_t r = DL[i < DL_N ? i : 0].ref;
    if (r >= DR_PAT) u_int(k + 1u + ((r - DR_PAT) & 1u) * 8u, t);   /* the step: 1..8, 9..16 */
    else if (r == DR_PIECE) db_upper(t, du_piece_name((int)k), R4P_LM_STR);
    else if (r == DR_NOTES) strcpy(t, DU_NOTE_NAME[k % 12u]);
    else if (r == DR_PLAY) strcpy(t, k ? "PLAYING" : "RESTING");
    else if (r == DR_SOLO) strcpy(t, SOLO[k % 5u]);
    else if (r >= DR_PIECE) strcpy(t, k ? "ON" : "OFF");
    else if (r == DK_MODE) db_upper(t, k < DU_NMODES ? DU_MODE_NAME[k] : "Custom", R4P_LM_STR);
    else if (r == DK_SLOT) u_int(k + 1u, t);
    else if (r == DK_LANE) strcpy(t, DB_LANE[k % DU_NDRUM]);
    else db_ktext((int)r, du_param_info((int)r)->lo + (int)k, t);
}
static int dl_get(uint32_t i)
{
    uint32_t r = DL[i < DL_N ? i : 0].ref, a = 0, k;
    if (r < DR_PIECE)
        return db_kget((int)r);
    if (r >= DR_PAT)
        return (int)dl_pat_get(r);
    switch (r) {
    case DR_PIECE: return db.piece;
    case DR_NOTES:
        for (k = 0; k < 12u; k++)
            if ((DB_CUR->pool >> k) & 1u)
                a |= 1u << ((k + (uint32_t)DB_CUR->par[DP_ROOT]) % 12u);
        return (int)a;
    case DR_PLAY: return du_playing;
    case DR_HOLD: return du_frozen;
    case DR_KICK: return !(dbr.mutes & DU_M_KICK);
    case DR_HATS: return !(dbr.mutes & DU_M_HATS);
    case DR_SOLO: return (int)dbr.solo;
    default: return 0;                       /* (the actions) */
    }
}
static void dl_text(uint32_t i, int v, char *t)
{
    uint32_t r = DL[i < DL_N ? i : 0].ref, k, n = 0;
    char b[24];
    t[0] = 0;
    if (r < DR_PIECE) {
        if (r == DK_MODE || r == DK_SLOT || r == DK_LANE) dl_label(i, (uint32_t)v, t);
        else db_ktext((int)r, v, t);
        return;
    }
    if (r >= DR_PAT) {                       /* the eight steps: X a hit (an accent), - none */
        for (k = 0; k < 8u; k++)
            t[k] = ((uint32_t)v >> k) & 1u ? 'X' : '-';
        t[8] = 0;
        return;
    }
    switch (r) {
    case DR_PIECE: case DR_HOLD: case DR_KICK: case DR_HATS: dl_label(i, (uint32_t)v, t); break;
    case DR_NOTES:
        for (k = 0; k < 12u; k++) n += ((uint32_t)v >> k) & 1u;
        u_int(n, b); u_cat(b, " NOTES");
        strcpy(t, b);
        break;
    case DR_PLAY: strcpy(t, dbr.play_at && !v ? "WAKING" : v ? "PLAYING" : "RESTING"); break;
    case DR_SOLO:
        if (!v) { strcpy(t, "NONE"); break; }
        for (k = 0; k < 5u; k++) n += ((uint32_t)v >> k) & 1u;
        if (n == 1) {
            for (k = 0; k < 4u && !((uint32_t)v >> k & 1u); k++)
                continue;
            dl_label(i, k, t);
        }
        else { u_int(n, b); u_cat(b, " SOLO"); strcpy(t, b); }
        break;
    default: break;
    }
}
static int dl_set(uint32_t i, int v)
{
    uint32_t r = DL[i < DL_N ? i : 0].ref;
    if (r < DR_PIECE) {
        if (r == DP_MODE || r == DK_MODE) { if (v < DU_NMODES) db_set_mode(v); }   /* (CUSTOM: the notes as they are) */
        else if (r >= DP_CQ0 && r <= DP_CQ7) {
            db_set((int)r, v);
            if (v && !DB_CUR->par[DP_PMODE])
                db_set(DP_PMODE, 1);         /* a chord written: the slots play (as the knob) */
        } else db_kset((int)r, v);
        db_build();
        return R4P_LE_OK;
    }
    if (r >= DR_PAT) {                       /* eight steps or accents: as the keys, Auto's pattern taken over first */
        uint32_t q = r - DR_PAT, sh = (q & 1u) * 8u, p = (q & 2u ? DP_DACC0 : DP_DPAT0) + (q >> 2), m;
        db_pattern_own();
        m = (uint32_t)(uint16_t)DB_CUR->par[p];
        m = (m & ~(0xFFu << sh)) | ((uint32_t)v & 0xFFu) << sh;
        db_set((int)p, (int)(int16_t)(uint16_t)m);
        db_build();
        return R4P_LE_OK;
    }
    switch (r) {
    case DR_PIECE:
        db_piece(v);
        break;
    case DR_NOTES: {                         /* the notes (pitch classes): relative to the root inside */
        uint32_t rel = 0, k, root = (uint32_t)DB_CUR->par[DP_ROOT];
        for (k = 0; k < 12u; k++)
            if (((uint32_t)v >> ((k + root) % 12u)) & 1u)
                rel |= 1u << k;
        if (!db_pool_ok(rel))
            return R4P_LE_REFUSED;           /* the root out, or fewer than three notes */
        DB_CUR->pool = (uint16_t)rel;
        du_pool(rel);
        db_touch();
        db_build();
        break;
    }
    case DR_PLAY:                            /* PLAY / REST: a running clock's START / STOP has the transport */
        if (db_clk_run && du_ext)
            return R4P_LE_CLOCK;
        db_play(v);
        break;
    case DR_HOLD: db_hold(v); break;
    case DR_NEXT: db_next(); break;
    case DR_SAVE:
        db_save();
        sp_toast(dbr.dirty ? "NOT SAVED" : "SAVED");
        break;
    case DR_RESET: db_reset_piece(); break;
    case DR_KICK: case DR_HATS: {
        uint32_t m = r == DR_KICK ? DU_M_KICK : DU_M_HATS;
        dbr.mutes = v ? dbr.mutes & ~m : dbr.mutes | m;
        db_mutes();
        db_build();
        break;
    }
    case DR_SOLO:
        dbr.solo = (uint32_t)v & 0x1Fu;
        db_mutes();
        db_build();
        break;
    default: break;
    }
    return R4P_LE_OK;
}
static void dl_state(r4p_lstate_t *s)
{
    static const char *const SEC[DU_NSEC] = {"FULL", "BREAK", "GROOVE", "THIN"};
    char b[24];
    int m = DB_CUR->par[DP_MODE];
    s->flags = (uint8_t)((du_playing ? R4P_LS_PLAYING : 0) | (du_frozen ? R4P_LS_FROZEN : 0) | (du_ext ? R4P_LS_EXTCLOCK : 0) |
                         (dbr.play_at && !du_playing ? R4P_LS_WAKING : 0));
    s->n = 4;
    db_upper(s->text[0], du_piece_name(db.piece), R4P_LM_STR);
    b[0] = 0;
    db_chord_name(b);
    lm_str(s->text[1], b, R4P_LM_STR);
    db_upper(b, DB_CUR->pool == DU_MODE_POOL[m] ? DU_MODE_NAME[m] : "Custom", 12);
    strcpy(s->text[2], DU_NOTE_NAME[DB_CUR->par[DP_ROOT] % 12]);
    u_cat(s->text[2], " ");
    u_cat(s->text[2], b);
    if (dbr.play_at && !du_playing) strcpy(s->text[3], "WAKING");
    else if (du_frozen) strcpy(s->text[3], "DUB HOLD");
    else if (du_playing) {                   /* the section and its bar: "BREAK 3/8" */
        strcpy(s->text[3], du_ext ? "EXT " : "");
        u_cat(s->text[3], SEC[du_section % DU_NSEC]);
        u_cat(s->text[3], " ");
        u_int(du_phrase_bar + 1u, b);
        u_cat(s->text[3], b);
        u_cat(s->text[3], "/8");
    } else strcpy(s->text[3], "RESTING");
}
/* 1.5: the device's view v is description page DL_PAGE_OF[v]; its knobs the parameters there that they edit (the
 * CHORDS page: the slot picked, PATTERN: the lane picked) */
static uint16_t dl_knob_id(uint32_t page, int k)
{
    uint32_t i, r = (uint32_t)k;
    if (k == DK_NONE)
        return R4P_LVIEW_NONE;
    if (k == DK_CQ) r = DP_CQ0 + dbr.slot;
    else if (k == DK_CX) r = DP_CX0 + dbr.slot;
    else if (k == DK_LCH) r = DP_DCH0 + dbr.lane;
    for (i = 0; i < DL_N; i++)
        if (DL[i].page == page && DL[i].ref == r)
            return dl_id(&DL[i]);
    return R4P_LVIEW_NONE;
}
static void dl_view(uint8_t *page, uint16_t ids[R4P_LVIEW_MAX], uint8_t *n)
{
    uint32_t v = dbr.view % DV_N, k;
    *page = DL_PAGE_OF[v];
    for (k = 0; k < 4u; k++)
        ids[k] = dl_knob_id(*page, DV_KNOB[v][k]);
    *n = 4;
}
static int dl_show(uint32_t page)            /* LVIEW_SET: as the device's buttons would (PLAY: nothing; MIX: white 6 held) */
{
    uint32_t v;
    if (page < 1u || page >= DL_NPAGES || page == DL_PAGE_OF[dbr.view % DV_N])
        return 1;
    if (ui.setup)
        return 0;                            /* (the SETUP page is the player's: it stays open) */
    for (v = 0; v < DV_N && DL_PAGE_OF[v] != page; v++)
        ;
    if (v == DV_MIX)
        return 0;                            /* (only while white 6 is held in PERFORM: the host's tab goes back) */
    ctl_card_off();
    if (v == DV_PERF) {
        if (!dbr.perf) db_perf(1); else db_view(DV_PERF);
    } else if (v == DV_CHORDS) {
        dbr.chords_from = dbr.view;
        db_view(DV_CHORDS);
    } else {
        db_view((int)v);
    }
    return 1;
}
static const lm_desc_t DL_DESC = {MODE_DUB, "DUB", DL_NPAGES, DL_PAGES, DL_N, 4, DL_STATE,
                                  dl_param, dl_label, dl_get, dl_text, dl_set, dl_state, dl_view, dl_show};
