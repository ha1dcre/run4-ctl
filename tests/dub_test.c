/* SPDX-License-Identifier: GPL-3.0-only
 * Native tests of DUB mode's engine (src/dub/dub.c, DUB-1's, with the shared plate src/plate.c and the mode arena):
 * every piece plays at a sane level (not silent, not clipped), rests to silence, dub hold keeps the echo, the master
 * actions and the effects grid stay sane, PERFORM's mutes and moves, MIDI clock in, what MIDI OUT says, the piece's ping
 * loops kept and given back, the overload guard's steps lighten it; its RAM and its time on this Mac. With a directory,
 * a listening file: dub_demo.wav (8 s: Basic, the master filter shut and opened, Dub up, a dub hold).
 *   cc tests/dub_test.c -> build/dub_test [wav_out_dir] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/dub/dub.c"
#include "../src/plate.c"

uint8_t r4_arena[R4_ARENA_BYTES] __attribute__((aligned(4)));
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t m_notes, m_clock, m_start, m_stop, m_cc;
void du_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
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
static uint32_t frames, bad, hot;
static FILE *wav;
static uint32_t wav_frames;
static void run(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i += 256) {
        du_render(buf, 256, 4096);
        for (k = 0; k < 512; k++) {
            double v = buf[k] / 8388608.0;
            if (v != v) bad++;
            if (fabs(v) > pk) pk = fabs(v);
            if (fabs(v) > 0.97) hot++;       /* (the soft clip's top: 0.989) */
            ss += v * v;
        }
        if (wav) {
            int16_t s[512];
            for (k = 0; k < 512; k++)
                s[k] = (int16_t)(buf[k] >> 8);
            fwrite(s, 2, 512, wav);
            wav_frames += 256;
        }
        frames += 256;
    }
}
static void meter_reset(void) { pk = ss = 0; frames = 0; hot = 0; }
static double rms(void) { return frames ? sqrt(ss / (2.0 * frames)) : 0; }
static double db(double x) { return 20.0 * log10(x + 1e-12); }

static void piece(int i)
{
    int16_t par[DP_NPARAMS];
    uint16_t pool;
    int p;
    du_piece(i, par, &pool);
    for (p = 0; p < DP_NPARAMS; p++)
        if (p != DP_TUNE && p != DP_MIDI)
            du_set(p, par[p]);
    du_pool(pool);
    du_pings(0);
    du_regrow();
}
static double cpu_pct(uint32_t secs)
{
    struct timespec c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &c0);
    run(secs * SEC);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    return ((c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) / 1e9) / secs * 100.0;
}
static void wav_open(const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/dub_demo.wav", dir);
    wav = fopen(path, "wb");
    if (wav)
        fseek(wav, 44, SEEK_SET);
}
static void wav_put32(uint32_t v) { fputc((int)(v & 255u), wav); fputc((int)(v >> 8 & 255u), wav); fputc((int)(v >> 16 & 255u), wav); fputc((int)(v >> 24), wav); }
static void wav_close(void)
{
    uint32_t data = wav_frames * 4u;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); wav_put32(36u + data); fwrite("WAVEfmt ", 1, 8, wav);
    wav_put32(16); wav_put32(1u | 2u << 16); wav_put32(SEC); wav_put32(SEC * 4u); wav_put32(4u | 16u << 16);
    fwrite("data", 1, 4, wav); wav_put32(data);
    fclose(wav);
    wav = 0;
}

int main(int argc, char **argv)
{
    int i, k;
    double c0, c4;
    printf("  RAM: the mode arena %u B (DUB %u: tape %u, Echoplex %u, cloud %u, sine and mix %u; AMB %u; Synth %u), the plate %u B\n",
           (unsigned)R4_ARENA_BYTES, (unsigned)R4_DUB_BYTES, (unsigned)(R4_DUB_TAPE_N * 2u), (unsigned)(R4_DUB_PLEX_N * 2u),
           (unsigned)(R4_DUB_CL_N * 2u), (unsigned)R4_DUB_OFF_TAPE, (unsigned)R4_AMB_BYTES, (unsigned)R4_SYN_BYTES, plate_bytes());
    CHECK(R4_DUB_BYTES <= R4_ARENA_BYTES && R4_DUB_OFF_MIX % 4u == 0 && R4_DUB_OFF_TAPE % 4u == 0 && R4_DUB_OFF_CL % 2u == 0);
    CHECK(R4_DUB_BYTES - R4_AMB_BYTES < 16384u);  /* (what DUB adds to AMB's arena: the cloud's line, less the smaller mix) */
    du_init();
    du_enter();
    /* the pure parts */
    CHECK(du_euclid(4, 16, 0) == 0x1111u && __builtin_popcount(du_euclid(3, 8, 0)) == 3);
    {
        int pcs[4], n = du_chord_tones(DU_MODE_POOL[DM_DORIAN], 5, 0, pcs), iv[4];
        char nm[16];
        CHECK(n == 4 && pcs[0] == 5 && pcs[1] == 8 && pcs[2] == 0 && pcs[3] == 3);
        CHECK(!strcmp(du_chord_name(pcs, n, nm), "Fm7"));
        du_voicing(DU_MODE_POOL[DM_DORIAN], 0, 0, iv);   /* the open 7th: root, 5th, 7th, 10th */
        CHECK(iv[0] == 0 && iv[1] == 7 && iv[2] == 10 && iv[3] == 15);
        CHECK(!strcmp(du_chord_label(5, DU_CQ_MINOR, DU_CX_7, nm), "Fm7") && !strcmp(du_chord_label(0, DU_CQ_SUS4, DU_CX_7, nm), "C7sus4"));
        CHECK(du_kick_note(5) == 29 && du_kick_note(2) == 33);
    }
    /* every piece: plays, sounds, never clips hard, nothing goes NaN */
    for (i = 0; i < DU_NPIECES; i++) {
        piece(i);
        du_seed((uint32_t)i * 977u + 1u);
        du_play(1);
        run(SEC * 2);
        meter_reset();
        run(SEC * 10);
        printf("  piece %d %-11s: peak %.3f rms %.4f (%.1f dBFS), %.3f%% of samples near the top\n", i, du_piece_name(i),
               pk, rms(), db(rms()), 100.0 * hot / (2.0 * frames));
        CHECK(bad == 0 && pk < 0.99 && rms() > 0.005 && hot < frames / 500u);
        du_play(0);
        run(SEC);
    }
    /* resting: everything rings out to silence (the echo's feedback eases under 90% once stopped) */
    piece(0);
    du_play(1);
    run(SEC * 4);
    du_play(0);
    run(SEC * 60);
    meter_reset();
    run(SEC);
    printf("  rest: 60 s after PLAY (the tape echo's repeats), peak %.6f\n", pk);
    CHECK(pk < 0.003);
    /* dub hold: the echo plays what it holds for ever; let go, it rings out */
    du_play(1);
    run(SEC * 6);
    du_freeze(1);
    run(SEC);
    du_play(0);
    run(SEC * 12);
    meter_reset();
    run(SEC);
    printf("  dub hold: 12 s after it rested, rms %.5f\n", rms());
    CHECK(du_frozen && rms() > 0.001);
    du_freeze(0);
    run(SEC * 60);
    meter_reset();
    run(SEC);
    CHECK(pk < 0.003);
    /* the master actions, each to either end: sane; Energy far down: the next phrases Thin */
    piece(0);
    du_play(1);
    run(SEC * 2);
    for (i = 0; i < DU_NMAC; i++)
        for (k = -50; k <= 50; k += 100) {
            du_macro(i, k);
            meter_reset();
            run(SEC * 3);
            CHECK(bad == 0 && pk < 0.99 && hot < frames / 200u);
            du_macro(i, 0);
        }
    du_macro(DU_MAC_DUB, 50);
    run(SEC / 4);
    printf("  Dub +100: the echo's input %.2f, the repeats keep %.2f\n", du_echo_in, du_echo_g);
    CHECK(du_echo_in > 0.45f);
    du_macro(DU_MAC_DUB, 0);
    du_macro(DU_MAC_ENERGY, -50);
    run(SEC * 34);                               /* (past a phrase's end: eight bars, 15.7 s at 122) */
    CHECK(du_section == DSEC_THIN);
    du_macro(DU_MAC_ENERGY, 0);
    /* the effects grid: each effect held a second, then let go: sane */
    for (i = 0; i < DU_NFX; i++) {
        du_fx(1u << i);
        meter_reset();
        run(SEC);
        du_fx(0);
        run(SEC / 2);
        if (bad || pk >= 0.99 || hot > frames / 100u)
            printf("  effect %s: peak %.3f, %u hot\n", DU_FX_NAME[i], pk, hot);
        CHECK(bad == 0 && pk < 0.99 && hot <= frames / 100u);
    }
    /* PERFORM: everything muted is quieter than all of it; Throw throws */
    meter_reset();
    run(SEC * 4);
    {
        double all = rms();
        uint32_t t0;
        du_mute(0x1Fu);
        run(SEC * 6);
        meter_reset();
        run(SEC * 2);
        printf("  PERFORM: rms %.4f with everything, %.4f all muted\n", all, rms());
        CHECK(rms() < all * 0.7);
        du_mute(0);
        t0 = du_throws;
        du_perf(DU_PF_THROW);
        run(SEC * 4);
        du_perf(0);
        CHECK(du_throws > t0);
    }
    /* the piece's ping loops: taken, and given back as they were after a regrow */
    {
        uint8_t a[DU_PINGS_N], b[DU_PINGS_N];
        du_pings_get(a);
        du_pings(a);
        du_regrow();
        run(256);
        du_pings_get(b);
        CHECK(!memcmp(a, b, sizeof a));
    }
    /* MIDI out: off sends nothing; clock: clock, start, stop; notes: the notes too */
    m_notes = m_clock = m_start = m_stop = 0;
    du_play(0); run(SEC / 4);
    du_set(DP_MIDI, 0);
    du_play(1); run(SEC * 3); du_play(0); run(SEC / 4);
    CHECK(m_notes == 0 && m_clock == 0 && m_start == 0);
    du_set(DP_MIDI, 1);
    du_play(1); run(SEC * 3); du_play(0); run(SEC / 4);
    printf("  MIDI OUT CLOCK: %u clocks in 3 s at %d BPM, start %u, stop %u, notes %u\n", m_clock, par[DP_TEMPO], m_start, m_stop, m_notes);
    CHECK(m_notes == 0 && m_clock > 3u * 24u * (unsigned)par[DP_TEMPO] / 60u - 4u && m_start == 1 && m_stop == 1);
    du_set(DP_MIDI, 2);
    du_play(1); run(SEC * 4); du_play(0); run(SEC / 4);
    CHECK(m_notes > 0);
    du_set(DP_MIDI, 0);
    /* MIDI clock in: START plays from the top, 24 ticks are a beat (four sixteenths), STOP rests; then its own tempo */
    {
        du_clock_isr(DU_CLK_START);
        CHECK(du_playing && du_ext);
        for (k = 0; k < 96; k++) {
            du_clock_isr(DU_CLK_TICK);
            run(256);
        }
        printf("  clock in: 96 ticks -> %u sixteenths\n", du_bars);
        CHECK(du_bars == 16u);
        du_clock_isr(DU_CLK_STOP);
        CHECK(!du_playing);
        run(SEC);
        CHECK(!du_ext);
    }
    /* the overload guard: each step costs less; the sound stays */
    piece(7);                                    /* Haze: the most cloud and echo */
    du_set(DP_CLOUD, 100); du_set(DP_PINGS, 80); du_set(DP_DENSITY, 90); du_set(DP_CRACKLE, 60);
    du_play(1);
    run(SEC * 3);
    du_shed = 0;
    c0 = cpu_pct(4);
    printf("  CPU on this Mac: %.2f%% of real time (step 0)", c0);
    du_shed = 2;
    printf(", step 2 %.2f%%", cpu_pct(4));
    du_shed = 4;
    run(SEC);
    meter_reset();
    c4 = cpu_pct(4);
    printf(", step 4 %.2f%%\n", c4);
    CHECK(rms() > 0.002 && !bell[4].on && !bell[5].on && cl_live == 0);   /* (the times only printed: a busy Mac is noisy) */
    du_shed = 0;
    du_play(0);
    run(SEC * 2);
    /* the listening file, 8 s: Basic; the filter shut and opened, Dub up, a dub hold, rest */
    if (argc > 1) {
        piece(0);
        du_seed(12345u);
        run(SEC * 30);                           /* (the tails of what went before gone) */
        wav_open(argv[1]);
        meter_reset();
        du_play(1);
        run(SEC * 3);
        du_macro(DU_MAC_FILTER, -40); run(SEC);
        du_macro(DU_MAC_FILTER, 40); run(SEC);
        du_macro(DU_MAC_FILTER, 0); du_macro(DU_MAC_DUB, 50); run(SEC);
        du_freeze(1); run(SEC); du_freeze(0);
        du_macro(DU_MAC_DUB, 0); run(SEC / 2);
        du_play(0); run(SEC / 2);
        printf("  dub_demo.wav: %.1f s, peak %.3f (%.1f dBFS), rms %.1f dBFS, %u samples near the top\n",
               wav_frames / (double)SEC, pk, db(pk), db(rms()), hot);
        CHECK(bad == 0 && pk < 0.99 && rms() > 0.01 && hot < frames / 1000u);
        wav_close();
    }
    printf("dub: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
