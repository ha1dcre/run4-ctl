/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL without AMB mode: the same firmware (app/app.c) built with R4_AMB 0 (app.h here), for a release without
 * it: SEL + HOME goes RUN4, SYNTH, RUN4; no AMB code, buffers or cards; its store object and the backup image as in
 * the full build (a backup restores both ways). */
#include "../app/app.c"
