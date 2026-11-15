/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL without AMB mode and DUB mode: the same firmware (app/app.c) built with R4_AMB 0 and R4_DUB 0 (app.h here),
 * for a release without the generative modes: SEL + HOME goes RUN4, SYNTH, RUN4; no AMB or DUB code, buffers or cards
 * (the arena is Synth's); their store objects and the backup image as in the full build (a backup restores both ways). */
#include "../app/app.c"
