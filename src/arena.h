/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: the mode arena. Synth mode (and RUN4 mode's host voice), AMB mode and DUB mode never sound at once, so
 * their big lines share one buffer in the pool: Synth's delay and chorus lines, or AMB's tape loop, Echoplex, sine
 * table and mix buffers, or DUB's tape loop, Echoplex, cloud line, sine table and mix buffers. A mode switch holds the
 * render silent and clears the arena for the mode coming in (app.c mode_switch). The plate reverb's lines
 * (src/plate.c) are shared the same way, in a buffer of their own. Defined in src/synth.c. */
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

#define R4_DUB_SN 1024u                      /* DUB's sine table (+1 point) */
#define R4_DUB_BLK 128u                      /* its mix buffers' length (nine of them; DUB-1: seven of 256) */
#define R4_DUB_NBUF 9u
#define R4_DUB_TAPE_N 62000u                 /* its tape loop: 5.6 s at 11.025 kHz, as DUB-1 */
#define R4_DUB_PLEX_N 7100u                  /* its Echoplex (and Stutter's sixteenth): 160 ms */
#define R4_DUB_CL_N 8192u                    /* its cloud's line: 0.74 s at 11.025 kHz */
#define R4_DUB_OFF_SIN 0u
#define R4_DUB_OFF_MIX (R4_DUB_OFF_SIN + (R4_DUB_SN + 1u) * 4u + 4u)
#define R4_DUB_OFF_TAPE (R4_DUB_OFF_MIX + R4_DUB_NBUF * R4_DUB_BLK * 4u)
#define R4_DUB_OFF_PLEX (R4_DUB_OFF_TAPE + R4_DUB_TAPE_N * 2u)
#define R4_DUB_OFF_CL (R4_DUB_OFF_PLEX + R4_DUB_PLEX_N * 2u)
#define R4_DUB_BYTES (R4_DUB_OFF_CL + R4_DUB_CL_N * 2u)

#ifndef R4_AMB
#define R4_AMB 1                             /* AMB mode in the build (app/app.h; app-noamb: 0) */
#endif
#ifndef R4_DUB
#define R4_DUB 1                             /* DUB mode in the build (app/app.h; app-noamb: 0) */
#endif
/* the largest of the modes built: Synth's lines always (Synth mode and the host voice), AMB's and DUB's if built */
#define R4_MAX_(a, b) ((a) > (b) ? (a) : (b))
#define R4_ARENA_BYTES R4_MAX_(R4_SYN_BYTES, R4_MAX_(R4_AMB ? R4_AMB_BYTES : 0u, R4_DUB ? R4_DUB_BYTES : 0u))
extern uint8_t r4_arena[];
