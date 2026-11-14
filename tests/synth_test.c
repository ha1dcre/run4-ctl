/* SPDX-License-Identifier: GPL-3.0-only
 * Native tests of the Synth mode's sound (src/synth.c): renders, checks and measures the engine without the platform.
 *   cc tests/synth_test.c  ->  build/synth_test [wav_out_dir]     (make test runs it; the WAV is optional) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/synth.c"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define BLK 256
static int32_t pcm[2 * 44100 * 40];
static uint32_t pn;                                   /* frames rendered into pcm */

static void run(uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i += BLK) {
        syn_render(pcm + 2 * pn, BLK, 4096);
        pn += BLK;
    }
}
static double peak_since(uint32_t from, uint32_t to, double *rms)
{
    double pk = 0, acc = 0;
    uint32_t i;
    for (i = from; i < to; i++) {
        double l = pcm[2 * i] / 8388608.0, r = pcm[2 * i + 1] / 8388608.0;
        if (fabs(l) > pk) pk = fabs(l);
        if (fabs(r) > pk) pk = fabs(r);
        acc += l * l + r * r;
    }
    if (rms) *rms = sqrt(acc / (2.0 * (to - from)));
    return pk;
}
static void wav(const char *path, uint32_t from, uint32_t to)       /* mono 16-bit 44.1 kHz */
{
    FILE *f = fopen(path, "wb");
    uint32_t n = to - from, i, v;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xAC, 0, 0,
                     0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    if (!f) return;
    v = 36 + n * 2; memcpy(h + 4, &v, 4); v = n * 2; memcpy(h + 40, &v, 4);
    fwrite(h, 1, 44, f);
    for (i = from; i < to; i++) {
        int32_t s = (pcm[2 * i] + pcm[2 * i + 1]) / 2 >> 8;
        int16_t o = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        fwrite(&o, 2, 1, f);
    }
    fclose(f);
}
static uint8_t cksum(const uint8_t *d, uint32_t n) { uint32_t s = 0, i; for (i = 0; i < n; i++) s += d[i]; return (uint8_t)((128u - (s & 127u)) & 127u); }
static void feed(const uint8_t *d, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) syn_sysex_byte(d[i]); }

/* ---------------------------------------------------------------- sequencer (src/synth_seq.c) --- */
static int keyed(int note)                            /* voices started and not released, of this note (-1: any) */
{
    int i, c = 0;
    for (i = 0; i < SYN_VOICES; i++)
        if (syn_v[i].n.live && syn_v[i].key && (note < 0 || syn_v[i].note == note))
            c++;
    return c;
}
static uint32_t until_step(uint32_t step, uint32_t max)   /* render 32 samples at a time until that step plays */
{
    uint32_t n = 0;
    while (sq.step != step && n < max) {
        syn_render(pcm + 2 * (pn % (44100 * 30)), FM6_N, 4096);
        n += FM6_N;
    }
    return n;
}
static void run_small(uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i += FM6_N)
        syn_render(pcm, FM6_N, 4096);
}
static int rt_count[256];
static void rt_out(uint8_t b) { rt_count[b]++; }
static void seq_tests(void)
{
    sq_pat_t *p;
    sq_step_t *s;
    uint32_t i, n, spt;
    static sq_obj_t ob[SQ_OBJS];
    static sq_pat_t keep[SQ_PATS];
    syn_event(6, 0, 0, 0);
    run_small(256);
    /* defaults: pattern 1 holds the bass line, the others are empty 16-step 1/16 patterns */
    sq_defaults();
    CHECK(sq_count_on(&sq_pat[0]) == 10 && sq_pat[0].len == 16);
    for (i = 1; i < SQ_PATS; i++)
        CHECK(sq_count_on(&sq_pat[i]) == 0 && sq_pat[i].len == 16 && sq_pat[i].rate == SQR_16 && sq_pat[i].swing == 50);

    /* the grid: a tap toggles a step on (with the last note) and off (its notes kept) */
    syn.seq_pat = 1;
    p = &sq_pat[1];
    sq_ed.last_note = 60;
    sq_ed_step_down(3, 6); sq_ed_step_up();
    CHECK(sq_plays(&p->s[3]) && sq_n(&p->s[3]) == 1 && p->s[3].note[0] == 60);
    sq_ed_step_down(3, 6); sq_ed_step_up();
    CHECK(!sq_plays(&p->s[3]) && sq_n(&p->s[3]) == 1 && p->s[3].note[0] == 60);
    sq_ed_step_down(3, 6); sq_ed_step_up();
    CHECK(sq_plays(&p->s[3]));
    CHECK(sq_dirty & 1u);
    /* hold a step + keys: the first note replaces, the next ones make a chord of up to 4 (a fifth replaces the last) */
    sq_ed_step_down(5, 9);
    sq_ed_note(60); sq_ed_note(64); sq_ed_note(67); sq_ed_note(71); sq_ed_note(72);
    sq_ed_step_up();
    s = &p->s[5];
    CHECK(sq_plays(s) && sq_n(s) == 4 && s->note[0] == 60 && s->note[1] == 64 && s->note[2] == 67 && s->note[3] == 72);
    /* hold a step + K1..K4: the chord moves, velocity, gate, chance; letting go does not toggle */
    sq_ed_step_down(5, 9);
    sq_ed_step_turn(5, 0, 2);
    sq_ed_step_turn(5, 1, 90);
    sq_ed_step_turn(5, 2, 2);
    sq_ed_step_turn(5, 3, -50);
    sq_ed_step_up();
    CHECK(sq_plays(s) && s->note[0] == 62 && s->note[3] == 74 && s->vel == 90 && s->gate == 10 && s->chance == 50);
    CHECK(sq_step_get(s, 3) == 50 && sq_step_get(&p->s[0], 3) == 100 && sq_step_get(&p->s[0], 0) == -1);
    sq_ed_step_turn(5, 3, 60);                         /* 100 % is stored as 0 (always) */
    CHECK(s->chance == 0);
    sq_ed_step_turn(5, 0, 100);                        /* past 127: refused, the chord keeps its shape */
    CHECK(s->note[0] == 62 && s->note[3] == 74);

    /* step recording: a chord into the cursor step, letting go moves on; EDIT a rest, GLO a TIE; the cursor wraps at the length */
    sq_ed.cursor = 8;
    sq_ed_entry(48, 90); sq_ed_entry(52, 100); sq_ed_entry_done();
    CHECK(sq_plays(&p->s[8]) && sq_n(&p->s[8]) == 2 && p->s[8].note[1] == 52 && p->s[8].vel == 100 && sq_ed.cursor == 9);
    sq_ed_rest();
    CHECK(!sq_has(&p->s[9]) && sq_ed.cursor == 10);
    sq_ed_tie();
    CHECK(sq_plays(&p->s[10]) && sq_is_tie(&p->s[10]) && sq_ed.cursor == 11);
    p->len = 12;
    sq_ed_entry(55, 100); sq_ed_entry_done();
    CHECK(p->s[11].note[0] == 55 && sq_ed.cursor == 0);
    /* 64 steps: four step pages; the cursor walks into the next page and the view follows it */
    p->len = 64;
    sq_ed.cursor = 15;
    sq_ed_entry(60, 100); sq_ed_entry_done();
    CHECK(sq_ed.cursor == 16 && sq_ed.view == 1);
    sq_ed.cursor = 63;
    sq_ed_rest();
    CHECK(sq_ed.cursor == 0 && sq_ed.view == 0);

    /* copy and clear */
    sq_copy(1, 7);
    CHECK(!memcmp(&sq_pat[1], &sq_pat[7], sizeof(sq_pat_t)) && (sq_dirty & 2u));
    sq_clear(7);
    CHECK(sq_count_on(&sq_pat[7]) == 0 && sq_pat[7].len == 16);

    /* persistence: the four objects round-trip; a damaged one is refused; bad values are put in range */
    for (i = 0; i < SQ_OBJS; i++)
        sq_obj_fill(i, &ob[i]);
    memcpy(keep, sq_pat, sizeof keep);
    sq_defaults();
    CHECK(memcmp(keep, sq_pat, sizeof keep) != 0);
    for (i = 0; i < SQ_OBJS; i++)
        CHECK(sq_obj_take(i, &ob[i], sizeof ob[i]) == 0);
    CHECK(!memcmp(keep, sq_pat, sizeof keep));
    ob[2].magic ^= 1;
    CHECK(sq_obj_take(2, &ob[2], sizeof ob[2]) < 0 && sq_obj_take(1, &ob[2], sizeof ob[2]) < 0);
    ob[2].magic ^= 1;
    ob[2].p[0].len = 0; ob[2].p[0].rate = 9; ob[2].p[0].s[0].f = 0xFF;
    CHECK(sq_obj_take(2, &ob[2], sizeof ob[2]) == 0 && sq_pat[8].len == 16 && sq_pat[8].rate == SQR_16 && sq_n(&sq_pat[8].s[0]) <= 4);
    printf("  seq store: %u bytes in %u objects of %u (16 patterns x 64 steps x 8 bytes, 6 lock lanes each, the chain)\n", (unsigned)(SQ_OBJS * sizeof(sq_obj_t)),
           (unsigned)SQ_OBJS, (unsigned)sizeof(sq_obj_t));

    /* playback: a chord, a TIE holds it over the gate into the next step, a rest ends it */
    syn.rev_mix = 0; syn.dly_mix = 0; syn.chorus = 0; syn.arp_on = 0;
    syn.bpm10 = 1200;
    syn.seq_pat = 2;
    sq_clear(2);
    p = &sq_pat[2];
    p->len = 4; p->gate = 50;
    p->s[0].note[0] = 48; p->s[0].note[1] = 52; p->s[0].note[2] = 55; p->s[0].f = SQ_ON | 3u;
    p->s[1].f = SQ_ON | SQ_TIE;
    p->s[3].note[0] = 60; p->s[3].f = SQ_ON | 1u;
    spt = sq_spt_internal();                          /* 120 BPM: 24 ticks a 1/16 = 5512.5 samples */
    CHECK(spt * 24u / 256u >= 5510u && spt * 24u / 256u <= 5514u);
    syn_event(8, 1, 0, 0);
    run_small(5512 * 3 / 4);                          /* step 1, past the 50 % gate: the chord is held */
    CHECK(sq.run && sq.step == 0 && keyed(-1) == 3);
    run_small(5512);                                  /* in the TIE (step 2 of 4), past its gate: off by now */
    CHECK(sq.step == 1 && keyed(-1) == 0);
    until_step(0, 44100);
    n = until_step(1, 44100);
    run_small(5512 / 4);
    CHECK(sq.step == 1 && keyed(-1) == 3);            /* (the next loop: still held at the TIE's start) */
    until_step(3, 44100);
    run_small(64);
    CHECK(keyed(60) == 1 && keyed(48) == 0);
    /* transpose (SEL + key): the step's notes move as they start */
    syn.seq_trn = 5;
    until_step(0, 44100);
    run_small(64);
    CHECK(keyed(53) == 1 && keyed(57) == 1 && keyed(48) == 0);
    syn.seq_trn = 0;
    /* the next pattern takes over at the end of the loop */
    syn.seq_pat = 0;
    run_small(64);
    CHECK(sq.pat == 2);
    until_step(1, 44100 * 2);
    CHECK(sq.pat == 2);
    until_step(0, 44100 * 2);
    CHECK(sq.pat == 0);
    syn_event(8, 0, 0, 0);
    run_small(44100);
    CHECK(!sq.run && keyed(-1) == 0);

    /* swing 75 %: the odd steps start half a step late */
    syn.seq_pat = 3;
    sq_clear(3);
    p = &sq_pat[3];
    p->len = 4; p->swing = 75;
    CHECK(sq_start_tick(p, 1) == 36 && sq_start_tick(p, 2) == 48 && sq_start_tick(p, 3) == 84 && sq_start_tick(p, 4) == 96);
    syn_event(8, 1, 0, 0);
    run_small(32);
    n = until_step(1, 44100);
    CHECK(n + 32u >= 36u * spt / 256u - 64u && n + 32u <= 36u * spt / 256u + 64u);
    n = until_step(2, 44100);
    CHECK(n >= 12u * spt / 256u - 64u && n <= 12u * spt / 256u + 64u);
    syn_event(8, 0, 0, 0);
    run_small(256);
    /* chance: 50 % plays about half of the time */
    p->swing = 50; p->len = 1; p->rate = SQR_32;
    p->s[0].note[0] = 60; p->s[0].f = SQ_ON | 1u; p->s[0].chance = 50;
    syn_event(8, 1, 0, 0);
    run_small(32);
    i = sq.played;
    run_small(2756 * 100);                            /* 100 steps of 1/32 */
    i = sq.played - i;
    printf("  chance 50 %%: %u of ~100 steps sounded\n", i);
    CHECK(i >= 30 && i <= 70);
    syn_event(8, 0, 0, 0);
    run_small(44100);

    /* live recording: into the nearest step, chords overdub, a held note writes TIEs, an early release puts the last back */
    syn.seq_pat = 4;
    sq_clear(4);
    p = &sq_pat[4];
    p->gate = 50;
    sq.rec = 1;
    syn_event(8, 1, 0, 0);
    run_small(32);
    until_step(2, 44100);
    run_small(1000);                                  /* early in step 3: into it */
    syn_event(1, 60, 100, 0); run_small(64); syn_event(2, 60, 0, 0);
    until_step(5, 44100);
    run_small(4000);                                  /* late in step 6: into step 7, which must not play it again */
    syn_event(1, 62, 110, 0); run_small(64);
    until_step(6, 44100);
    run_small(64);
    CHECK(sq_plays(&p->s[6]) && p->s[6].note[0] == 62 && sq.n_notes == 0 && keyed(62) == 1);
    syn_event(2, 62, 0, 0);
    until_step(8, 44100);
    run_small(200);
    syn_event(1, 64, 100, 0);                         /* held for 3.2 steps: TIEs in 10, 11 (12 put back) */
    until_step(11, 44100);
    run_small(1000);
    syn_event(2, 64, 0, 0);
    run_small(64);
    CHECK(sq_plays(&p->s[2]) && p->s[2].note[0] == 60 && sq_n(&p->s[2]) == 1);
    CHECK(sq_plays(&p->s[8]) && p->s[8].note[0] == 64);
    CHECK(sq_is_tie(&p->s[9]) && sq_is_tie(&p->s[10]) && !sq_has(&p->s[11]));
    /* overdub: the next loop adds a note to step 3 */
    until_step(2, 44100 * 3);
    run_small(1000);
    syn_event(1, 67, 100, 0); run_small(64); syn_event(2, 67, 0, 0);
    run_small(64);
    CHECK(sq_n(&p->s[2]) == 2 && p->s[2].note[1] == 67);
    sq.rec = 0;
    syn_event(8, 0, 0, 0);
    run_small(44100);
    CHECK(keyed(-1) == 0);

    /* MIDI clock out: FA, then 24 clocks a quarter note; FC at the stop */
    syn.seq_clock = SQC_OUT;
    sq_midi_out = rt_out;
    memset(rt_count, 0, sizeof rt_count);
    syn_event(8, 1, 0, 0);
    run_small(44100);                                 /* 1 s at 120 BPM: 2 beats = 48 clocks */
    syn_event(8, 0, 0, 0);
    run_small(64);
    printf("  clock out: FA %d, F8 %d in 1 s, FC %d\n", rt_count[0xFA], rt_count[0xF8], rt_count[0xFC]);
    CHECK(rt_count[0xFA] == 1 && rt_count[0xFC] == 1 && rt_count[0xF8] >= 47 && rt_count[0xF8] <= 50);
    /* MIDI clock in (clock AUTO): FA starts, each F8 is 4 ticks (a 1/16 = 6 clocks), the tempo is measured and the arp
     * follows it; when the clocks stop the tempo takes over; FC stops */
    syn.seq_clock = SQC_AUTO;
    sq_realtime(0xFA);
    CHECK(sq.run && sq.step == 0);
    for (i = 0; i < 24; i++) {
        sq_realtime(0xF8);
        run_small(1024);                              /* 1024 samples a clock: 107.7 BPM */
    }
    printf("  clock in: %u ticks, step %u, %u.%u BPM measured\n", (unsigned)sq.ticks, sq.step, sq_ext_bpm10() / 10, sq_ext_bpm10() % 10);
    CHECK(sq.ticks == 96 && sq.step == 4 && sq_ext_bpm10() >= 1070 && sq_ext_bpm10() <= 1085 && sq_ext());
    CHECK(syn_step_samples(1) >= 6130 && syn_step_samples(1) <= 6160);     /* the arp's 1/16 at 107.7 BPM */
    run_small(22050 + 44100);                         /* the clock is gone: 1 s more at 120 BPM after the half second */
    printf("  clock lost: %u ticks after 1.5 s more (internal 120 BPM)\n", (unsigned)sq.ticks);
    CHECK(!sq_ext() && sq.run && sq.ticks >= 96 + 180 && sq.ticks <= 96 + 200 && syn_step_samples(1) == 5512);
    sq_realtime(0xFC);
    CHECK(!sq.run);
    syn.seq_clock = SQC_INT;                          /* INT: an incoming clock changes nothing */
    sq_realtime(0xFA);
    CHECK(!sq.run);
    syn.seq_clock = SQC_AUTO;
    sq_midi_out = 0;
    run_small(44100);
}

/* ---------------------------------------------------------------- step 2: keys, chords, locks, song, mutate --- */
static uint32_t popc(uint32_t m) { uint32_t n = 0; while (m) { n += m & 1u; m >>= 1; } return n; }
static void step2_tests(void)
{
    syn_set_t before = syn;
    uint8_t c[4];
    uint32_t i, n, k, ok;
    /* scales: sizes, snapping (a tie goes down), degrees, IN KEY */
    CHECK(popc(SCL_MASK[SCL_MAJOR]) == 7 && popc(SCL_MASK[SCL_MINPENT]) == 5 && popc(SCL_MASK[SCL_BLUES]) == 6 &&
          popc(SCL_MASK[SCL_HARM]) == 7 && popc(SCL_MASK[SCL_CHROM]) == 12 && popc(SCL_MASK[SCL_MAJPENT]) == 5);
    CHECK(syn_snap(61, SCL_MAJOR, 0) == 60 && syn_snap(66, SCL_MAJOR, 0) == 65 && syn_snap(64, SCL_MAJOR, 0) == 64);
    CHECK(syn_snap(61, SCL_MINPENT, 9) == 60 && syn_snap(63, SCL_MINPENT, 9) == 62 && syn_snap(61, SCL_CHROM, 0) == 61);
    CHECK(syn_snap(1, SCL_MAJOR, 0) == 0 && syn_snap(127, SCL_MAJOR, 0) == 127);
    CHECK(syn_degree(60, 0, SCL_MAJOR, 0) == 60 && syn_degree(60, 7, SCL_MAJOR, 0) == 72 && syn_degree(60, 2, SCL_MINOR, 0) == 63 &&
          syn_degree(60, -1, SCL_MAJOR, 0) == 59 && syn_degree(57, 5, SCL_MINPENT, 9) == 69 && syn_degree(62, 2, SCL_MAJOR, 0) == 65 &&
          syn_degree(64, -3, SCL_MAJOR, 0) == 59);
    syn.scale = SCL_DORIAN; syn.root = 2; syn.keymode = KM_INKEY;
    CHECK(syn_key_note(53, 0, 0) == 50 && syn_key_note(55, 1, 0) == 52 && syn_key_note(54, -1, 0) == -1 &&
          syn_key_note(53, 7, 1) == 74);                  /* white key 0 = D3 + 12 x octave, black keys silent */
    syn.keymode = KM_SNAP;
    CHECK(syn_key_note(54, -1, 0) == 53);                 /* F#3 in D dorian: F (a tie goes down) */
    /* chords */
    CHECK(syn_chord(60, CH_MAJ, 0, 0, 0, 0, c) == 3 && c[0] == 60 && c[1] == 64 && c[2] == 67);
    CHECK(syn_chord(60, CH_MIN7, 0, 0, 0, 0, c) == 4 && c[1] == 63 && c[3] == 70);
    CHECK(syn_chord(60, CH_MAJ, 1, 0, 0, 0, c) == 3 && c[0] == 64 && c[2] == 72);
    CHECK(syn_chord(60, CH_MAJ, 2, 0, 0, 0, c) == 3 && c[0] == 67 && c[1] == 72 && c[2] == 76);
    CHECK(syn_chord(60, CH_MAJ, 0, SPR_OPEN, 0, 0, c) == 3 && c[0] == 60 && c[1] == 67 && c[2] == 76);
    CHECK(syn_chord(60, CH_MAJ, 0, SPR_WIDE, 0, 0, c) == 3 && c[0] == 48 && c[1] == 64);
    CHECK(syn_chord(60, CH_POWER, 0, 0, 0, 0, c) == 3 && c[2] == 72 && syn_chord(60, CH_OFF, 3, 2, 0, 0, c) == 1 && c[0] == 60);
    CHECK(syn_chord(62, CH_SCALE3, 0, 0, SCL_MAJOR, 0, c) == 3 && c[0] == 62 && c[1] == 65 && c[2] == 69);     /* D minor in C */
    CHECK(syn_chord(67, CH_SCALE4, 0, 0, SCL_MAJOR, 0, c) == 4 && c[1] == 71 && c[3] == 77);                  /* G7 in C */
    CHECK(syn_chord(125, CH_MAJ7, 0, 0, 0, 0, c) == 1);  /* (out of range dropped) */
    syn.scale = SCL_CHROM; syn.root = 0;
    /* settings migration: a version 2 store gets the defaults of the new fields */
    {
        syn_set_t t = syn;
        t.ver = 2; t.scale = 9; t.mut_amt = 77; t.song = 1;
        syn_set_fix(&t, 1);
        CHECK(t.ver == SYN_SET_VER && t.scale == 0 && t.mut_amt == 1 && t.song == 0 && t.bpm10 == syn.bpm10);
        t.chord = 99; t.inv = 7;
        syn_set_fix(&t, 0);
        CHECK(t.chord == 0 && t.inv == 0);
    }
    /* locks: lanes are taken by the first lock of a parameter and freed with its last; six at most */
    sq_defaults();
    CHECK(sq_lock_get(0, 3, LK_BRIGHT) == -1000 && sq_lanes_used(0) == 0);
    CHECK(sq_lock_set(0, 3, LK_BRIGHT, -20) == 0 && sq_lock_get(0, 3, LK_BRIGHT) == -20 && sq_lanes_used(0) == 1);
    CHECK(sq_lock_set(0, 5, LK_BRIGHT, 99) == 0 && sq_lock_get(0, 5, LK_BRIGHT) == 63);     /* clamped */
    for (i = 0, ok = 0; i < 5u; i++)
        ok += sq_lock_set(0, 0, LK_DRIVE + i, 10) == 0;
    CHECK(ok == 5 && sq_lanes_used(0) == 6 && sq_lock_set(0, 1, LK_ALG, 3) < 0);
    sq_lock_del(0, 0, LK_DRIVE);
    CHECK(sq_lanes_used(0) == 5 && sq_lock_set(0, 1, LK_ALG, 3) == 0);
    CHECK(sq_step_locks(0, 3) == 1u << LK_BRIGHT && sq_step_locks(0, 0) == (0x1Fu << LK_DRIVE & ~(1u << LK_DRIVE)));
    sq_lock_clear_step(0, 0);
    CHECK(sq_step_locks(0, 0) == 0 && sq_lanes_used(0) == 2);
    sq_copy(0, 9);
    CHECK(sq_lock_get(9, 3, LK_BRIGHT) == -20);
    sq_clear(9);
    CHECK(sq_lock_get(9, 3, LK_BRIGHT) == -1000);
    /* the store: version 2 keeps locks and the chain; a version 1 object loads its patterns, without locks */
    {
        static sq_obj_t o2, o1;
        sq_chain_set(0, 2, 3); sq_chain_set(1, 4, 1);
        sq_obj_fill(0, &o2);
        sq_defaults();
        CHECK(sq_obj_take(0, &o2, sizeof o2) == 0 && sq_lock_get(0, 3, LK_BRIGHT) == -20 && sq_ch.len == 2 && sq_ch.pat[0] == 2 &&
              sq_ch.rep[0] == 3);
        CHECK(sq_obj_take(0, &o2, sizeof o2 - 2u) < 0);
        o1 = o2;
        o1.ver = 1;
        sq_pat[1].len = 5;
        CHECK(sq_obj_take(0, &o1, SQ_OBJ_V1_SIZE) == 0 && sq_pat[1].len == 16 && sq_lock_get(0, 3, LK_BRIGHT) == -1000 &&
              sq_lanes_used(0) == 0 && sq_ch.len == 2);    /* (the chain is not in a version 1 object: kept) */
        o2.lk[0].id[0] = 40;                               /* a damaged lane goes */
        CHECK(sq_obj_take(0, &o2, sizeof o2) == 0 && sq_lanes_used(0) == 1);
        printf("  seq store v2: %u bytes an object (v1 %u), locks %u B and chain %u B in RAM\n", (unsigned)sizeof(sq_obj_t),
               (unsigned)SQ_OBJ_V1_SIZE, (unsigned)sizeof sq_lk, (unsigned)sizeof sq_ch);
    }
    /* playback: a locked step puts its locks on, a plain one takes them off, a TIE adds its own, stop clears them */
    sq_defaults();
    memset(&sq_ch, 0, sizeof sq_ch);
    syn.rev_mix = 0; syn.dly_mix = 0; syn.chorus = 0; syn.arp_on = 0; syn.song = 0; syn.bpm10 = 1200;
    for (i = 0; i < 16u; i++) sq_pat[0].s[i].f = 0;
    sq_pat[0].s[0].f = SQ_ON | 1u; sq_pat[0].s[0].note[0] = 60;
    sq_pat[0].s[1].f = SQ_ON | SQ_TIE;
    sq_pat[0].s[4].f = SQ_ON | 1u; sq_pat[0].s[4].note[0] = 62;
    sq_pat[0].s[8].f = 0;                                 /* (off: changes nothing) */
    sq_lock_set(0, 0, LK_ALG, 30);
    sq_lock_set(0, 1, LK_BRIGHT, 40);
    sq_lock_set(0, 8, LK_LEVEL, 10);
    syn.seq_pat = 0;
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_seq_start(1);
    run(BLK * 2);
    CHECK(syn_lk.mask == 1u << LK_ALG && syn_lk.v[LK_ALG] == 30 && syn_eff_alg == 30);
    while (sq.step < 2u) run(BLK);
    CHECK(syn_lk.mask == (1u << LK_ALG | 1u << LK_BRIGHT) && syn_lk.v[LK_BRIGHT] == 40);
    while (sq.step < 5u) run(BLK);
    CHECK(syn_lk.mask == 0 && syn_eff_alg == (syn.cur[FP_ALG] & 31u));
    while (sq.step < 9u) run(BLK);
    CHECK(syn_lk.mask == 0);                              /* (a step that plays nothing) */
    syn_seq_start(0);
    sq_lock_set(0, 4, LK_DRIVE, 50);
    syn_seq_start(1);
    while (sq.step < 5u) run(BLK);
    CHECK(syn_lk.mask == 1u << LK_DRIVE && fxv.drive == 50 && syn.drive == 0);
    syn_seq_start(0);
    run(BLK);
    CHECK(syn_lk.mask == 0 && fxv.drive == 0);
    /* the song: P2 twice, P3 once, again from the start */
    sq_defaults();
    for (i = 0; i < SQ_PATS; i++) { sq_pat[i].len = 4; sq_pat[i].s[0].f = SQ_ON | 1u; sq_pat[i].s[0].note[0] = 60; }
    sq_chain_set(0, 1, 2); sq_chain_set(1, 2, 1);
    syn.song = 1; syn.seq_pat = 9;
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_seq_start(1);
    {
        uint8_t seen[8], ns = 0, last = 255;
        uint32_t lp = 0;
        while (ns < 8u && pn < 44100u * 30u) {
            run(BLK);
            if (sq.loops != lp || last == 255) {
                lp = sq.loops;
                seen[ns++] = sq.pat;
                last = sq.pat;
            }
        }
        CHECK(ns == 8 && seen[0] == 1 && seen[1] == 1 && seen[2] == 2 && seen[3] == 1 && seen[4] == 1 && seen[5] == 2 && seen[6] == 1);
        printf("  song: P%u P%u P%u P%u P%u P%u (chain P2 x2, P3)\n", seen[0] + 1, seen[1] + 1, seen[2] + 1, seen[3] + 1, seen[4] + 1, seen[5] + 1);
    }
    syn.song = 0;                                         /* off while playing: the selected pattern from the next loop */
    {
        uint32_t lp = sq.loops;
        while (sq.loops < lp + 2u) run(BLK);
        CHECK(sq.pat == 9 && !sq.in_song);
    }
    syn_seq_start(0);
    run(BLK);
    sq_chain_insert(0);
    CHECK(sq_ch.len == 3 && sq_ch.pat[1] == 1 && sq_ch.rep[1] == 2);
    sq_chain_delete(0);
    sq_chain_delete(5);
    CHECK(sq_ch.len == 2 && sq_ch.pat[1] == 2);
    /* mutate: always audible, carriers loud, ratios on the grid; something changes */
    {
        uint32_t seed = 1234u, quiet = 0, offgrid = 0, unchanged = 0, trials = 0;
        double minpk = 1.0;
        for (k = 0; k < SYN_NFACTORY; k++)
            for (n = 0; n < 3u; n++)
                for (i = 0; i < 12u; i++) {
                    uint8_t orig[FP_SIZE + 1];
                    uint32_t chg, j, car;
                    double pk;
                    syn_load_patch(k);
                    memcpy(orig, syn.cur, sizeof orig);
                    chg = syn_mutate(syn.cur, n, &seed);
                    trials++;
                    unchanged += chg == 0;
                    car = fm6_carriers(syn.cur[FP_ALG] & 31u);
                    for (j = 0; j < 6u; j++) {
                        const uint8_t *op = syn.cur + (5u - j) * FP_OP, *oo = orig + (5u - j) * FP_OP;
                        if ((car >> (5u - j)) & 1u)
                            quiet += op[FP_OL] < 75u;
                        if (!op[FP_MODE] && (op[FP_FC] != oo[FP_FC] || op[FP_FF] != oo[FP_FF]) && op[FP_FF] != 0u && op[FP_FF] != 50u)
                            offgrid++;
                    }
                    syn_reset_sound(); syn_dirty = 1; pn = 0;
                    syn_event(1, 60, 100, 0);
                    run(44100 / 4);
                    syn_event(2, 60, 0, 0);
                    pk = peak_since(0, pn, 0);
                    if (pk < minpk) minpk = pk;
                }
        printf("  mutate: %u voices, softest peak %.3f, %u unchanged\n", trials, minpk, unchanged);
        CHECK(quiet == 0 && offgrid == 0 && minpk > 0.01 && unchanged < trials / 10u);
    }
    syn = before;
    syn_dirty = 1;
    syn_reset_sound();
    sq_defaults();
    memset(&sq, 0, sizeof sq);
}

/* ---------------------------------------------------------------- the host voice (protocol 1.3) --- */
static void host_voice_tests(void)
{
    static int32_t a[2 * 44100], b[2 * 44100];
    syn_set_t hd, before = syn;
    uint32_t i, diff = 0, live = 0;
    int ok60 = 0, ok64 = 0;
    /* the host voice's defaults leave the sounding settings alone */
    syn_host_defaults(&hd);
    CHECK(!memcmp(&before, &syn, sizeof syn) && hd.at_depth == 32 && !hd.arp_on && hd.patch_idx == 0 && hd.level == 80);
    /* pressure: no effect at depth 0 (bit-exact), a different sound at depth 63 */
    syn.arp_on = 0;
    syn.at_depth = 0;
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_event(1, 60, 100, 0); syn_event(10, 127, 0, 0);
    run(44100 / 2);
    memcpy(a, pcm, sizeof a);
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_event(1, 60, 100, 0);
    run(44100 / 2);
    CHECK(!memcmp(a, pcm, 2u * (44100 / 2) * sizeof pcm[0]));
    syn.at_depth = 63;
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_event(1, 60, 100, 0); syn_event(10, 127, 0, 0);
    run(44100 / 2);
    memcpy(b, pcm, sizeof b);
    for (i = 0; i < 2u * (44100 / 2); i++)
        diff += a[i] != b[i];
    CHECK(diff > 1000u);
    /* poly pressure reaches its own note's voice only */
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_event(1, 60, 100, 0); syn_event(1, 64, 100, 0); syn_event(9, 60, 100, 0);
    run(BLK * 2);
    for (i = 0; i < SYN_VOICES; i++) {
        if (syn_v[i].n.live && syn_v[i].note == 60) ok60 = syn_v[i].at == 100;
        if (syn_v[i].n.live && syn_v[i].note == 64) ok64 = syn_v[i].at == 0;
    }
    CHECK(ok60 && ok64);
    /* held: silence, the events wait; let go: they play */
    syn_reset_sound(); syn_dirty = 1; pn = 0;
    syn_hold = 1;
    syn_event(1, 67, 100, 0);
    run(BLK * 4);
    CHECK(peak_since(0, pn, 0) == 0.0 && syn_evh != syn_evt);
    syn_hold = 0;
    run(44100 / 4);
    CHECK(peak_since(BLK * 4, pn, 0) > 0.01);
    /* the swap: the two sets trade places, the sound starts from nothing */
    syn.level = 55; syn_alt = syn; syn_alt.level = 77; syn_alt.at_depth = 9;
    syn_swap();
    for (i = 0; i < SYN_VOICES; i++)
        live += syn_v[i].n.live;
    CHECK(syn.level == 77 && syn.at_depth == 9 && syn_alt.level == 55 && !live && syn_evh == syn_evt && !syn_hold);
    for (i = 0, diff = 0; i < DLY_N; i++)
        diff += fx_dly[0][i] != 0;
    CHECK(diff == 0);
    syn_swap();
    CHECK(syn.level == 55 && syn_alt.level == 77);
    syn = before;
    syn_dirty = 1;
    syn_reset_sound();
    printf("  host voice: pressure, poly pressure, hold, swap ok\n");
}

/* ------------------------------------------------------- trig conditions, generators, phrase tools (synth_gen.c) --- */
static uint32_t played_loops(uint32_t pat, uint32_t step, uint32_t loops, uint32_t *mask)   /* bit l: step played in loop l */
{
    uint32_t l;
    *mask = 0;
    syn.seq_pat = (uint8_t)pat;
    syn_seq_start(1);
    for (l = 0; l < loops; l++) {
        uint32_t guard = 0;
        until_step(step, 44100 * 4);
        if (sq.n_notes && sq.step == step)
            *mask |= 1u << l;
        while (sq.step == step && guard++ < 2000)            /* past it */
            syn_render(pcm, FM6_N, 4096);
    }
    syn_seq_start(0);
    return *mask;
}
static void cond_gen_tests(void)
{
    sq_pat_t *p = &sq_pat[2];
    uint32_t i, m, full, hits;
    char d[32];
    static sq_obj_t ob;
    syn_event(6, 0, 0, 0);
    run_small(256);
    sq_pat_init(p);
    sq_lanes_init(&sq_lk[2]);
    p->len = 4;
    for (i = 0; i < 4; i++) { p->s[i].note[0] = (uint8_t)(60 + i); p->s[i].f = SQ_ON | 1u; }
    /* conditions: step 2 1:2 (loops 0, 2), 2:2 (1, 3), 1:4 (0, 4), FIRST (0), FILL / NOT FILL */
    sq_set_cond(&p->s[1], SQK_1OF2);
    CHECK(sq_cond(&p->s[1]) == SQK_1OF2 && sq_n(&p->s[1]) == 1 && sq_plays(&p->s[1]));
    CHECK(played_loops(2, 1, 4, &m) == 0x5u);
    sq_set_cond(&p->s[1], SQK_2OF2);
    CHECK(played_loops(2, 1, 4, &m) == 0xAu);
    sq_set_cond(&p->s[1], SQK_1OF4);
    CHECK(played_loops(2, 1, 6, &m) == 0x11u);
    sq_set_cond(&p->s[1], SQK_FIRST);
    CHECK(played_loops(2, 1, 3, &m) == 0x1u);
    sq_set_cond(&p->s[1], SQK_FILL);
    sq.fill = 0;
    CHECK(played_loops(2, 1, 2, &m) == 0u);
    sq.fill = 1;
    CHECK(played_loops(2, 1, 2, &m) == 0x3u);
    sq_set_cond(&p->s[1], SQK_NOFILL);
    CHECK(played_loops(2, 1, 2, &m) == 0u);
    sq.fill = 0;
    CHECK(played_loops(2, 1, 2, &m) == 0x3u);
    /* kept in the store (spare bits of the flags: no layout change), and an editing of the notes keeps it */
    sq_obj_fill(0, &ob);
    sq_set_cond(&p->s[1], SQK_ALWAYS);
    CHECK(sq_obj_take(0, &ob, sizeof ob) == 0 && sq_cond(&sq_pat[2].s[1]) == SQK_NOFILL);
    ob.p[2].s[1].f |= SQ_CMASK;                            /* 7: no such condition: loads as ALWAYS */
    CHECK(sq_obj_take(0, &ob, sizeof ob) == 0 && sq_cond(&sq_pat[2].s[1]) == SQK_ALWAYS && sq_n(&sq_pat[2].s[1]) == 1);
    sq_set_cond(&p->s[1], SQK_FILL);
    syn.seq_pat = 2;
    sq_ed_step_down(1, 0); sq_ed_note(50); sq_ed_step_up();
    CHECK(sq_cond(&p->s[1]) == SQK_FILL && p->s[1].note[0] == 50);
    sq_set_cond(&p->s[1], SQK_ALWAYS);

    /* EUCLID: 3 hits over 8, rotated; hits keep their notes, the others are switched off with theirs kept */
    sq_pat_init(p);
    p->len = 8;
    p->s[3].note[0] = 70; p->s[3].f = 1u;
    sq_ed.last_note = 62;
    syn.scale = SCL_CHROM;
    sgg.gen = GEN_EUCLID; sgg.par[GEN_EUCLID][0] = 3; sgg.par[GEN_EUCLID][1] = 0; sgg.par[GEN_EUCLID][2] = 0;
    sg_generate(2, d);
    CHECK(!strcmp(d, "EUCLID 3/8"));
    CHECK(sq_plays(&p->s[0]) && sq_plays(&p->s[3]) && sq_plays(&p->s[6]) && sq_count_on(p) == 3);
    CHECK(p->s[0].note[0] == 62 && p->s[3].note[0] == 70);
    sgg.par[GEN_EUCLID][2] = 1;
    sg_generate(2, d);
    CHECK(sq_plays(&p->s[1]) && sq_plays(&p->s[4]) && sq_plays(&p->s[7]) && sq_count_on(p) == 3 && sq_has(&p->s[3]));
    for (hits = 0; hits <= 8; hits++) {                   /* E(k, n): exactly k hits, as evenly as can be */
        sgg.par[GEN_EUCLID][0] = (uint8_t)hits; sgg.par[GEN_EUCLID][2] = 0;
        sg_generate(2, d);
        CHECK(sq_count_on(p) == hits);
    }
    /* undo: one level, again = redo */
    CHECK(sg_undo() == 1 && sq_count_on(p) == 7);
    CHECK(sg_undo() == 0 && sq_count_on(p) == 8);
    /* SPARSE / DRIFT / ARP / PULSE in D dorian: every note in the scale; the same seed the same pattern */
    syn.scale = SCL_DORIAN; syn.root = 2; sq_ed.last_note = 62;
    p->len = 32;
    for (i = 0; i < 5; i++) {
        static const uint8_t G[5] = {GEN_SPARSE, GEN_DRIFT, GEN_ARP, GEN_PULSE, GEN_SPARSE};
        uint32_t k, sum = 0, n = 0, out = 0, sum2 = 0;
        sgg.gen = G[i];
        sg_generate(2, d);
        for (k = 0; k < 32; k++)
            if (sq_n(&p->s[k])) { n++; sum += p->s[k].note[0] * (k + 1); out += !syn_in_scale(p->s[k].note[0], SCL_DORIAN, 2); }
        CHECK(n > 0 && out == 0);
        sg_generate(2, d);
        for (k = 0; k < 32; k++)
            if (sq_n(&p->s[k])) sum2 += p->s[k].note[0] * (k + 1);
        CHECK(sum == sum2);
    }
    sgg.gen = GEN_PULSE; sgg.par[GEN_PULSE][0] = 4; sgg.par[GEN_PULSE][1] = 1; sgg.par[GEN_PULSE][2] = 3;
    sg_generate(2, d);
    CHECK(sq_n(&p->s[1]) == 1 && sq_is_tie(&p->s[2]) && sq_is_tie(&p->s[3]) && !sq_has(&p->s[4]) && sq_n(&p->s[5]) == 1);
    sgg.gen = GEN_ARP; sgg.par[GEN_ARP][0] = 0; sgg.par[GEN_ARP][1] = 1; sgg.par[GEN_ARP][2] = 1;
    syn.chord = CH_OFF;
    sg_generate(2, d);                                     /* D dorian's seventh chord on D: D F A C, up */
    CHECK(p->s[0].note[0] == 62 && p->s[1].note[0] == 65 && p->s[2].note[0] == 69 && p->s[3].note[0] == 72 && p->s[4].note[0] == 62);
    syn.scale = SCL_CHROM; syn.root = 0;

    /* phrase tools: rotate (locks with their steps), transpose (scale steps), reverse by phrases, mirror */
    sq_pat_init(p);
    sq_lanes_init(&sq_lk[2]);
    p->len = 8;
    p->s[0].note[0] = 60; p->s[0].f = SQ_ON | 1u;
    p->s[1].f = SQ_ON | SQ_TIE;
    p->s[4].note[0] = 67; p->s[4].f = SQ_ON | 1u;
    CHECK(sq_lock_set(2, 4, LK_BRIGHT, 20) == 0);
    sg_rotate(2, 1);
    CHECK(p->s[1].note[0] == 60 && sq_is_tie(&p->s[2]) && p->s[5].note[0] == 67 && sq_lock_get(2, 5, LK_BRIGHT) == 20 &&
          sq_lock_get(2, 4, LK_BRIGHT) == -1000);
    sg_rotate(2, -1);
    CHECK(p->s[0].note[0] == 60 && sq_lock_get(2, 4, LK_BRIGHT) == 20);
    sg_reverse(2);                                         /* runs: [0,1] [2] [3] [4] [5] [6] [7] -> 7 6 5 4 3 2 [0,1] */
    CHECK(p->s[3].note[0] == 67 && sq_lock_get(2, 3, LK_BRIGHT) == 20 && p->s[6].note[0] == 60 && sq_is_tie(&p->s[7]));
    sg_reverse(2);
    CHECK(p->s[0].note[0] == 60 && sq_is_tie(&p->s[1]) && p->s[4].note[0] == 67);
    CHECK(sg_transpose(2, 2) == 0 && p->s[0].note[0] == 62 && p->s[4].note[0] == 69);
    syn.scale = SCL_MAJOR;
    CHECK(sg_transpose(2, 1) == 0 && p->s[0].note[0] == 64 && p->s[4].note[0] == 71);   /* D -> E, A -> B in C major */
    CHECK(sg_transpose(2, 100) == -1 && p->s[0].note[0] == 64);
    sg_mirror(2);
    CHECK(p->s[0].note[0] == 71 && p->s[4].note[0] == 64);
    syn.scale = SCL_CHROM;
    /* copy steps 1..2 (a note and its TIE, with locks) and paste them at 6 */
    CHECK(sq_lock_set(2, 0, LK_DRIVE, 50) == 0);
    sg_copy(2, 0, 1);
    CHECK(sgc.n == 2 && sg_paste(2, 6, &full) == 2 && full == 0);
    CHECK(p->s[6].note[0] == 71 && sq_is_tie(&p->s[7]) && sq_lock_get(2, 6, LK_DRIVE) == 50);
    CHECK(sg_paste(2, 7, &full) == 1);                     /* cut at the length */
    CHECK(sg_undo() == 1 && sq_is_tie(&p->s[7]));
    sq_pat_init(p);
    sq_lanes_init(&sq_lk[2]);
    syn.seq_pat = 0;
    printf("  seq tools: conditions, 5 generators, rotate / transpose / reverse / mirror, copy / paste, undo ok\n");
}

int main(int argc, char **argv)
{
    double rms, pk, rms2;
    uint32_t t0, i;
    struct timespec c0, c1;
    syn_init();
    /* patch formats: pack / unpack is lossless on every factory voice, names are readable */
    for (i = 0; i < SYN_NFACTORY; i++) {
        uint8_t v[FP_SIZE + 1], p[128];
        char nm[11];
        CHECK(syn_load_patch(i) == 0);
        fp_pack(syn.cur, p);
        fp_unpack(p, v);
        CHECK(!memcmp(v, syn.cur, FP_SIZE) && !memcmp(p, FM6_FACTORY[i], 128));
        syn_patch_name(i, nm);
        CHECK(nm[0] > ' ');
        if (i == 0) printf("  patch 0: %s alg %d\n", nm, syn.cur[FP_ALG] + 1);
    }
    CHECK(syn_load_patch(8) == -1);                     /* an empty user slot refuses */

    /* one note: sounds, then releases to silence */
    syn_load_patch(0);
    t0 = pn;
    syn_event(1, 60, 100, 0);
    run(44100);
    pk = peak_since(t0 + 4410, t0 + 44100, &rms);
    printf("  note C4 patch 0: peak %.3f rms %.3f\n", pk, rms);
    CHECK(pk > 0.02 && pk < 1.0);
    syn_event(2, 60, 0, 0);
    run(44100 * 3);
    CHECK(peak_since(pn - 4410, pn, 0) < 0.0005 || syn.rev_mix);                     /* (a reverb tail may remain) */
    syn.rev_mix = 0; syn.chorus = 0; syn.dly_mix = 0;
    run(44100);
    CHECK(peak_since(pn - 4410, pn, 0) < 0.0003);

    /* every factory patch sounds at a sane level, a chord of 8 does not clip */
    for (i = 0; i < SYN_NFACTORY; i++) {
        syn_load_patch(i);
        syn_event(6, 0, 0, 0);
        t0 = pn;
        syn_event(1, 48, 110, 0); syn_event(1, 52, 110, 0); syn_event(1, 55, 110, 0); syn_event(1, 60, 110, 0);
        run(22050);
        pk = peak_since(t0 + 2205, pn, &rms);
        printf("  patch %u chord of 4: peak %.3f rms %.3f\n", i, pk, rms);
        CHECK(pk > 0.005 && pk < 0.99);
        syn_event(3, 0, 0, 0);
        run(22050);
    }
    /* polyphony: 12 notes on 8 voices: the oldest are stolen, nothing breaks, the voices come back */
    syn_load_patch(0);
    for (i = 0; i < 12; i++) syn_event(1, (uint8_t)(40 + i * 3), 100, 0);
    run(8192);
    CHECK(syn_active_voices <= SYN_VOICES && syn_active_voices >= 6);
    printf("  12 notes: %u voices sounding (max %d), peak since start %u\n", syn_active_voices, SYN_VOICES, syn_voices_peak);
    syn_event(3, 0, 0, 0);
    run(44100 * 4);
    CHECK(syn_active_voices == 0);

    /* DX7 sysex: a 32-voice bank and a single voice */
    {
        static uint8_t bank[6 + 4096 + 2];
        uint8_t one[6 + 155 + 2];
        uint32_t k;
        bank[0] = 0xF0; bank[1] = 0x43; bank[2] = 0x00; bank[3] = 0x09; bank[4] = 0x20; bank[5] = 0x00;
        for (k = 0; k < 32; k++) { memcpy(bank + 6 + k * 128, FM6_FACTORY[k % 8], 128); bank[6 + k * 128 + 127] = 0x20; bank[6 + k * 128 + 118] = (uint8_t)('A' + k % 26); }
        bank[6 + 4096] = cksum(bank + 6, 4096);
        bank[6 + 4097] = 0xF7;
        feed(bank, sizeof bank);
        CHECK(syn_imp_done == 32 && syn_bank[0].used == 0xFFFFu && syn_bank[1].used == 0xFFFFu);
        CHECK(syn_load_patch(8 + 31) == 0 && syn.cur[FP_NAME] == 'A' + 31 % 26);
        syn_imp_done = 0;
        bank[6 + 4096] ^= 1;                              /* a bad checksum imports nothing */
        syn_bank[0].used = 0;
        feed(bank, sizeof bank);
        CHECK(syn_imp_done == 0 && syn_bank[0].used == 0);
        /* single voice */
        one[0] = 0xF0; one[1] = 0x43; one[2] = 0x00; one[3] = 0x00; one[4] = 0x01; one[5] = 0x1B;
        syn_load_patch(1);
        memcpy(one + 6, syn.cur, 155);
        one[6 + 155] = cksum(one + 6, 155);
        one[6 + 156] = 0xF7;
        syn_imp_slot = 3;
        feed(one, sizeof one);
        CHECK(syn_imp_done == 1 && syn_load_patch(8 + 3) == 0 && 1);
        /* another manufacturer's SysEx and a cut frame import nothing */
        syn_imp_done = 0;
        { static const uint8_t other[] = {0xF0, 0x7D, 0x52, 0x34, 0x01, 0xF7}; feed(other, sizeof other); }
        feed(bank, 100);
        syn_sysex_byte(0xF0); syn_sysex_byte(0xF7);
        CHECK(syn_imp_done == 0);
        /* the stored slots persist the patch in packed form: store then reload gives the same patch */
        syn_load_patch(2);
        { uint8_t keep[FP_SIZE]; memcpy(keep, syn.cur, FP_SIZE);
          CHECK(syn_store_patch(20) == 0 && syn_load_patch(1) == 0 && syn_load_patch(20) == 0 && !memcmp(keep, syn.cur, FP_SIZE)); }
    }

    /* fx tails: a short note through chorus, delay and reverb leaves a tail; the dry one does not */
    syn_load_patch(0);
    syn.rev_mix = 60; syn.rev_type = REV_HALL; syn.rev_size = 80; syn.dly_mix = 50; syn.dly_div = 2; syn.dly_fb = 50; syn.chorus = 40;
    syn_event(6, 0, 0, 0);
    t0 = pn;
    syn_event(1, 57, 110, 0);
    run(8000);
    syn_event(2, 57, 0, 0);
    run(44100 * 3);
    pk = peak_since(t0 + 44100 * 2, pn, &rms);
    printf("  fx tail 2..3 s after note-off (hall reverb + delay + chorus): peak %.4f rms %.5f\n", pk, rms);
    CHECK(pk > 0.0005);
    wav(argc > 1 ? "fx_tail.wav" : "/dev/null", t0, pn);

    /* arpeggio and sequencer produce notes without keys */
    syn.rev_mix = 0; syn.dly_mix = 0; syn.chorus = 0;
    syn_event(6, 0, 0, 0);
    syn.arp_on = 1; syn.arp_mode = ARP_UP; syn.arp_rate = 1; syn.arp_oct = 2;
    syn_event(1, 48, 100, 0); syn_event(1, 55, 100, 0);
    t0 = pn;
    run(44100 * 2);
    CHECK(peak_since(t0, pn, 0) > 0.01);
    syn_event(3, 0, 0, 0); run(44100 * 3);
    syn.arp_on = 0;
    syn_seq_start(1);
    t0 = pn;
    run(44100 * 2);
    CHECK(peak_since(t0, pn, 0) > 0.01 && sq.ticks / 24u > 8);
    printf("  seq: %u steps in 2 s at %u.%u BPM (%u sounded)\n", (unsigned)(sq.ticks / 24u), syn.bpm10 / 10, syn.bpm10 % 10,
           (unsigned)sq.played);
    syn_seq_start(0);
    run(44100 * 2);
    seq_tests();

    step2_tests();
    host_voice_tests();
    cond_gen_tests();

    /* CPU: 8 voices held on patch 0 (the factory patch with the most operators sounding), all fx on */
    syn_load_patch(0);
    syn.rev_mix = 30; syn.dly_mix = 30; syn.chorus = 30;
    for (i = 0; i < 8; i++) syn_event(1, (uint8_t)(40 + i * 4), 100, 0);
    pn = 0;
    run(BLK * 4);
    pn = 0;
    clock_gettime(CLOCK_MONOTONIC, &c0);
    run(44100 * 4);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    {
        double s = (c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) / 1e9;
        printf("  CPU: 8 voices + fx: %.2f%% of real time on this Mac (%u voices sounding)\n", s / 4.0 * 100.0, syn_active_voices);
    }
    CHECK(syn_active_voices >= 6);
    if (argc > 1) {
        char path[512];
        snprintf(path, sizeof path, "%s/synth_chord.wav", argv[1]);
        pn = 0;
        syn_event(6, 0, 0, 0); syn_load_patch(0);
        syn_event(1, 48, 100, 0); syn_event(1, 55, 100, 0); syn_event(1, 60, 100, 0);
        run(44100 * 3);
        wav(path, 0, pn);
    }
    printf("synth: %d checks, %d failed\n", checks, fails);
    (void)rms2;
    return fails ? 1 : 0;
}
