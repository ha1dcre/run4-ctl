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
