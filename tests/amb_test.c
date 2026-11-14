/* SPDX-License-Identifier: GPL-3.0-only
 * Native tests of AMB mode's engine (src/amb/amb.c, AMB-1's, with the shared plate src/plate.c and the mode arena):
 * every piece sounds at a sane level, rests to silence, freezes, follows a MIDI clock, sends what MIDI OUT says; the
 * overload guard's steps lighten it; its RAM and its time on this Mac.
 *   cc tests/amb_test.c -> build/amb_test [wav_out_dir] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/amb/amb.c"
#include "../src/plate.c"

uint8_t r4_arena[R4_ARENA_BYTES] __attribute__((aligned(4)));
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t m_notes, m_clock, m_start, m_stop, m_cc;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1; (void)d2;
    if (st == 0xF8u) m_clock++;
    else if (st == 0xFAu || st == 0xFBu) m_start++;
    else if (st == 0xFCu) m_stop++;
    else if ((st & 0xF0u) == 0x90u) m_notes++;
    else if ((st & 0xF0u) == 0xB0u) m_cc++;
}

#define SEC 44100u
static int32_t buf[2 * 256];
static double pk, ss;
static uint32_t frames, bad;
static void run(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i += 256) {
        en_render(buf, 256, 4096);
        for (k = 0; k < 512; k++) {
            double v = buf[k] / 8388608.0;
            if (v != v) bad++;
            if (fabs(v) > pk) pk = fabs(v);
            ss += v * v;
        }
        frames += 256;
    }
}
static void meter_reset(void) { pk = ss = 0; frames = 0; }
static double rms(void) { return frames ? sqrt(ss / (2.0 * frames)) : 0; }
static double db(double x) { return 20.0 * log10(x + 1e-12); }

static void piece(int i)
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    int p;
    en_piece(i, par, &pool);
    for (p = 0; p < P_NPARAMS; p++)
        if (p != P_TUNE && p != P_MIDI)
            en_set(p, par[p]);
    en_pool(pool);
    en_regrow();
}
static double cpu_pct(uint32_t secs)
{
    struct timespec c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &c0);
    run(secs * SEC);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    return ((c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) / 1e9) / secs * 100.0;
}

int main(int argc, char **argv)
{
    int i, k;
    double c0, c4;
    (void)argv;
    printf("  RAM: the mode arena %u B (AMB %u: tape %u, Echoplex %u, sine and mix %u; Synth %u), the plate %u B\n",
           (unsigned)R4_ARENA_BYTES, (unsigned)R4_AMB_BYTES, (unsigned)(R4_AMB_TAPE_N * 2u), (unsigned)(R4_AMB_PLEX_N * 2u),
           (unsigned)R4_AMB_OFF_TAPE, (unsigned)R4_SYN_BYTES, plate_bytes());
    CHECK(R4_AMB_OFF_PLEX + R4_AMB_PLEX_N * 2u <= R4_ARENA_BYTES && R4_AMB_OFF_MIX % 4u == 0 && R4_AMB_OFF_TAPE % 4u == 0);
    en_init();
    en_enter();
    /* the pure parts */
    CHECK(en_euclid(4, 16, 0) == 0x1111u && __builtin_popcount(en_euclid(3, 8, 0)) == 3 && __builtin_popcount(en_euclid(5, 12, 2)) == 5);
    {
        int pcs[4], n = en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 0, pcs);
        char nm[12];
        CHECK(n == 4 && pcs[0] == 2 && pcs[1] == 5 && pcs[2] == 9 && pcs[3] == 0);
        CHECK(!strcmp(en_chord_name(pcs, n, nm), "Dm7"));
    }
    /* every piece: grows, sounds, never clips hard, nothing goes NaN */
    for (i = 0; i < EN_NPIECES; i++) {
        piece(i);
        en_seed((uint32_t)i * 977u + 1u);
        en_play(1);
        run(SEC * 2);
        meter_reset();
        run(SEC * 10);
        printf("  piece %d %-10s: peak %.3f rms %.4f (%.1f dBFS)\n", i, en_piece_name(i), pk, rms(), db(rms()));
        CHECK(bad == 0 && pk < 1.0 && rms() > 0.002);
        en_play(0);
        run(SEC);
    }
    /* resting: everything rings out to silence */
    piece(0);
    en_play(1);
    run(SEC * 4);
    en_play(0);
    run(SEC * 40);
    meter_reset();
    run(SEC);
    printf("  rest: 40 s after PLAY (the tape echo's repeats), peak %.6f\n", pk);
    CHECK(pk < 0.003);
    /* freeze: the plate rings on; thawed, it lets go */
    en_play(1);
    run(SEC * 6);
    en_freeze(1);
    run(SEC);
    en_play(0);                                  /* (resting: only the frozen plate is left) */
    run(SEC * 12);
    meter_reset();
    run(SEC);
    printf("  frozen: 12 s after the garden rested, rms %.5f\n", rms());
    CHECK(rms() > 0.0005);
    en_freeze(0);
    run(SEC * 30);
    meter_reset();
    run(SEC);
    CHECK(pk < 0.002);
    /* MIDI out: off sends nothing; clock: clock, start, stop; notes: the notes too */
    m_notes = m_clock = m_start = m_stop = 0;
    en_set(P_MIDI, 0);
    en_play(1); run(SEC * 3); en_play(0); run(SEC / 4);
    CHECK(m_notes == 0 && m_clock == 0 && m_start == 0);
    en_set(P_MIDI, 1);
    en_play(1); run(SEC * 3); en_play(0); run(SEC / 4);
    printf("  MIDI OUT CLOCK: %u clocks in 3 s at %d BPM, start %u, stop %u, notes %u\n", m_clock, par[P_TEMPO], m_start, m_stop, m_notes);
    CHECK(m_notes == 0 && m_clock > 3u * 24u * (unsigned)par[P_TEMPO] / 60u - 4u && m_start == 1 && m_stop == 1);
    en_set(P_MIDI, 2);
    en_play(1); run(SEC * 6); en_play(0); run(SEC / 4);
    CHECK(m_notes > 0);
    en_set(P_MIDI, 0);
    /* MIDI clock in: START grows, ticks drive the grid, STOP rests; when it stops coming, the own tempo again */
    {
        uint32_t s0;
        en_clock_isr(EN_CLK_START);
        CHECK(en_playing && en_ext);
        s0 = 0;                                  /* (START: from the top) */
        for (k = 0; k < 96; k++) {                /* 4 beats of 24 ticks */
            en_clock_isr(EN_CLK_TICK);
            run(256);
        }
        printf("  clock in: 96 ticks -> %u sixteenths\n", en_bars - s0);
        CHECK(en_bars - s0 == 16u);
        en_clock_isr(EN_CLK_STOP);
        CHECK(!en_playing);
        run(SEC);
        CHECK(!en_ext);
    }
    /* the overload guard: each step costs less; the sound stays */
    piece(2);                                    /* Voices: the dearest pad */
    en_set(P_PULSE, 60); en_set(P_HITS, 70); en_set(P_BELLS, 60); en_set(P_DENSITY, 80);
    en_play(1);
    run(SEC * 3);
    en_shed = 0;
    c0 = cpu_pct(4);
    en_shed = 2;
    printf("  CPU on this Mac: %.2f%% of real time (step 0)", c0);
    printf(", step 2 %.2f%%", cpu_pct(4));
    en_shed = 4;
    run(SEC);
    meter_reset();
    c4 = cpu_pct(4);
    printf(", step 4 %.2f%%\n", c4);
    CHECK(rms() > 0.001);                         /* (the times only printed: a busy Mac makes them noisy) */
    CHECK(pad[2].amp < 1e-4f && pad[3].amp < 1e-4f && !bell[4].on && !bell[5].on && !drum[1].on && !drum[2].on);
    en_shed = 0;
    en_play(0);
    run(SEC * 2);
    (void)argc;
    printf("amb: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
