/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: the mode arena. Synth mode (and RUN4 mode's host voice) and AMB mode never sound at once, so their big
 * lines share one buffer in the pool: Synth's delay and chorus lines, or AMB's tape loop, Echoplex, sine table and
 * mix buffers. A mode switch holds the render silent and clears the arena for the mode coming in (app.c mode_switch).
 * The plate reverb's lines (src/plate.c) are shared the same way, in a buffer of their own. Defined in src/synth.c. */
#pragma once
#include <stdint.h>

#define R4_SYN_DLY_N 16384u                  /* the delay line a side: 371 ms */
#define R4_SYN_CHO_N 1024u
#define R4_SYN_BYTES (2u * R4_SYN_DLY_N * 2u + R4_SYN_CHO_N * 2u)

#define R4_AMB_SN 1024u                      /* AMB's sine table (+1 point) */
#define R4_AMB_BLK 256u                      /* its mix buffers' length (eight of them) */
#define R4_AMB_TAPE_N 62000u                 /* its tape loop: 5.6 s at 11.025 kHz, as AMB-1 */
#define R4_AMB_PLEX_N 7100u                  /* its Echoplex: 160 ms */
#define R4_AMB_OFF_SIN 0u
#define R4_AMB_OFF_MIX (R4_AMB_OFF_SIN + (R4_AMB_SN + 1u) * 4u + 4u)
#define R4_AMB_OFF_TAPE (R4_AMB_OFF_MIX + 8u * R4_AMB_BLK * 4u)
#define R4_AMB_OFF_PLEX (R4_AMB_OFF_TAPE + R4_AMB_TAPE_N * 2u)
#define R4_AMB_BYTES (R4_AMB_OFF_PLEX + R4_AMB_PLEX_N * 2u)

#ifndef R4_AMB
#define R4_AMB 1                             /* AMB mode in the build (app/app.h; app-noamb: 0) */
#endif
#if R4_AMB
#define R4_ARENA_BYTES (R4_AMB_BYTES > R4_SYN_BYTES ? R4_AMB_BYTES : R4_SYN_BYTES)
#else
#define R4_ARENA_BYTES R4_SYN_BYTES          /* (no AMB mode: Synth's lines only) */
#endif
extern uint8_t r4_arena[];
