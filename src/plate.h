/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1, https://github.com/charlesvestal/fm1-amb, app/dsp/amb.c)
 * Modified for RUN4 CTL by ha1dcre, 2026: the plate taken out of AMB-1's engine into a unit of its own, shared by
 * Synth mode's reverb (src/synth.c) and AMB mode (src/amb/amb.c); see plate.c for what changed. */
/* Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997), as AMB-1 has it (from FoMni-1): mono in, stereo out.
 * Float processing, int16 delay lines (69 KB in the pool, half of float lines). Audio context; one user at a time
 * (the modes are exclusive). */
#pragma once
#include <stdint.h>

typedef struct {
    float decay;                         /* the tank's decay, 0 .. 0.9995 (Dattorro 0.5; AMB-1 0.6 .. 0.975, frozen 0.9995) */
    float damp;                          /* the tank's low-pass: the share of a new sample, 0.05 (dark) .. 1 (none) */
    float bw;                            /* the input's low-pass the same way (Dattorro's bandwidth 0.9995; AMB-1 0.6) */
    float dd2;                           /* decay diffusion 2 (Dattorro 0.5; AMB-1 decay + 0.15 in 0.25 .. 0.5) */
    uint16_t pre;                        /* the predelay in samples, 0 .. PLATE_PRE_MAX */
    uint8_t light;                       /* the overload guard: the four input diffusers bypassed */
} plate_cfg_t;

#define PLATE_PRE_MAX 1323u              /* 30 ms */
#define PLATE_Q 8192.0f                  /* the lines' scale: 1.0 is 8192, so +-4 fits */

void plate_clear(void);                  /* every line silent (a mode switch, the guard giving a step back) */
void plate_set(const plate_cfg_t *c);    /* the settings (block rate) */
/* n frames: in (mono, 1.0 = full scale) -> the wet left and right (0.6 x the taps, as AMB-1) */
void plate_run(const float *in, float *outl, float *outr, uint32_t n);
uint32_t plate_bytes(void);              /* its lines' RAM */

/* The plate a chunk late, so that the second core can run it beside the rest of the render (RUN4 CTL, firmware
 * 0.9.2; DUB and AMB mode). A chunk's input goes in (plate_pipe_put) after its render; the next chunk's start hands it
 * to the second core (plate_pipe_go: the job, plate_run of that input, into a ring) while the first core renders the
 * voices, and waits for it before the dry mix (plate_pipe_wait). The wet sample for frame p of the render is the
 * plate's output for frame p - lag (plate_pipe_l / _r, j: the frame in the chunk), lag at least the longest chunk; the
 * plate's tail 2 x lag back (plate_pipe_tail) is there before the wait, for a feedback into the plate's input. With
 * no second core (plate_offload unset, its run refusing, or 0 for it) the first core runs the job itself, at the same
 * place: the same sound either way. */
#define PLATE_PIPE_N 1024u                       /* the ring: 2 x lag + the longest chunk must fit */
#define PLATE_PIPE_MAXN 256u                     /* the longest chunk */
void plate_pipe_reset(uint32_t lag);             /* silence, the lag (frames, PLATE_PIPE_MAXN at most); and plate_clear */
void plate_pipe_go(void);                        /* a chunk's start: the last input to the plate (another core or here) */
void plate_pipe_wait(void);                      /* its output in */
void plate_pipe_put(const float *in, uint32_t n);   /* after the wait: this chunk's plate input (n <= PLATE_PIPE_MAXN) */
float plate_pipe_l(uint32_t j);                  /* the wet left / right for frame j of the chunk (after the wait) */
float plate_pipe_r(uint32_t j);
float plate_pipe_tail(uint32_t j);               /* (l + r) / 2, 2 x lag back (any time in the chunk) */
/* the second core: run(fn, arg) 1 = it has the job (wait then returns 0 = done, else: the job dropped, do it here) */
void plate_offload(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));
uint32_t plate_pipe_jobs2(void);                 /* the jobs the second core ran (the diagnostics) */
int plate_pipe_offloaded(void);                  /* a second core is set to take the jobs (plate_offload) */
void plate_pipe_hold(int on);                    /* on: no plate (the overload guard's last step on one core): the wet
                                                  * silent; off: back, from silence */
