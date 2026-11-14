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
    CHECK(peak_since(t0, pn, 0) > 0.01 && syn_sq.seq_count > 8);
    printf("  seq: %u steps in 2 s at %u.%u BPM\n", (unsigned)syn_sq.seq_count, syn.bpm10 / 10, syn.bpm10 % 10);
    syn_seq_start(0);
    run(44100 * 2);

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
