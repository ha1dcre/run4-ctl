/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1, https://github.com/charlesvestal/fm1-amb, app/dsp/amb.c: "reverb:
 * Dattorro's plate", from FoMni-1)
 * Modified for RUN4 CTL by ha1dcre, 2026. Changes from AMB-1's code: a unit of its own (fm1.toml o2) with a small API
 * (plate.h) so Synth mode and AMB mode share one plate and one buffer; the delay lines hold int16 (PLATE_Q) instead
 * of float, rounded, which halves their RAM (69,412 B); the predelay, the damping, the bandwidth, the decay and decay
 * diffusion 2 are settings instead of AMB-1's constants and its control rate; a "light" setting for RUN4 CTL's
 * overload guard bypasses the four input diffusers. The tank, its taps and its modulation are AMB-1's. */
/* J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45 (9), 1997: a predelay, a bandwidth
 * low-pass, four input diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
 * low-pass, an allpass and a delay, feeding the other half; stereo out from seven taps a side. The paper's lengths are
 * for 29761 Hz; DS() scales them to 44.1 kHz. */
#include "plate.h"
#include "fastmath.h"

#if defined PLAT_HOST || defined R4_NATIVE
#define PL_POOL
#else
#define PL_POOL __attribute__((section(".pool")))
#endif

#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N PLATE_PRE_MAX
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static int16_t pl_buf[DL_TOTAL] PL_POOL;
typedef struct {
    int16_t *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static int pl_ready;
static float pl_bw_s, pl_damp_l, pl_damp_r, lfo_s, lfo_c = 1.0f;
static plate_cfg_t pl = {0.5f, 0.55f, 0.6f, 0.5f, 0, 0};

#define INV_Q (1.0f / PLATE_Q)
static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return (float)d->b[j < 0 ? j + d->n : j] * INV_Q;
}
static inline void push(dl_t *d, float x)
{
    float v = x * PLATE_Q;
    v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
    d->b[d->i] = (int16_t)v;                 /* toward zero: no limit cycle hums on at the last bit when it rings out */
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, v);
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z0 = tap(d, k), z = z0 + (tap(d, k + 1) - z0) * f, v = x - g * z;
    push(d, v);
    return z + g * v;
}

static void plate_init(void)
{
    int16_t *p = pl_buf;
    int k;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    pl_ready = 1;
}
void plate_clear(void)
{
    uint32_t i;
    if (!pl_ready)
        plate_init();
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0;
    for (i = 0; i < L_N; i++)
        dl[i].i = 0;
    pl_bw_s = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}
void plate_set(const plate_cfg_t *c)
{
    pl = *c;
    pl.decay = fm_clampf(pl.decay, 0.0f, 0.9995f);
    pl.damp = fm_clampf(pl.damp, 0.02f, 1.0f);
    pl.bw = fm_clampf(pl.bw, 0.02f, 1.0f);
    pl.dd2 = fm_clampf(pl.dd2, 0.0f, 0.7f);
    if (pl.pre > PRE_N)
        pl.pre = PRE_N;
}
uint32_t plate_bytes(void) { return (uint32_t)sizeof pl_buf; }

void plate_run(const float *in, float *outl, float *outr, uint32_t n)
{
    const float decay = pl.decay, damp = pl.damp, bw = pl.bw, dd2 = pl.dd2;
    const int pre = (int)pl.pre, light = pl.light;
    uint32_t j;
    if (!pl_ready)
        plate_clear();
    for (j = 0; j < n; j++) {
        float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
        push(&dl[L_PRE], in[j]);
        x = pre ? tap(&dl[L_PRE], pre) : in[j];
        pl_bw_s += bw * (x - pl_bw_s);
        if (light) {
            x = pl_bw_s;
        } else {
            x = allpass(&dl[L_IN1], DS(142), pl_bw_s, 0.75f);
            x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
            x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
            x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
        }
        {
            float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
            lfo_s = s;
            lfo_c = c;
        }
        a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + decay * fb_l, -0.7f);
        push(&dl[L_D1L], a);
        b = tap(&dl[L_D1L], DS(4453));
        pl_damp_l += damp * (b - pl_damp_l);
        b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * decay, dd2);
        push(&dl[L_D2L], b);
        a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + decay * fb_r, -0.7f);
        push(&dl[L_D1R], a);
        b = tap(&dl[L_D1R], DS(4217));
        pl_damp_r += damp * (b - pl_damp_r);
        b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * decay, dd2);
        push(&dl[L_D2R], b);
        outl[j] = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                          tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                          tap(&dl[L_D2L], DS(1066)));
        outr[j] = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                          tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                          tap(&dl[L_D2R], DS(121)));
    }
    pl_bw_s = fm_flush(pl_bw_s);
    pl_damp_l = fm_flush(pl_damp_l);
    pl_damp_r = fm_flush(pl_damp_r);
    {   /* (the rotation's length drifts with rounding: kept at 1) */
        float m = lfo_s * lfo_s + lfo_c * lfo_c;
        if (m < 0.98f || m > 1.02f) {
            float k = 1.0f / fm_sqrtf(m);
            lfo_s *= k;
            lfo_c *= k;
        }
    }
}

/* --------------------------------------------------------------- the pipe --- */
/* (plate.h) The ring holds the plate's output by absolute frame (pp_out: frames written), the render's position
 * pp_pos (frames put in). The job is the only writer of the plate's state and the ring's next frames while it runs;
 * the first core reads the ring only at frames older than what the job writes (lag and 2 x lag back) */
static float pp_l[PLATE_PIPE_N] PL_POOL, pp_r[PLATE_PIPE_N] PL_POOL, pp_in[PLATE_PIPE_MAXN] PL_POOL;
static volatile uint32_t pp_in_n, pp_out, pp_pos, pp_lag = 128u, pp_jobs2;
static volatile uint8_t pp_out_c2;                /* the job in flight is the second core's */
static uint8_t pp_held, pp_was_held;              /* plate_pipe_hold: no plate (its frames silent), and was */
static int (*pp_run)(void (*fn)(uint32_t), uint32_t arg);
static int (*pp_wait)(void);
#define PP_M (PLATE_PIPE_N - 1u)
void plate_offload(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void))
{
    pp_run = run;
    pp_wait = wait;
}
uint32_t plate_pipe_jobs2(void) { return pp_jobs2; }
static void pp_job(uint32_t n)
{
    static float wl[PLATE_PIPE_MAXN], wr[PLATE_PIPE_MAXN];   /* (the job's own: the first core never touches them) */
    uint32_t j, w = pp_out;
    plate_run(pp_in, wl, wr, n);
    for (j = 0; j < n; j++) {
        pp_l[(w + j) & PP_M] = wl[j];
        pp_r[(w + j) & PP_M] = wr[j];
    }
}
void plate_pipe_reset(uint32_t lag)
{
    uint32_t i;
    for (i = 0; i < PLATE_PIPE_N; i++)
        pp_l[i] = pp_r[i] = 0.0f;
    pp_in_n = 0;
    pp_out = pp_pos = 0;
    pp_lag = lag > PLATE_PIPE_MAXN ? PLATE_PIPE_MAXN : lag < 1u ? 1u : lag;
    pp_out_c2 = 0;
    plate_clear();
}
int plate_pipe_offloaded(void) { return pp_run != 0; }
void plate_pipe_hold(int on) { pp_held = (uint8_t)(on != 0); }
void plate_pipe_go(void)
{
    uint32_t n = pp_in_n, j;
    pp_out_c2 = 0;
    if (!n)
        return;
    if (pp_held) {                              /* (no plate: its frames silent; the lines cleared when it comes back) */
        for (j = 0; j < n; j++)
            pp_l[(pp_out + j) & PP_M] = pp_r[(pp_out + j) & PP_M] = 0.0f;
        pp_out += n;
        pp_in_n = 0;
        pp_was_held = 1;
        return;
    }
    if (pp_was_held) {
        pp_was_held = 0;
        plate_clear();
    }
    if (pp_run && pp_run(pp_job, n)) {
        pp_out_c2 = 1;
        return;
    }
    pp_job(n);                                  /* (one core: here, now) */
    pp_out += n;
    pp_in_n = 0;
}
void plate_pipe_wait(void)
{
    if (!pp_out_c2)
        return;
    pp_out_c2 = 0;
    if (!pp_wait || pp_wait() != 0) {           /* (it gave up and dropped the job: do it here; the plate's state may be
                                                 * half on: it was the job's only) */
        pp_job(pp_in_n);
    } else {
        pp_jobs2++;
    }
    pp_out += pp_in_n;
    pp_in_n = 0;
}
void plate_pipe_put(const float *in, uint32_t n)
{
    uint32_t j;
    n = n > PLATE_PIPE_MAXN ? PLATE_PIPE_MAXN : n;
    for (j = 0; j < n; j++)
        pp_in[j] = in[j];
    pp_in_n = n;
    pp_pos += n;
}
/* frame p of the render (pp_pos the chunk's first: plate_pipe_put has not counted it yet) */
static float pp_at(const float *b, uint32_t j, uint32_t back)
{
    uint32_t p = pp_pos + j;
    return p >= back && p - back < pp_out ? b[(p - back) & PP_M] : 0.0f;
}
float plate_pipe_l(uint32_t j) { return pp_at(pp_l, j, pp_lag); }
float plate_pipe_r(uint32_t j) { return pp_at(pp_r, j, pp_lag); }
float plate_pipe_tail(uint32_t j) { return 0.5f * (pp_at(pp_l, j, 2u * pp_lag) + pp_at(pp_r, j, 2u * pp_lag)); }
