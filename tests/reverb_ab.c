/* SPDX-License-Identifier: GPL-3.0-only
 * The reverb A/B (part of tests/synth_test.c): Synth mode's old reverb (tests/reverb_ref.c, four combs and two
 * allpasses a channel) against the Dattorro plate (src/plate.c) as synth.c drives it (syn_plate_cfg). The same input
 * through both: an impulse (the impulse response: decay time, decay smoothness, echo density, ringing) and a phrase of
 * the synth (the listening files, and the wet level); and each one's time on this Mac. */
#include "reverb_ref.c"

#define AB_SR 44100
#define AB_IR (AB_SR * 4)
static float ab_l[AB_IR], ab_r[AB_IR];

/* the impulse response of each, left channel: old (type, size) / plate (type, size), the input 0.5 at frame 0 */
static void ab_ir_old(uint32_t type, int size, float *h, uint32_t n)
{
    uint32_t i;
    ref_clear();
    for (i = 0; i < n; i++) {
        int32_t x = i == 0 ? 4194304 : 0, wl, wr;
        ref_rev(x, x, type, size, 0, &wl, &wr);
        h[i] = (float)wl / 8388608.0f;
    }
}
static void ab_ir_plate(uint32_t type, int size, float *h, uint32_t n)
{
    static float in[FM6_N], ol[FM6_N], or_[FM6_N];
    uint32_t i, k;
    syn_plate_cfg(type, size, 0);
    plate_clear();
    for (i = 0; i < n; i += FM6_N) {
        for (k = 0; k < FM6_N; k++)
            in[k] = i + k == 0 ? 0.5f * SYN_PL_IN : 0.0f;
        plate_run(in, ol, or_, FM6_N);
        for (k = 0; k < FM6_N && i + k < n; k++)
            h[i + k] = ol[k] * syn_pl_out[type];
    }
}

/* the decay time (T30 from the Schroeder integral, s); the smoothness of the decay (the RMS of 10 ms energies about a
 * straight line between -5 and -40 dB, dB); the echo density (Abel & Huang's normalized echo density over 20 ms
 * windows over the same stretch of the decay: 1 is noise-like, under 1 sparse, over 1 tonal; and when it first
 * reaches 0.9, ms); the ringing (the late tail's spectrum, 100 ms
 * on: per octave 250 Hz .. 8 kHz, the peak bin over the median bin, dB, averaged) */
typedef struct { double t30 /* T60, s */, rough, ned, mix_ms, ring; } ab_m_t;
static double ab_fr[8192], ab_fi[8192];
static void ab_fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2.0 * M_PI / m;
        for (k = 0; k < n; k += m)
            for (j = 0; j < m / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi, xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
                re[k + j + m / 2] = re[k + j] - xr; im[k + j + m / 2] = im[k + j] - xi;
                re[k + j] += xr; im[k + j] += xi;
            }
    }
}
static int ab_cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static ab_m_t ab_measure(const float *h, uint32_t n)
{
    ab_m_t m = {0, 0, 0, -1, 0};
    uint32_t i, t5 = 0, t40 = 0, f, nf = 0;
    {   /* the decay time from the 10 ms energies' slope, from 5 dB under their peak down to 40 dB under it (or 10 dB
         * over the noise floor, the last 0.5 s, if that comes first: the old reverb hums on at its last bits) */
        double e, pk = -300, fl = 0, xs = 0, ys = 0, xx = 0, xy = 0, c = 0, sl;
        uint32_t k, nk = n / 441u, k0 = 0, k1 = 0, kf = 0;
        for (i = n - AB_SR / 2u; i < n; i++) fl += (double)h[i] * h[i];
        fl = 10.0 * log10(fl / (AB_SR / 2u) + 1e-30);
        for (k = 0; k < nk; k++) {
            for (e = 0, i = k * 441u; i < (k + 1) * 441u; i++) e += (double)h[i] * h[i];
            e = 10.0 * log10(e / 441.0 + 1e-30);
            if (e > pk) pk = e, k0 = k;
        }
        for (k = k0; k < nk; k++) {
            for (e = 0, i = k * 441u; i < (k + 1) * 441u; i++) e += (double)h[i] * h[i];
            e = 10.0 * log10(e / 441.0 + 1e-30);
            if (e > pk - 5.0) continue;
            if (e < pk - 40.0 || e < fl + 10.0) break;
            xs += k; ys += e; xx += (double)k * k; xy += k * e; c++;
            if (!kf) kf = k;
            k1 = k;
        }
        sl = c > 2 ? (c * xy - xs * ys) / (c * xx - xs * xs) : -1e9;     /* dB a frame */
        m.t30 = sl < 0 ? -60.0 / sl * 0.01 : 99.0;
        t5 = kf * 441u;
        t40 = (k1 + 1u) * 441u;
    }
    {   /* the decay's roughness: 10 ms energies against their line */
        double xs = 0, ys = 0, xx = 0, xy = 0, e[1000], a, b, r = 0;
        uint32_t k0 = t5 / 441u, k1 = (t40 ? t40 : n) / 441u, k;
        for (k = k0; k < k1 && k < 1000u; k++) {
            double acc = 0;
            for (i = k * 441u; i < (k + 1) * 441u && i < n; i++) acc += (double)h[i] * h[i];
            e[k] = 10.0 * log10(acc / 441.0 + 1e-30);
            xs += k; ys += e[k]; xx += (double)k * k; xy += k * e[k]; nf++;
        }
        b = (nf * xy - xs * ys) / (nf * xx - xs * xs);
        a = (ys - b * xs) / nf;
        for (k = k0; k < k1 && k < 1000u; k++) r += (e[k] - a - b * k) * (e[k] - a - b * k);
        m.rough = nf ? sqrt(r / nf) : 0;
    }
    {   /* echo density: windows of 20 ms, every 1 ms */
        double acc = 0;
        uint32_t c = 0;
        for (f = t5 > 441u ? t5 - 441u : 0; f + 882u < n && f + 441u < t40; f += 44u) {
            double sg = 0;
            uint32_t out = 0;
            for (i = f; i < f + 882u; i++) sg += (double)h[i] * h[i];
            sg = sqrt(sg / 882.0);
            for (i = f; i < f + 882u; i++) out += fabs(h[i]) > sg;
            {
                double ned = out / 882.0 / 0.3173, ms = (f + 441u) * 1000.0 / AB_SR;
                if (m.mix_ms < 0 && ned >= 0.9) m.mix_ms = ms;
                acc += ned; c++;
            }
        }
        m.ned = c ? acc / c : 0;             /* (1: as dense as noise; under 1 sparse, over 1 tonal) */
    }
    {   /* ringing: the tail's spectrum from 100 ms, per octave the peak over the median */
        static const double EDGE[6] = {250, 500, 1000, 2000, 4000, 8000};
        static double band[4096];
        uint32_t o0 = AB_SR / 10u, b;
        double acc = 0;
        for (i = 0; i < 8192u; i++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / 8191.0);
            ab_fr[i] = (o0 + i < n ? h[o0 + i] : 0) * w;
            ab_fi[i] = 0;
        }
        ab_fft(ab_fr, ab_fi, 8192);
        for (b = 0; b < 5u; b++) {
            uint32_t lo = (uint32_t)(EDGE[b] * 8192 / AB_SR), hi = (uint32_t)(EDGE[b + 1] * 8192 / AB_SR), k, c = 0;
            double pk = 0;
            for (k = lo; k < hi; k++) {
                double p = ab_fr[k] * ab_fr[k] + ab_fi[k] * ab_fi[k];
                band[c++] = p;
                if (p > pk) pk = p;
            }
            qsort(band, c, sizeof band[0], ab_cmp);
            acc += 10.0 * log10(pk / (band[c / 2] + 1e-30));
        }
        m.ring = acc / 5.0;
    }
    return m;
}

/* each one's time: 4 s of noise through it, as the render feeds it (sub-blocks of 32) */
static double ab_cpu_old(uint32_t type)
{
    struct timespec c0, c1;
    uint32_t i, r = 1;
    int32_t wl, wr, sink = 0;
    ref_clear();
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for (i = 0; i < AB_SR * 4u; i++) {
        int32_t x;
        r = r * 1664525u + 1013904223u;
        x = (int32_t)(r >> 8) - 8388608;
        ref_rev(x >> 2, x >> 2, type, 50, 0, &wl, &wr);
        sink += wl ^ wr;
    }
    clock_gettime(CLOCK_MONOTONIC, &c1);
    if (sink == 12345) printf("-");
    return ((c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) / 1e9) / 4.0 * 100.0;
}
static double ab_cpu_plate(uint32_t type, int light)
{
    struct timespec c0, c1;
    static float in[FM6_N], ol[FM6_N], or_[FM6_N];
    uint32_t i, k, r = 1;
    float sink = 0;
    syn_plate_cfg(type, 50, light);
    plate_clear();
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for (i = 0; i < AB_SR * 4u; i += FM6_N) {
        for (k = 0; k < FM6_N; k++) {
            r = r * 1664525u + 1013904223u;
            in[k] = (float)((int32_t)(r >> 8) - 8388608) * (0.25f / 8388608.0f) * SYN_PL_IN;
        }
        plate_run(in, ol, or_, FM6_N);
        sink += ol[3] + or_[7];
    }
    clock_gettime(CLOCK_MONOTONIC, &c1);
    if (sink == 12345.0f) printf("-");
    return ((c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) / 1e9) / 4.0 * 100.0;
}

/* a stereo WAV (16 bit) of dry + wet */
static void ab_wav(const char *path, const float *l, const float *r, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, v;
    uint8_t hd[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0, 0x44, 0xAC, 0, 0,
                      0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    if (!f) return;
    v = 36 + n * 4; memcpy(hd + 4, &v, 4); v = n * 4; memcpy(hd + 40, &v, 4);
    fwrite(hd, 1, 44, f);
    for (i = 0; i < n; i++) {
        float s[2] = {l[i], r[i]};
        int k;
        for (k = 0; k < 2; k++) {
            float x = s[k] * 32767.0f;
            int16_t o = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            fwrite(&o, 2, 1, f);
        }
    }
    fclose(f);
}

static const char *const AB_OLD_NAME[3] = {"ROOM", "SPRING", "HALL"}, *const AB_PL_NAME[3] = {"ROOM", "PLATE", "HALL"};
static void reverb_ab(const char *dir)
{
    static float h[AB_IR], dry[AB_SR * 6], ol[AB_SR * 6], or_[AB_SR * 6];
    uint32_t ty, i, n = AB_SR * 6u;
    double cpu_old[3], cpu_pl[3], cpu_light;
    ab_m_t mo[3], mp[3];
    printf("  reverb A/B (old: 4 combs + 2 allpasses a side; plate: Dattorro), size 50, impulse response, left (0.9 at -1: never):\n");
    for (ty = 0; ty < 3u; ty++) {
        ab_ir_old(ty, 50, h, AB_IR);
        mo[ty] = ab_measure(h, AB_IR);
        if (dir && ty == REV_HALL) { char p[512]; snprintf(p, sizeof p, "%s/ir_old_hall.wav", dir); ab_wav(p, h, h, AB_SR * 3u); }
        ab_ir_plate(ty, 50, h, AB_IR);
        mp[ty] = ab_measure(h, AB_IR);
        if (dir && ty == REV_HALL) { char p[512]; snprintf(p, sizeof p, "%s/ir_plate_hall.wav", dir); ab_wav(p, h, h, AB_SR * 3u); }
        cpu_old[ty] = ab_cpu_old(ty);
        cpu_pl[ty] = ab_cpu_plate(ty, 0);
        printf("    %-6s old:   T60 %.2f s  rough %.2f dB  density %.2f (0.9 at %4.0f ms)  ringing %.1f dB  cpu %.3f%%\n",
               AB_OLD_NAME[ty], mo[ty].t30, mo[ty].rough, mo[ty].ned, mo[ty].mix_ms, mo[ty].ring, cpu_old[ty]);
        printf("    %-6s plate: T60 %.2f s  rough %.2f dB  density %.2f (0.9 at %4.0f ms)  ringing %.1f dB  cpu %.3f%%\n",
               AB_PL_NAME[ty], mp[ty].t30, mp[ty].rough, mp[ty].ned, mp[ty].mix_ms, mp[ty].ring, cpu_pl[ty]);
    }
    cpu_light = ab_cpu_plate(REV_HALL, 1);
    printf("    plate light (the overload guard's step 1): cpu %.3f%%\n", cpu_light);
    /* the phrase: patch 0, dry, a run of staccato notes and a chord, then 2.5 s for the tails */
    syn_load_patch(0);
    syn.rev_mix = 0; syn.dly_mix = 0; syn.chorus = 0; syn.arp_on = 0;
    syn_event(6, 0, 0, 0);
    run(BLK * 8);
    pn = 0;
    {
        static const uint8_t NT[6] = {60, 64, 67, 72, 67, 64};
        for (i = 0; i < 6; i++) { syn_event(1, NT[i], 100, 0); run(5120); syn_event(2, NT[i], 0, 0); run(2560); }
        syn_event(1, 48, 100, 0); syn_event(1, 55, 100, 0); syn_event(1, 64, 100, 0);
        run(26624);
        syn_event(3, 0, 0, 0);
        run(n - pn);
    }
    for (i = 0; i < n; i++) dry[i] = (float)pcm[2 * i] / 8388608.0f;
    {
        double e_old[3], e_pl[3];
        for (ty = 0; ty < 3u; ty++) {
            static float in[FM6_N], wl[FM6_N], wr[FM6_N];
            uint32_t k;
            double so = 0, sp = 0;
            ref_clear();
            for (i = 0; i < n; i++) {
                int32_t x = (int32_t)(dry[i] * 8388608.0f), a, b;
                ref_rev(x, x, ty, 50, 0, &a, &b);
                ol[i] = dry[i] + (float)a / 8388608.0f * 50.0f / 60.0f;
                or_[i] = dry[i] + (float)b / 8388608.0f * 50.0f / 60.0f;
                so += ((double)a * a + (double)b * b) / 7.0e13;
            }
            e_old[ty] = so;
            if (dir && ty == REV_HALL) { char p[512]; snprintf(p, sizeof p, "%s/reverb_old_hall.wav", dir); ab_wav(p, ol, or_, n); }
            syn_plate_cfg(ty, 50, 0);
            plate_clear();
            for (i = 0; i < n; i += FM6_N) {
                for (k = 0; k < FM6_N; k++) in[k] = dry[i + k] * SYN_PL_IN;
                plate_run(in, wl, wr, FM6_N);
                for (k = 0; k < FM6_N; k++) {
                    float a = wl[k] * syn_pl_out[ty], b = wr[k] * syn_pl_out[ty];
                    ol[i + k] = dry[i + k] + a * 50.0f / 60.0f;
                    or_[i + k] = dry[i + k] + b * 50.0f / 60.0f;
                    sp += ((double)a * a + (double)b * b) * 8388608.0 * 8388608.0 / 7.0e13;
                }
            }
            e_pl[ty] = sp;
            if (dir && ty == REV_HALL) { char p[512]; snprintf(p, sizeof p, "%s/reverb_plate_hall.wav", dir); ab_wav(p, ol, or_, n); }
            printf("    %-6s wet level of the phrase: plate %+.1f dB against the old\n", AB_PL_NAME[ty],
                   10.0 * log10(e_pl[ty] / e_old[ty]));
            CHECK(fabs(10.0 * log10(e_pl[ty] / e_old[ty])) < 3.0);       /* REVERB keeps its level */
        }
    }
    /* the verdict, as numbers: denser and smoother, less ringing, decay times in the old ones' range */
    for (ty = 0; ty < 3u; ty++) {
        CHECK(mp[ty].rough < mo[ty].rough + 0.5);
        CHECK(mp[ty].ring < mo[ty].ring - 3.0);
    }
    CHECK(mp[REV_HALL].t30 > 0.75 * mo[REV_HALL].t30 && mp[REV_HALL].t30 < 1.5 * mo[REV_HALL].t30);
    CHECK(mp[REV_ROOM].t30 < mp[REV_HALL].t30);
    CHECK(fabs(1.0 - mp[REV_HALL].ned) < fabs(1.0 - mo[REV_HALL].ned) && fabs(1.0 - mp[REV_ROOM].ned) < fabs(1.0 - mo[REV_ROOM].ned));
    {   /* SIZE still means short .. long */
        static float h2[AB_IR];
        ab_m_t a, b;
        ab_ir_plate(REV_HALL, 0, h2, AB_IR); a = ab_measure(h2, AB_IR);
        ab_ir_plate(REV_HALL, 100, h2, AB_IR); b = ab_measure(h2, AB_IR);
        printf("    HALL plate decay: SIZE 0 %.2f s, 100 %.2f s\n", a.t30, b.t30);
        CHECK(a.t30 < 1.0 && b.t30 > 2.5);
    }
    CHECK(cpu_light < cpu_pl[REV_HALL]);
}
