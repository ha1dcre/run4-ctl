/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the step sequencer. 16 patterns of up to 64 steps; a step holds up to four notes (a chord), is
 * a NOTE or a TIE (the notes before it go on sounding), and has its own velocity, gate and chance. Patterns have their
 * own length, rate (1/8, 1/16, 1/16T, 1/32), swing, gate and velocity. Live recording (REC while playing) puts what is
 * played into the nearest step, chords add up (overdub), and a note held over the next steps writes TIEs; step
 * recording (REC while stopped) writes the keys pressed together into the cursor step and moves on when they are let go.
 *
 * The step model (notes[4] + n, NOTE / TIE, velocity, chance), live recording (rec_note / rec_hold / rec_release:
 * nearest step, overdub, held notes as TIEs, an early release puts the last TIE back, no double trigger of the note
 * recorded ahead) and step entry (a chord into the cursor step, letting go moves on) follow Felucca's seq.c and
 * ui_input.c (seq_entry), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments, GPL-3.0-only
 * (https://github.com/hugelton/Felucca), adapted to one track and this firmware; see NOTICE. The rest is new.
 *
 * Plain C, integer only. Included by synth.c (the unity build and tests/synth_test.c). Two contexts, as the rest of
 * synth.c: the audio context runs the clock (sq_clock, from syn_render) and live recording (from syn_apply_event); the
 * main loop edits the patterns (sq_ed_*) and sets the transport through the event ring. Byte-sized fields only are
 * shared, so a step being edited while it plays sounds either old or new.
 *
 * Parameter locks (Elektron's idea): a step can lock some of the sound's parameters (synth.c LK_*) to its own value.
 * Sparse: each pattern has SQ_LANES lanes, a lane is one parameter and its value on each step (none = SQ_NOLOCK); a
 * lane is taken by the first lock of a parameter and freed with its last. A step that plays notes puts its locks on
 * (every other parameter back to its base value); a TIE step adds its own on top (the note goes on, the sound changes);
 * a step that plays nothing (off, a rest, chance said no) changes nothing; stopping takes them all off.
 * The chain (the song): up to SQ_CHAIN entries, a pattern and how often it plays; with the song on the sequencer plays
 * them in order from the first, each to its end as a queued pattern change does, and starts over after the last. */

#define SQ_STEPS 64
#define SQ_PATS 16
#define SQ_NOTES 4
#define SQ_PAGE 16                           /* steps per step page (the 16 white keys) */
#define SQ_PPQN 96                           /* the clock's ticks per quarter note (a MIDI clock is 4 of them) */
#define SQ_ON 0x80u                          /* step flags: it plays */
#define SQ_TIE 0x40u                         /* ... it is a TIE: what sounds goes on */
#define SQ_NMASK 0x07u                       /* ... the notes in use (0..4) */
enum { SQR_8, SQR_16, SQR_16T, SQR_32, SQR_N };
/* the clock: AUTO follows a MIDI clock coming in (and its start / stop) while one comes, else the tempo; OUT plays the
 * tempo and sends MIDI clock; INT plays the tempo and ignores MIDI clock */
enum { SQC_AUTO, SQC_OUT, SQC_INT, SQC_N };
#define SQ_EXT_LOST 22050u                   /* no F8 for half a second: the clock is gone, the tempo takes over */
static const uint8_t SQ_RATE_TICKS[SQR_N] = {48, 24, 16, 12};

typedef struct {
    uint8_t note[SQ_NOTES];
    uint8_t f;                               /* SQ_ON | SQ_TIE | n */
    uint8_t vel;                             /* 0 = the pattern's, 1..127 */
    uint8_t gate;                            /* 0 = the pattern's, 5..100 % of the step */
    uint8_t chance;                          /* 0 = always, 1..99 % */
} sq_step_t;
typedef struct {
    sq_step_t s[SQ_STEPS];
    uint8_t len;                             /* 1..64 */
    uint8_t rate;                            /* SQR_* */
    uint8_t swing;                           /* 50..75 %: the first step of each pair gets that much of the pair */
    uint8_t gate;                            /* 5..100 % */
    uint8_t vel;                             /* 1..127 */
    uint8_t rsv[3];
} sq_pat_t;
_Static_assert(sizeof(sq_step_t) == 8u && sizeof(sq_pat_t) == 520u, "sequencer store layout");
static sq_pat_t sq_pat[SQ_PATS];
#define SQ_LANES 6                           /* lock lanes a pattern: six parameters locked on any of its steps */
#define SQ_NOLOCK 0xFFu
typedef struct {
    uint8_t id[SQ_LANES];                    /* the lane's parameter (LK_*), SQ_NOLOCK = a free lane */
    uint8_t rsv[2];
    uint8_t v[SQ_LANES][SQ_STEPS];           /* per step: the value - LK_LO[id], or SQ_NOLOCK */
} sq_lanes_t;
static sq_lanes_t sq_lk[SQ_PATS];
#define SQ_CHAIN 32
typedef struct { uint8_t len, rsv; uint8_t pat[SQ_CHAIN], rep[SQ_CHAIN]; } sq_chain_t;   /* rep 1..16 */
static sq_chain_t sq_ch;

/* stored in four objects of four patterns (app.h OBJ_SEQ_A..D). Version 1: 8 + 4 x 520 = 2088 bytes; version 2 adds the
 * patterns' lock lanes (4 x 392) and the chain (66, used in object 0): 3724 bytes each (PLAT_STORE_MAX 3840), 14896 in
 * all, each still in its own pair of 4 KiB sectors. A version 1 object loads as it was, without locks and chain. */
#define SQ_OBJ_PATS 4u
#define SQ_OBJS (SQ_PATS / SQ_OBJ_PATS)
#define SQ_MAGIC 0x54505153u                 /* "SQPT" */
#define SQ_VER 2u
#define SQ_OBJ_V1_SIZE 2088u
typedef struct {
    uint32_t magic; uint8_t ver, first, rsv[2];
    sq_pat_t p[SQ_OBJ_PATS];                 /* (version 1 ends here) */
    sq_lanes_t lk[SQ_OBJ_PATS];
    sq_chain_t chain;
    uint8_t pad[2];
} sq_obj_t;
_Static_assert(sizeof(sq_lanes_t) == 392u && sizeof(sq_chain_t) == 66u && offsetof(sq_obj_t, lk) == SQ_OBJ_V1_SIZE &&
               sizeof(sq_obj_t) == 3724u, "sequencer object size");
static uint8_t sq_dirty;                     /* bit o: object o changed since saved (the chain: object 0) */

static uint32_t sq_n(const sq_step_t *s) { return s->f & SQ_NMASK; }
static int sq_is_tie(const sq_step_t *s) { return (s->f & SQ_TIE) != 0; }
static int sq_is_on(const sq_step_t *s) { return (s->f & SQ_ON) != 0; }
static int sq_has(const sq_step_t *s) { return sq_is_tie(s) || sq_n(s) > 0u; }       /* holds something */
static int sq_plays(const sq_step_t *s) { return sq_is_on(s) && sq_has(s); }
static void sq_set_n(sq_step_t *s, uint32_t n) { s->f = (uint8_t)((s->f & ~SQ_NMASK) | (n > SQ_NOTES ? SQ_NOTES : n)); }
static void sq_touch(uint32_t pat) { sq_dirty = (uint8_t)(sq_dirty | 1u << (pat / SQ_OBJ_PATS)); }

static void sq_lanes_init(sq_lanes_t *l) { memset(l, SQ_NOLOCK, sizeof *l); }
static void sq_pat_init(sq_pat_t *p)
{
    memset(p, 0, sizeof *p);
    p->len = 16;
    p->rate = SQR_16;
    p->swing = 50;
    p->gate = 60;
    p->vel = 100;
}
static void sq_pat_sanitize(sq_pat_t *p)
{
    uint32_t i, k;
    if (p->len < 1u || p->len > SQ_STEPS) p->len = 16;
    if (p->rate >= SQR_N) p->rate = SQR_16;
    if (p->swing < 50u || p->swing > 75u) p->swing = 50;
    if (p->gate < 5u || p->gate > 100u) p->gate = 60;
    if (p->vel < 1u || p->vel > 127u) p->vel = 100;
    for (i = 0; i < SQ_STEPS; i++) {
        sq_step_t *s = &p->s[i];
        s->f &= (uint8_t)(SQ_ON | SQ_TIE | SQ_NMASK);
        if (sq_n(s) > SQ_NOTES) sq_set_n(s, SQ_NOTES);
        if (sq_is_tie(s)) sq_set_n(s, 0);
        for (k = 0; k < SQ_NOTES; k++) s->note[k] &= 0x7Fu;
        s->vel &= 0x7Fu;
        if (s->gate > 100u) s->gate = 100;
        if (s->chance > 99u) s->chance = 0;
    }
}
static void sq_defaults(void)
{
    static const uint8_t PAT[16] = {36, 0, 48, 0, 36, 0, 51, 48, 36, 0, 48, 0, 34, 0, 46, 43};    /* pattern 1: a bass line */
    uint32_t i;
    for (i = 0; i < SQ_PATS; i++) {
        sq_pat_init(&sq_pat[i]);
        sq_lanes_init(&sq_lk[i]);
    }
    memset(&sq_ch, 0, sizeof sq_ch);
    for (i = 0; i < 16u; i++)
        if (PAT[i]) {
            sq_pat[0].s[i].note[0] = PAT[i];
            sq_pat[0].s[i].f = SQ_ON | 1u;
        }
    sq_dirty = 0;
}
/* object o <-> the patterns (o * 4 ..): what plat_store_save writes, what plat_store_load gave (0 = taken) */
static void sq_obj_fill(uint32_t o, sq_obj_t *b)
{
    memset(b, 0, sizeof *b);
    b->magic = SQ_MAGIC;
    b->ver = SQ_VER;
    b->first = (uint8_t)(o * SQ_OBJ_PATS);
    memcpy(b->p, &sq_pat[o * SQ_OBJ_PATS], sizeof b->p);
    memcpy(b->lk, &sq_lk[o * SQ_OBJ_PATS], sizeof b->lk);
    b->chain = sq_ch;
}
static void sq_lanes_sanitize(sq_lanes_t *l)
{
    uint32_t i, k;
    for (i = 0; i < SQ_LANES; i++) {
        if (l->id[i] >= LK_N) {
            l->id[i] = SQ_NOLOCK;
            memset(l->v[i], SQ_NOLOCK, SQ_STEPS);
        }
        for (k = 0; k < SQ_STEPS && l->id[i] != SQ_NOLOCK; k++)
            if (l->v[i][k] != SQ_NOLOCK && (int32_t)l->v[i][k] > LK_HI[l->id[i]] - LK_LO[l->id[i]])
                l->v[i][k] = SQ_NOLOCK;
    }
}
/* what plat_store_load gave (len bytes): a version 2 object, or a version 1 one (no locks, no chain: migrated) */
static int sq_obj_take(uint32_t o, const sq_obj_t *b, uint32_t len)
{
    uint32_t i;
    int v1 = len == SQ_OBJ_V1_SIZE && b->ver == 1u;
    if (o >= SQ_OBJS || b->magic != SQ_MAGIC || b->first != o * SQ_OBJ_PATS || !(v1 || (len == sizeof *b && b->ver == SQ_VER)))
        return -1;
    for (i = 0; i < SQ_OBJ_PATS; i++) {
        uint32_t pi = o * SQ_OBJ_PATS + i;
        sq_pat[pi] = b->p[i];
        sq_pat_sanitize(&sq_pat[pi]);
        if (v1) {
            sq_lanes_init(&sq_lk[pi]);
        } else {
            sq_lk[pi] = b->lk[i];
            sq_lanes_sanitize(&sq_lk[pi]);
        }
    }
    if (o == 0u && !v1) {
        sq_ch = b->chain;
        if (sq_ch.len > SQ_CHAIN) sq_ch.len = 0;
        for (i = 0; i < SQ_CHAIN; i++) {
            sq_ch.pat[i] %= SQ_PATS;
            if (sq_ch.rep[i] < 1u || sq_ch.rep[i] > 16u) sq_ch.rep[i] = 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ locks --- */
static int sq_lane_of(const sq_lanes_t *l, uint32_t id)
{
    int i;
    for (i = 0; i < SQ_LANES; i++)
        if (l->id[i] == id)
            return i;
    return -1;
}
static int32_t sq_lock_get(uint32_t pat, uint32_t step, uint32_t id)     /* the locked value, or -1000 = none */
{
    const sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    int i = sq_lane_of(l, id);
    if (i < 0 || l->v[i][step % SQ_STEPS] == SQ_NOLOCK)
        return -1000;
    return (int32_t)l->v[i][step % SQ_STEPS] + LK_LO[id];
}
static void sq_lock_del(uint32_t pat, uint32_t step, uint32_t id)
{
    sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    int i = sq_lane_of(l, id);
    uint32_t k;
    if (i < 0)
        return;
    l->v[i][step % SQ_STEPS] = SQ_NOLOCK;
    for (k = 0; k < SQ_STEPS && l->v[i][k] == SQ_NOLOCK; k++)
        ;
    if (k == SQ_STEPS)
        l->id[i] = SQ_NOLOCK;                /* its last lock: the lane is free */
    sq_touch(pat);
}
static int sq_lock_set(uint32_t pat, uint32_t step, uint32_t id, int32_t val)   /* 0 ok, -1 no lane free */
{
    sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    int i = sq_lane_of(l, id);
    if (id >= LK_N)
        return -1;
    if (i < 0) {
        for (i = 0; i < SQ_LANES && l->id[i] != SQ_NOLOCK; i++)
            ;
        if (i == SQ_LANES)
            return -1;
        l->id[i] = (uint8_t)id;
        memset(l->v[i], SQ_NOLOCK, SQ_STEPS);
    }
    val = val < LK_LO[id] ? LK_LO[id] : val > LK_HI[id] ? LK_HI[id] : val;
    l->v[i][step % SQ_STEPS] = (uint8_t)(val - LK_LO[id]);
    sq_touch(pat);
    return 0;
}
static uint32_t sq_step_locks(uint32_t pat, uint32_t step)  /* the parameters locked on a step, a bit each */
{
    const sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    uint32_t i, m = 0;
    for (i = 0; i < SQ_LANES; i++)
        if (l->id[i] != SQ_NOLOCK && l->v[i][step % SQ_STEPS] != SQ_NOLOCK)
            m |= 1u << l->id[i];
    return m;
}
static void sq_lock_clear_step(uint32_t pat, uint32_t step)
{
    uint32_t id, m = sq_step_locks(pat, step);
    for (id = 0; id < LK_N; id++)
        if ((m >> id) & 1u)
            sq_lock_del(pat, step, id);
}
static uint32_t sq_lanes_used(uint32_t pat)
{
    uint32_t i, n = 0;
    for (i = 0; i < SQ_LANES; i++)
        n += sq_lk[pat % SQ_PATS].id[i] != SQ_NOLOCK;
    return n;
}
/* step idx of pattern pat sounds (audio context): its locks on; tie: on top of what is on */
static void sq_lock_fire(uint32_t pat, uint32_t idx, int tie)
{
    const sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    uint32_t m = tie ? syn_lk.mask : 0u, i;
    int16_t v[LK_N];
    memcpy(v, syn_lk.v, sizeof v);
    for (i = 0; i < SQ_LANES; i++)
        if (l->id[i] != SQ_NOLOCK && l->v[i][idx % SQ_STEPS] != SQ_NOLOCK) {
            m |= 1u << l->id[i];
            v[l->id[i]] = (int16_t)(l->v[i][idx % SQ_STEPS] + LK_LO[l->id[i]]);
        }
    for (i = 0; i < LK_N; i++)
        if (((m ^ syn_lk.mask) >> i & 1u) || ((m >> i & 1u) && v[i] != syn_lk.v[i]))
            break;
    if (i == LK_N)
        return;                              /* (nothing changes: no resync) */
    memcpy(syn_lk.v, v, sizeof v);
    syn_lk.mask = m;
    syn_dirty = 1;
}
static void sq_lock_off(void)
{
    if (syn_lk.mask) {
        syn_lk.mask = 0;
        syn_dirty = 1;
    }
}

/* ----------------------------------------------------- the transport --- */
static struct {
    uint8_t run;                             /* the transport */
    uint8_t rec;                             /* REC armed (the main loop's): live recording while it runs */
    uint8_t pat;                             /* the pattern playing (the selected one, syn.seq_pat, from the next loop on) */
    uint8_t step;                            /* the step playing */
    uint16_t pos;                            /* ticks since step 0 of this loop */
    uint16_t nstart;                         /* the tick the next step starts at */
    uint32_t ticks;                          /* ticks since the start (bar / beat) */
    uint32_t loops;                          /* loops since the start */
    uint32_t ph;                             /* the internal clock's phase (samples x bpm10 x PPQN) */
    uint32_t elapsed, len_smp;               /* samples into the playing step; its length */
    uint32_t spt_q8;                         /* samples per tick, Q8 (internal: from the tempo; MIDI in: measured) */
    uint32_t off_at;                         /* the sounding notes stop after this many samples of the step */
    uint8_t notes[SQ_NOTES], n_notes;        /* what the sequencer has sounding (its note-offs) */
    uint8_t hold;                            /* the next step is a TIE: keep them over the gate */
    uint8_t gate_org;                        /* the gate of the step they started on (a TIE uses it) */
    /* live recording (rec_note / rec_hold / rec_release, Felucca) */
    uint8_t rh_n, rh_note[SQ_NOTES];         /* recorded notes still held */
    uint8_t rh_start, rh_ties, rh_last;      /* the step they went into; TIEs written after it; the last of them */
    sq_step_t rh_bak;                        /* what the last TIE replaced (an early release puts it back) */
    uint8_t rskip_idx, rskip_n, rskip[SQ_NOTES];   /* notes recorded ahead into the next step: sounding already */
    /* MIDI clock */
    uint32_t smp;                            /* samples since boot (the render's) */
    uint32_t ext_last, ext_dt;               /* the last F8's sample time; the smoothed distance between two */
    uint8_t ext_ticks;                       /* ticks owed by F8s not taken yet */
    uint8_t out_div;                         /* ticks to the next F8 out */
    uint32_t played;                         /* steps that sounded (tests, the simulator) */
    /* the song */
    uint8_t ci;                              /* the chain entry playing */
    uint8_t rep_n;                           /* its loops played so far */
    uint8_t in_song;                         /* the song plays (set at a start or a loop end) */
} sq;
static void (*sq_midi_out)(uint8_t rt);      /* the app's: a MIDI real-time byte out (F8, FA, FC); 0 = none */

static uint32_t sq_clock_mode(void);
static int sq_song(void);                    /* the song is on (synth.c: syn.song) */
static int32_t sq_transpose(void);
static uint32_t sq_sel_pat(void);
static uint32_t sq_bpm10(void);

static uint32_t sq_step_ticks(const sq_pat_t *p) { return SQ_RATE_TICKS[p->rate < SQR_N ? p->rate : SQR_16]; }
static uint32_t sq_start_tick(const sq_pat_t *p, uint32_t i)      /* where step i starts in the loop: odd steps swing late */
{
    uint32_t l = sq_step_ticks(p), len = p->len < 1u ? 1u : p->len;
    if (i >= len)
        return l * len;
    return i * l + ((i & 1u) ? l * 2u * (uint32_t)(p->swing < 50u ? 0u : p->swing - 50u) / 100u : 0u);
}
static uint32_t sq_spt_internal(void)        /* samples per tick, Q8, at the tempo */
{
    uint32_t b = sq_bpm10();
    b = b < 400u ? 400u : b > 2400u ? 2400u : b;
    return (uint32_t)(44100ull * 600ull * 256ull / ((uint64_t)b * SQ_PPQN));
}
static int sq_ext(void)                      /* a MIDI clock is coming in and the sequencer follows it */
{
    return sq_clock_mode() == SQC_AUTO && sq.ext_last && sq.smp - sq.ext_last < SQ_EXT_LOST;
}
static uint32_t sq_spt(void) { return sq_ext() && sq.ext_dt ? sq.ext_dt * 256u / 4u : sq_spt_internal(); }

static void syn_note_on_now(uint8_t note, uint8_t vel);
static void syn_note_off_now(uint8_t note);
static void sq_release(void)                 /* the sequencer's notes off */
{
    uint32_t i;
    for (i = 0; i < sq.n_notes; i++)
        syn_note_off_now(sq.notes[i]);
    sq.n_notes = 0;
    sq.hold = 0;
}
static uint32_t sq_rand(void)
{
    static uint32_t r = 0x2545F491u;
    r = r * 1664525u + 1013904223u;
    return r >> 16;
}

/* the sequencer enters step idx: a recorded note still held ties into it (Felucca's rec_hold) */
static void sq_rec_hold(sq_pat_t *p, uint32_t idx)
{
    sq_step_t *s;
    if (!sq.rh_n)
        return;
    if (!sq.rec || sq.rh_ties + 1u >= p->len) {
        sq.rh_n = 0;                         /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == sq.rh_start)
        return;                              /* (recorded ahead into the step now starting) */
    s = &p->s[idx];
    sq.rh_bak = *s;
    sq.rh_last = (uint8_t)idx;
    sq.rh_ties++;
    memset(s->note, 0, sizeof s->note);
    s->f = SQ_ON | SQ_TIE;
    s->vel = 0;
    sq_touch(sq.pat);
}

/* step idx starts: its notes on (or a TIE goes on, or a rest ends what sounds) */
static void sq_fire(uint32_t idx)
{
    sq_pat_t *p = &sq_pat[sq.pat];
    const sq_step_t *s;
    uint32_t len = p->len < 1u ? 1u : p->len, i, j, gate;
    int32_t trn = sq_transpose();
    int next_tie;
    sq.step = (uint8_t)idx;
    sq.pos = (uint16_t)sq_start_tick(p, idx);   /* (on time, or realigned after a rate or swing change) */
    sq.elapsed = 0;
    sq.nstart = (uint16_t)sq_start_tick(p, idx + 1u);
    sq.len_smp = (uint32_t)(((uint64_t)(sq.nstart - sq_start_tick(p, idx)) * sq_spt()) >> 8);
    sq_rec_hold(p, idx);
    s = &p->s[idx];
    next_tie = sq_is_on(&p->s[(idx + 1u) % len]) && sq_is_tie(&p->s[(idx + 1u) % len]) && len > 1u;
    if (sq_is_on(s) && sq_is_tie(s)) {      /* a TIE: what sounds goes on to its gate (or over, into another TIE) */
        if (sq.n_notes) {
            sq.off_at = sq.len_smp * sq.gate_org / 100u;
            sq.hold = (uint8_t)next_tie;
            sq_lock_fire(sq.pat, idx, 1);    /* its locks on top: the held note's sound changes */
        }
        return;
    }
    sq_release();
    if (!sq_plays(s) || (s->chance && sq_rand() % 100u >= s->chance)) {
        sq.rskip_n = 0;
        return;
    }
    gate = s->gate ? s->gate : p->gate;
    sq_lock_fire(sq.pat, idx, 0);            /* (before the notes start: they start with the locked sound) */
    for (i = 0; i < sq_n(s); i++) {
        int32_t nt = (int32_t)s->note[i] + trn;
        if (nt < 0 || nt > 127)
            continue;
        for (j = 0; j < sq.n_notes && sq.notes[j] != (uint8_t)nt; j++)
            ;
        if (j < sq.n_notes)
            continue;                        /* (the same note twice: once) */
        if (sq.rskip_idx == idx && sq.rskip_n) {   /* recorded ahead into this step: it sounds from the key already */
            for (j = 0; j < sq.rskip_n && sq.rskip[j] != s->note[i]; j++)
                ;
            if (j < sq.rskip_n)
                continue;
        }
        syn_note_on_now((uint8_t)nt, s->vel ? s->vel : p->vel);
        sq.notes[sq.n_notes++] = (uint8_t)nt;
    }
    if (sq.rskip_idx == idx)
        sq.rskip_n = 0;
    sq.gate_org = (uint8_t)gate;
    sq.off_at = sq.len_smp * gate / 100u;
    sq.hold = (uint8_t)next_tie;
    sq.played++;
}
static void sq_tick(void)
{
    sq_pat_t *p = &sq_pat[sq.pat];
    sq.ticks++;
    sq.pos++;
    if (sq_midi_out && sq_clock_mode() == SQC_OUT && ++sq.out_div >= SQ_PPQN / 24u) {
        sq.out_div = 0;
        sq_midi_out(0xF8u);
    }
    if (sq.pos < sq.nstart)
        return;
    if ((uint32_t)sq.step + 1u >= p->len) {  /* the end of the loop: the selected pattern takes over, or the song goes on */
        sq.pos = 0;
        sq.loops++;
        if (sq_song()) {
            if (!sq.in_song || sq.ci >= sq_ch.len) {          /* (switched on meanwhile, or the chain got shorter) */
                sq.ci = 0;
                sq.rep_n = 0;
            } else if (++sq.rep_n >= sq_ch.rep[sq.ci]) {
                sq.ci = (uint8_t)((sq.ci + 1u) % sq_ch.len);
                sq.rep_n = 0;
            }
            sq.in_song = 1;
            if (sq.pat != sq_ch.pat[sq.ci])
                sq.rh_n = 0;
            sq.pat = sq_ch.pat[sq.ci];
        } else {
            sq.in_song = 0;
            if (sq.pat != sq_sel_pat()) {
                sq.pat = (uint8_t)sq_sel_pat();
                sq.rh_n = 0;
            }
        }
        sq_fire(0);
    } else {
        sq_fire(sq.step + 1u);
    }
}
static void sq_start(int run)
{
    if (run == sq.run)
        return;
    if (run) {
        sq.pat = (uint8_t)sq_sel_pat();
        sq.in_song = (uint8_t)sq_song();
        sq.ci = 0;
        sq.rep_n = 0;
        if (sq.in_song)
            sq.pat = sq_ch.pat[0];           /* the song starts at its first entry */
        sq.pos = 0;
        sq.ticks = 0;
        sq.loops = 0;
        sq.ph = 0;
        sq.out_div = 0;
        sq.ext_ticks = 0;
        sq.rh_n = 0;
        sq.rskip_n = 0;
        sq.run = 1;
        if (sq_midi_out && sq_clock_mode() == SQC_OUT) {
            sq_midi_out(0xFAu);
            sq_midi_out(0xF8u);
        }
        sq_fire(0);
    } else {
        sq.run = 0;
        sq_release();
        sq_lock_off();
        sq.rh_n = 0;
        if (sq_midi_out && sq_clock_mode() == SQC_OUT)
            sq_midi_out(0xFCu);
    }
}
/* MIDI real-time in (audio context, before the render): F8 clocks the sequencer (clock AUTO), FA / FB start, FC stops */
static void sq_realtime(uint8_t b)
{
    if (b == 0xF8u) {
        uint32_t dt = sq.smp - sq.ext_last;
        if (sq.ext_last && dt > 0u && dt < 44100u)
            sq.ext_dt = sq.ext_dt ? (sq.ext_dt * 3u + dt) / 4u : dt;
        sq.ext_last = sq.smp ? sq.smp : 1u;
        if (sq_clock_mode() == SQC_AUTO && sq.run && sq.ext_ticks < 200u)
            sq.ext_ticks = (uint8_t)(sq.ext_ticks + SQ_PPQN / 24u);
        return;
    }
    if (sq_clock_mode() != SQC_AUTO)
        return;
    if (b == 0xFAu || b == 0xFBu) {
        sq_start(0);
        sq_start(1);
        sq.ext_ticks = 0;
    } else if (b == 0xFCu) {
        sq_start(0);
    }
}
static uint32_t sq_ext_bpm10(void) { return sq_ext() && sq.ext_dt ? 44100u * 600u / (sq.ext_dt * 24u) : 0u; }

/* once a block of n samples (audio context): the clock, the gates */
static void sq_clock(uint32_t n)
{
    sq.smp += n;
    if (!sq.run)
        return;
    sq.elapsed += n;
    if (sq.n_notes && !sq.hold && sq.elapsed >= sq.off_at)
        sq_release();
    if (sq_ext()) {
        while (sq.ext_ticks) {
            sq.ext_ticks--;
            sq_tick();
        }
        sq.ph = 0;                           /* (the tempo takes over from a tick's start when the clock goes) */
        return;
    }
    sq.ext_ticks = 0;
    sq.ph += n * sq_bpm10() * SQ_PPQN;
    while (sq.ph >= 44100u * 600u) {
        sq.ph -= 44100u * 600u;
        sq_tick();
    }
}

/* ---------------------------------------------------- live recording --- */
/* A note played while recording goes into the nearest step: the one playing, or the next one when it is past the
 * middle of the playing one. Overdub: a step that holds notes gets this one added (a chord of up to 4; when full the
 * last one is replaced). Held on, each further step the sequencer enters becomes a TIE (sq_rec_hold), up to the pattern
 * length; a release before the middle of the last one puts that step back (sq_rec_release), so a short note stays
 * one step. (Felucca's rec_note, one track, no drums, no nudge.) Audio context. */
static void sq_rec_note(uint8_t note, uint8_t vel)
{
    sq_pat_t *p = &sq_pat[sq.pat];
    uint32_t len = p->len < 1u ? 1u : p->len, idx = sq.step % len, k, next = sq.elapsed > sq.len_smp / 2u;
    sq_step_t *s;
    if (next)
        idx = (idx + 1u) % len;
    s = &p->s[idx];
    if (!sq_is_on(s) || sq_is_tie(s) || !sq_n(s)) {     /* a fresh step */
        memset(s->note, 0, sizeof s->note);
        s->f = SQ_ON;
        s->vel = 0;
        s->gate = 0;
        s->chance = 0;
    }
    for (k = 0; k < sq_n(s) && s->note[k] != note; k++)
        ;
    if (k == sq_n(s)) {
        if (k < SQ_NOTES)
            sq_set_n(s, k + 1u);
        s->note[sq_n(s) - 1u] = note;
    }
    if (vel > s->vel)
        s->vel = vel;
    sq_touch(sq.pat);
    if (next) {                              /* it sounds now: the step must not trigger it again */
        if (sq.rskip_idx != idx)
            sq.rskip_n = 0;
        sq.rskip_idx = (uint8_t)idx;
        if (sq.rskip_n < SQ_NOTES)
            sq.rskip[sq.rskip_n++] = note;
    }
    if (!sq.rh_n || sq.rh_start != idx) {    /* a new hold (one in another step ends) */
        sq.rh_n = 0;
        sq.rh_start = (uint8_t)idx;
        sq.rh_ties = 0;
    }
    for (k = 0; k < sq.rh_n && sq.rh_note[k] != note; k++)
        ;
    if (k == sq.rh_n && sq.rh_n < SQ_NOTES)
        sq.rh_note[sq.rh_n++] = note;        /* a chord: held until its last key is up */
}
static void sq_rec_release(uint8_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < sq.rh_n; i++)
        if (sq.rh_note[i] != note)
            sq.rh_note[k++] = sq.rh_note[i];
    if (k == sq.rh_n || (sq.rh_n = (uint8_t)k))
        return;                              /* not one of them, or others still held */
    if (sq.rh_ties && sq.step == sq.rh_last && sq.elapsed < sq.len_smp / 2u)
        sq_pat[sq.pat].s[sq.rh_last] = sq.rh_bak;       /* released early in it: not held into this step */
}

/* --------------------------------------------- editing (main loop) --- */
static struct {
    int8_t held;                             /* the step held on the grid (0..63), -1 = none */
    uint8_t held_key;                        /* its key */
    uint8_t held_used;                       /* a note or a knob changed it while held (letting go then toggles nothing) */
    uint8_t held_fresh;                      /* the next note entered replaces the step's notes */
    uint8_t view;                            /* the step page shown on the keys (0..3) */
    uint8_t follow;                          /* the view follows the playing step */
    uint8_t cursor;                          /* step recording writes here */
    uint8_t entry_open;                      /* step recording: keys of a chord are down */
    uint8_t last_note;                       /* the last note entered: a step switched on gets it */
    uint8_t lview;                           /* a held step's knobs: 0 its NOTE / VEL / GATE / CHANCE, 1.. a lock page */
    uint64_t more;                           /* more steps held with it (SEL + their keys): bit i = step i */
} sq_ed = {-1, 0, 0, 0, 0, 1, 0, 0, 60, 0, 0};

static sq_pat_t *sq_ed_pat(void) { return &sq_pat[sq_sel_pat()]; }
static void sq_add_note(sq_step_t *s, uint8_t note)
{
    uint32_t k;
    for (k = 0; k < sq_n(s) && s->note[k] != note; k++)
        ;
    if (k < sq_n(s))
        return;
    if (k < SQ_NOTES)
        sq_set_n(s, k + 1u);
    s->note[sq_n(s) - 1u] = note;
}
/* a tap on a step: on <-> off; a step switched on with nothing in it gets the last note */
static void sq_ed_toggle(uint32_t i)
{
    sq_step_t *s = &sq_ed_pat()->s[i % SQ_STEPS];
    if (sq_is_on(s)) {
        s->f &= (uint8_t)~SQ_ON;
    } else {
        s->f |= SQ_ON;
        if (!sq_has(s)) {
            s->note[0] = sq_ed.last_note;
            sq_set_n(s, 1);
        }
    }
    sq_touch(sq_sel_pat());
}
/* a step's key down on the grid: it is held (and the cursor goes there); up: a tap toggles it */
static void sq_ed_step_down(uint32_t i, uint32_t key)
{
    sq_ed.held = (int8_t)(i % SQ_STEPS);
    sq_ed.held_key = (uint8_t)key;
    sq_ed.held_used = 0;
    sq_ed.held_fresh = 1;
    sq_ed.cursor = (uint8_t)sq_ed.held;
}
static void sq_ed_step_up(void)
{
    if (sq_ed.held < 0)
        return;
    if (!sq_ed.held_used && !sq_ed.more)
        sq_ed_toggle((uint32_t)sq_ed.held);
    sq_ed.held = -1;
    sq_ed.more = 0;
}
static int sq_ed_is_held(uint32_t i)         /* step i is the held one or one of the steps held with it */
{
    return sq_ed.held >= 0 && (i == (uint32_t)sq_ed.held || ((sq_ed.more >> (i % SQ_STEPS)) & 1u));
}
static uint32_t sq_ed_held_n(void)
{
    uint32_t n = 0, i;
    for (i = 0; i < SQ_STEPS; i++)
        n += (uint32_t)sq_ed_is_held(i);
    return n;
}
/* a note while a step is held: the first replaces what it holds, the next ones make a chord (up to 4) */
static void sq_ed_note(uint8_t note)
{
    sq_step_t *s;
    if (sq_ed.held < 0)
        return;
    s = &sq_ed_pat()->s[sq_ed.held];
    if (sq_ed.held_fresh) {
        memset(s->note, 0, sizeof s->note);
        s->f = SQ_ON;
        sq_ed.held_fresh = 0;
    }
    s->f |= SQ_ON;
    sq_add_note(s, note);
    sq_ed.held_used = 1;
    sq_ed.last_note = note;
    sq_touch(sq_sel_pat());
}
/* K1..K4 on a step (held, or the cursor's when step recording): note (all its notes move), velocity, gate, chance */
static int sq_step_get(const sq_step_t *s, uint32_t k)
{
    switch (k) {
    case 0: return sq_n(s) ? s->note[0] : -1;
    case 1: return s->vel;                          /* 0 = the pattern's */
    case 2: return s->gate;                         /* 0 = the pattern's */
    default: return s->chance ? s->chance : 100;
    }
}
static void sq_ed_step_turn(uint32_t i, uint32_t k, int d)
{
    sq_step_t *s = &sq_ed_pat()->s[i % SQ_STEPS];
    uint32_t j;
    int v;
    switch (k) {
    case 0:
        if (!sq_n(s)) {                             /* nothing to move: it gets the last note */
            s->note[0] = sq_ed.last_note;
            s->f = (uint8_t)(SQ_ON | 1u);
            break;
        }
        for (j = 0; j < sq_n(s); j++)
            if ((int)s->note[j] + d < 0 || (int)s->note[j] + d > 127)
                return;
        for (j = 0; j < sq_n(s); j++)
            s->note[j] = (uint8_t)(s->note[j] + d);
        sq_ed.last_note = s->note[0];
        break;
    case 1:
        v = (int)s->vel + d;
        s->vel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        break;
    case 2:                                         /* 0 (the pattern's), 5 .. 100 % in steps of 5 */
        v = (int)s->gate + 5 * d;
        s->gate = (uint8_t)(v < 5 ? 0 : v > 100 ? 100 : v);
        break;
    default:
        v = (s->chance ? s->chance : 100) + d;
        v = v < 1 ? 1 : v > 100 ? 100 : v;
        s->chance = (uint8_t)(v >= 100 ? 0 : v);
        break;
    }
    if (sq_ed.held == (int8_t)i)
        sq_ed.held_used = 1;
    sq_touch(sq_sel_pat());
}
/* step recording: the keys pressed together become the cursor step; letting go of all of them moves on (Felucca's
 * seq_entry); EDIT writes a rest, GLO a TIE (each moves on) */
static void sq_ed_entry(uint8_t note, uint8_t vel)
{
    sq_step_t *s = &sq_ed_pat()->s[sq_ed.cursor % SQ_STEPS];
    if (!sq_ed.entry_open) {
        sq_ed.entry_open = 1;
        memset(s->note, 0, sizeof s->note);
        s->f = SQ_ON;
        s->vel = 0;
    }
    sq_add_note(s, note);
    if (vel > s->vel)
        s->vel = vel;
    sq_ed.last_note = note;
    sq_touch(sq_sel_pat());
}
static void sq_ed_advance(void)
{
    uint32_t len = sq_ed_pat()->len;
    sq_ed.cursor = (uint8_t)((sq_ed.cursor + 1u) % (len ? len : 1u));
    sq_ed.view = (uint8_t)(sq_ed.cursor / SQ_PAGE);
}
static void sq_ed_entry_done(void)
{
    if (!sq_ed.entry_open)
        return;
    sq_ed.entry_open = 0;
    sq_ed_advance();
}
static void sq_ed_rest(void)
{
    sq_step_t *s = &sq_ed_pat()->s[sq_ed.cursor % SQ_STEPS];
    memset(s, 0, sizeof *s);
    sq_lock_clear_step(sq_sel_pat(), sq_ed.cursor);     /* (a rest is an empty step: its locks go too) */
    sq_ed.entry_open = 0;
    sq_touch(sq_sel_pat());
    sq_ed_advance();
}
static void sq_ed_tie(void)
{
    sq_step_t *s = &sq_ed_pat()->s[sq_ed.cursor % SQ_STEPS];
    memset(s->note, 0, sizeof s->note);
    s->f = SQ_ON | SQ_TIE;
    s->vel = 0;
    sq_ed.entry_open = 0;
    sq_touch(sq_sel_pat());
    sq_ed_advance();
}
static void sq_copy(uint32_t from, uint32_t to)
{
    if (from >= SQ_PATS || to >= SQ_PATS || from == to)
        return;
    sq_pat[to] = sq_pat[from];
    sq_lk[to] = sq_lk[from];
    sq_touch(to);
}
static void sq_clear(uint32_t p)
{
    if (p >= SQ_PATS)
        return;
    sq_pat_init(&sq_pat[p]);
    sq_lanes_init(&sq_lk[p]);
    sq_touch(p);
}
/* the chain (main loop): entry i = pattern p, played rep times */
static void sq_chain_set(uint32_t i, uint32_t pat, uint32_t rep)
{
    if (i > sq_ch.len || i >= SQ_CHAIN)
        return;
    if (i == sq_ch.len)
        sq_ch.len++;                         /* (one past the end: appended) */
    sq_ch.pat[i] = (uint8_t)(pat % SQ_PATS);
    sq_ch.rep[i] = (uint8_t)(rep < 1u ? 1u : rep > 16u ? 16u : rep);
    sq_touch(0);
}
static void sq_chain_insert(uint32_t i)      /* a copy of entry i after it */
{
    if (i >= sq_ch.len || sq_ch.len >= SQ_CHAIN)
        return;
    memmove(sq_ch.pat + i + 1, sq_ch.pat + i, sq_ch.len - i);
    memmove(sq_ch.rep + i + 1, sq_ch.rep + i, sq_ch.len - i);
    sq_ch.len++;
    sq_touch(0);
}
static void sq_chain_delete(uint32_t i)
{
    if (i >= sq_ch.len)
        return;
    memmove(sq_ch.pat + i, sq_ch.pat + i + 1, sq_ch.len - i - 1u);
    memmove(sq_ch.rep + i, sq_ch.rep + i + 1, sq_ch.len - i - 1u);
    sq_ch.len--;
    sq_touch(0);
}
static uint32_t sq_count_on(const sq_pat_t *p)     /* steps that play (tests, the screen) */
{
    uint32_t i, c = 0;
    for (i = 0; i < p->len; i++)
        c += (uint32_t)sq_plays(&p->s[i]);
    return c;
}
