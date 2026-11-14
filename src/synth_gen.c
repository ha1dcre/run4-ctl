/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the pattern tools of the SEQ page (synth_ui.c: the GENERATE and PHRASE sub-pages, the grid's
 * copy and paste). The generators fill the selected pattern: EUCLID (hits spread evenly over a cycle, rotated; a hit
 * keeps the notes its step has), SPARSE (notes here and there, the downbeats likelier), PULSE (a note every n steps,
 * held over TIEs), ARP (the chord, or the scale's seventh chord, arpeggiated over the steps) and DRIFT (a random walk
 * in the scale), all in the scale of GLO's KEYS page (with SCALE OFF: the minor pentatonic on the note), from the last
 * note entered on the grid. The phrase tools rotate a pattern, transpose it (in steps of the scale when one is set),
 * reverse it (a note and the TIEs after it stay together) and mirror it (the pitches upside down, into the scale); a
 * step, or a range of steps, is copied and pasted with its locks. Each change keeps one level of undo: the pattern and
 * its lock lanes as they were (undo again: redo).
 *
 * In the spirit of the sequence generators of Mutable Dreams (Kid Bunka, github.com/kidbee85-beep/mutable-dreams, MIT
 * OR GPL-3.0-only: euclid, sparse, arp from the chord / scale, pulse, drift; its phrase copy / paste / transpose /
 * mirror); the code is new. Main loop only, plain C, integer only; included by synth.c after the scales. */

enum { GEN_EUCLID, GEN_SPARSE, GEN_PULSE, GEN_ARP, GEN_DRIFT, GEN_N };
static const char *const GEN_NAME[GEN_N] = {"EUCLID", "SPARSE", "PULSE", "ARP", "DRIFT"};
static const char *const GEN_PNAME[GEN_N][3] = {{"HITS", "STEPS", "ROTATE"}, {"DENSITY", "RANGE", "SEED"},
                                                {"EVERY", "OFFSET", "LENGTH"}, {"SHAPE", "RANGE", "EVERY"},
                                                {"DENSITY", "MOVE", "SEED"}};
static const uint8_t GEN_LO[GEN_N][3] = {{0, 0, 0}, {5, 1, 1}, {1, 0, 1}, {0, 1, 1}, {5, 1, 1}};
static const uint8_t GEN_HI[GEN_N][3] = {{64, 64, 63}, {100, 3, 99}, {16, 15, 8}, {3, 3, 4}, {100, 4, 99}};
static const char *const GEN_SHAPE[4] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
/* the GENERATE sub-page's settings (RAM: the defaults at each start). EUCLID's STEPS 0 = the pattern's length */
static struct { uint8_t gen, par[GEN_N][3]; } sgg = {GEN_EUCLID, {{4, 0, 0}, {40, 2, 1}, {4, 0, 1}, {0, 2, 1}, {60, 2, 1}}};

/* ----------------------------------------------------------------- undo --- */
static struct { sq_pat_t p; sq_lanes_t l; uint8_t pat, valid, undone; } sgu;
static void sg_keep(uint32_t pat)             /* before a change of pattern pat */
{
    pat %= SQ_PATS;
    sgu.p = sq_pat[pat];
    sgu.l = sq_lk[pat];
    sgu.pat = (uint8_t)pat;
    sgu.valid = 1;
    sgu.undone = 0;
}
static void sg_swap(void *a, void *b, uint32_t n)
{
    uint8_t *x = (uint8_t *)a, *y = (uint8_t *)b, t;
    while (n--) { t = *x; *x++ = *y; *y++ = t; }
}
static int sg_undo(void)                      /* 1 undone, 0 redone, -1 nothing kept */
{
    if (!sgu.valid)
        return -1;
    sg_swap(&sq_pat[sgu.pat], &sgu.p, sizeof sgu.p);
    sg_swap(&sq_lk[sgu.pat], &sgu.l, sizeof sgu.l);
    sgu.undone ^= 1u;
    sq_touch(sgu.pat);
    return sgu.undone;
}

/* ----------------------------------------------------------- generators --- */
static void sg_cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++) != 0) ; }
static void sg_cat_u(char *d, uint32_t v)     /* a number at the end of d */
{
    char t[12];
    int n = 0;
    do t[n++] = (char)('0' + v % 10u); while ((v /= 10u) != 0u && n < 11);
    while (*d) d++;
    while (n) *d++ = t[--n];
    *d = 0;
}
static uint32_t sg_rnd(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r >> 9; }
static uint32_t sg_scale(void) { return syn.scale % SCL_N != SCL_CHROM ? syn.scale % SCL_N : SCL_MINPENT; }
static uint32_t sg_sroot(void) { return syn.scale % SCL_N != SCL_CHROM ? syn.root % 12u : sq_ed.last_note % 12u; }
static int32_t sg_root(void) { return syn_snap(sq_ed.last_note, sg_scale(), sg_sroot()); }
static uint32_t sg_per_oct(void)
{
    uint32_t m = SCL_MASK[sg_scale()], k, c = 0;
    for (k = 0; k < 12u; k++)
        c += (m >> k) & 1u;
    return c;
}
static uint8_t sg_clamp_note(int32_t n) { return (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n); }
static void sg_note(sq_step_t *s, int32_t note, uint32_t vel)
{
    memset(s, 0, sizeof *s);
    s->note[0] = sg_clamp_note(note);
    s->f = (uint8_t)(SQ_ON | 1u);
    s->vel = (uint8_t)(vel > 127u ? 127u : vel);
}
static int sg_euclid_hit(uint32_t i, uint32_t hits, uint32_t steps, uint32_t rot)   /* step i of the cycle is a hit */
{
    uint32_t j;
    if (!steps || !hits)
        return 0;
    if (hits >= steps)
        return 1;
    j = (i + steps - rot % steps) % steps;
    return (j * hits) % steps < hits;
}
/* the selected generator into pattern pat (undo kept); out: what the screen says ("EUCLID 4/16") */
static void sg_generate(uint32_t pat, char *out)
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    uint32_t L = p->len < 1u ? 1u : p->len, i, g = sgg.gen % GEN_N, a = sgg.par[g][0], b = sgg.par[g][1], c = sgg.par[g][2];
    uint32_t r = 0x9E3779B9u ^ (c * 2654435761u) ^ (g << 24), cnt = sg_per_oct(), sc = sg_scale(), ro = sg_sroot();
    int32_t R = sg_root();
    sg_keep(pat);
    strcpy(out, GEN_NAME[g]);
    switch (g) {
    case GEN_EUCLID: {                         /* hits a over b steps (0 = the length), rotated c: a hit keeps its notes */
        uint32_t steps = b ? b : L;
        for (i = 0; i < L; i++) {
            sq_step_t *s = &p->s[i];
            if (sg_euclid_hit(i % steps, a, steps, c)) {
                s->f |= SQ_ON;
                if (!sq_has(s)) {
                    s->note[0] = sg_clamp_note(R);
                    sq_set_n(s, 1);
                }
            } else {
                s->f &= (uint8_t)~SQ_ON;
            }
        }
        sg_cat(out, " "); sg_cat_u(out, a); sg_cat(out, "/"); sg_cat_u(out, steps);
        break;
    }
    case GEN_SPARSE:                           /* density a %, b octaves, seed c */
        for (i = 0; i < L; i++) {
            uint32_t dens = a + (i % 4u == 0u ? 15u : 0u);
            if (sg_rnd(&r) % 100u < dens) {
                uint32_t d = sg_rnd(&r) % (cnt * b), lo = sg_rnd(&r) % 3u;
                if (lo == 0u && d >= cnt) d -= cnt;            /* (the lower octave a little likelier) */
                sg_note(&p->s[i], syn_degree(R, (int32_t)d, sc, ro), 80u + sg_rnd(&r) % 41u + (i % 4u == 0u ? 6u : 0u));
            } else {
                memset(&p->s[i], 0, sizeof p->s[i]);
            }
        }
        break;
    case GEN_PULSE: {                          /* a note every a steps from step c + 1, held over b - 1 TIEs */
        uint32_t k = 0, j;
        for (i = 0; i < L; i++)
            memset(&p->s[i], 0, sizeof p->s[i]);
        for (i = b % a; i < L; i += a, k++) {
            sg_note(&p->s[i], R, k % 2u ? 92u : 112u);
            for (j = 1; j < c && i + j < L && j < a; j++)
                p->s[i + j].f = SQ_ON | SQ_TIE;
        }
        break;
    }
    case GEN_ARP: {                            /* the chord over b octaves, shape a, a note every c steps */
        uint8_t ch[SQ_NOTES], list[SQ_NOTES * 3];
        uint32_t n = syn_chord(R, syn.chord % CH_N ? syn.chord : CH_SCALE4, 0, SPR_CLOSE, syn.scale, syn.root, ch), m = 0, k = 0, o;
        for (o = 0; o < b; o++)
            for (i = 0; i < n; i++)
                list[m++] = sg_clamp_note((int32_t)ch[i] + 12 * (int32_t)o);
        for (i = 0; i < L; i++) {
            memset(&p->s[i], 0, sizeof p->s[i]);
            if (!m || i % c)
                continue;
            switch (a) {
            case 1: o = m - 1u - k % m; break;
            case 2: o = m < 2u ? 0u : k % (2u * m - 2u) < m ? k % (2u * m - 2u) : 2u * m - 2u - k % (2u * m - 2u); break;
            case 3: o = sg_rnd(&r) % m; break;
            default: o = k % m; break;
            }
            sg_note(&p->s[i], list[o], k % (4u * c) == 0u ? 112u : 96u);
            k++;
        }
        sg_cat(out, " "); sg_cat(out, GEN_SHAPE[a % 4u]);
        break;
    }
    default: {                                 /* DRIFT: density a %, moves of up to b scale steps, seed c */
        int32_t d = 0;
        for (i = 0; i < L; i++) {
            if (sg_rnd(&r) % 100u < a) {
                d += (int32_t)(sg_rnd(&r) % (2u * b + 1u)) - (int32_t)b;
                d = d < -(int32_t)cnt ? -(int32_t)cnt + 1 : d > 2 * (int32_t)cnt ? 2 * (int32_t)cnt - 1 : d;
                sg_note(&p->s[i], syn_degree(R, d, sc, ro), 84u + sg_rnd(&r) % 31u);
            } else {
                memset(&p->s[i], 0, sizeof p->s[i]);
            }
        }
        break;
    }
    }
    sq_touch(pat);
}

/* -------------------------------------------------------- phrase tools --- */
static void sg_rotate(uint32_t pat, int d)    /* the steps (and their locks) d steps later, the last ones to the front */
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    uint32_t L = p->len < 1u ? 1u : p->len, i, k, sh = (uint32_t)(((d % (int)L) + (int)L) % (int)L);
    sq_step_t t[SQ_STEPS];
    uint8_t v[SQ_STEPS];
    if (!sh)
        return;
    for (i = 0; i < L; i++)
        t[(i + sh) % L] = p->s[i];
    memcpy(p->s, t, L * sizeof t[0]);
    for (k = 0; k < SQ_LANES; k++) {
        if (l->id[k] == SQ_NOLOCK)
            continue;
        for (i = 0; i < L; i++)
            v[(i + sh) % L] = l->v[k][i];
        memcpy(l->v[k], v, L);
    }
    sq_touch(pat);
}
static int32_t sg_move(int32_t n, int d)      /* a note d steps up the scale (a semitone each with the scale off) */
{
    if (syn.scale % SCL_N == SCL_CHROM)
        return n + d;
    return syn_degree(syn_snap(n, syn.scale, syn.root), d, syn.scale, syn.root);
}
static int sg_transpose(uint32_t pat, int d)  /* every note d steps; 0 ok, -1 one would leave 0..127 (nothing changes) */
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    uint32_t i, k;
    for (i = 0; i < SQ_STEPS; i++)
        for (k = 0; k < sq_n(&p->s[i]); k++) {
            int32_t n = sg_move(p->s[i].note[k], d);
            if (n < 0 || n > 127)
                return -1;
        }
    for (i = 0; i < SQ_STEPS; i++)
        for (k = 0; k < sq_n(&p->s[i]); k++)
            p->s[i].note[k] = (uint8_t)sg_move(p->s[i].note[k], d);
    sq_touch(pat);
    return 0;
}
static void sg_reverse(uint32_t pat)          /* back to front by phrases: a note and the TIEs after it stay in order */
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    uint32_t L = p->len < 1u ? 1u : p->len, i, j, k, w = 0;
    uint8_t st[SQ_STEPS], rl[SQ_STEPS], nr = 0, map[SQ_STEPS];
    sq_step_t t[SQ_STEPS];
    uint8_t v[SQ_STEPS];
    for (i = 0; i < L; i = j) {                /* the runs: a step and the TIEs that follow it */
        for (j = i + 1; j < L && sq_is_on(&p->s[j]) && sq_is_tie(&p->s[j]); j++)
            ;
        st[nr] = (uint8_t)i;
        rl[nr++] = (uint8_t)(j - i);
    }
    while (nr--)
        for (k = 0; k < rl[nr]; k++)
            map[w++] = (uint8_t)(st[nr] + k);  /* new step w is old step map[w] */
    for (i = 0; i < L; i++)
        t[i] = p->s[map[i]];
    memcpy(p->s, t, L * sizeof t[0]);
    for (k = 0; k < SQ_LANES; k++) {
        if (l->id[k] == SQ_NOLOCK)
            continue;
        for (i = 0; i < L; i++)
            v[i] = l->v[k][map[i]];
        memcpy(l->v[k], v, L);
    }
    sq_touch(pat);
}
static void sg_mirror(uint32_t pat)           /* the pitches upside down around the middle of their range, into the scale */
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    uint32_t i, k, lo = 127, hi = 0;
    for (i = 0; i < SQ_STEPS; i++)
        for (k = 0; k < sq_n(&p->s[i]); k++) {
            if (p->s[i].note[k] < lo) lo = p->s[i].note[k];
            if (p->s[i].note[k] > hi) hi = p->s[i].note[k];
        }
    if (hi < lo)
        return;
    for (i = 0; i < SQ_STEPS; i++)
        for (k = 0; k < sq_n(&p->s[i]); k++)
            p->s[i].note[k] = sg_clamp_note(syn_snap((int32_t)(lo + hi) - p->s[i].note[k], syn.scale, syn.root));
    sq_touch(pat);
}

/* ------------------------------------------------------- copy and paste --- */
static struct { uint8_t n, id[SQ_LANES]; sq_step_t s[SQ_STEPS]; uint8_t v[SQ_LANES][SQ_STEPS]; } sgc;
static void sg_copy(uint32_t pat, uint32_t from, uint32_t to)   /* steps from..to (with their locks) to the clipboard */
{
    const sq_lanes_t *l = &sq_lk[pat % SQ_PATS];
    uint32_t i, k;
    if (to < from || to >= SQ_STEPS)
        return;
    sgc.n = (uint8_t)(to - from + 1u);
    memcpy(sgc.id, l->id, sizeof sgc.id);
    for (i = 0; i < sgc.n; i++) {
        sgc.s[i] = sq_pat[pat % SQ_PATS].s[from + i];
        for (k = 0; k < SQ_LANES; k++)
            sgc.v[k][i] = l->id[k] == SQ_NOLOCK ? SQ_NOLOCK : l->v[k][from + i];
    }
}
/* the clipboard into pattern pat from step at (the pattern's length cuts it; undo kept): steps pasted, and in *full
 * the locks that found no lane */
static uint32_t sg_paste(uint32_t pat, uint32_t at, uint32_t *full)
{
    sq_pat_t *p = &sq_pat[pat % SQ_PATS];
    uint32_t i, k, n = 0;
    *full = 0;
    if (!sgc.n)
        return 0;
    sg_keep(pat);
    for (i = 0; i < sgc.n && at + i < p->len; i++, n++) {
        p->s[at + i] = sgc.s[i];
        sq_lock_clear_step(pat, at + i);
        for (k = 0; k < SQ_LANES; k++)
            if (sgc.id[k] != SQ_NOLOCK && sgc.v[k][i] != SQ_NOLOCK &&
                sq_lock_set(pat, at + i, sgc.id[k], (int32_t)sgc.v[k][i] + LK_LO[sgc.id[k]]) < 0)
                (*full)++;
    }
    sq_touch(pat);
    return n;
}
