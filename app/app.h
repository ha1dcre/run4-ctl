/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: the app's half of the contract with fm1-base (same shape as base/template/app.h). */
#pragma once
#include <stdint.h>
#include "plat.h"

#ifndef R4_AMB
#define R4_AMB 1                         /* AMB mode in the build (app-noamb/app.h: 0); the plate and the cards either way */
#endif
#ifndef R4_DUB
#define R4_DUB 1                         /* DUB mode in the build (app-noamb/app.h: 0); it shares AMB's arena */
#endif
#define APP_NAME "RUN4 CTL"
#define APP_VERSION "0.5.2"                 /* firmware 0.9.2: protocol 1.7 (scenes 1.6, piano roll 1.7); HELLO stays 0.9 */
#define APP_SPLASH_RGB RGB(255, 140, 40)
#define APP_BLOCK 256u
#define APP_TOUCH                        /* key velocity and pressure (lib/touch.h); the SETUP page sets how they are used */
#define APP_SYSEX_BYTE                   /* app_sysex_byte: Synth mode reads DX7 bank dumps that outgrow the SysEx buffer */
#define APP_SYSEX                        /* the protocol rides on SysEx: app_sysex gets F0 7D 52 34 .. frames */
#define APP_OVERLOAD                     /* app_overload: a render took over 90 % of its time (the overload guard) */
#define APP_GAIN(m) app_gain(m)          /* MASTER's gain; 0 with LOCAL OUT: USB ONLY (the USB recording at full level) */

/* the store: the platform's panel calibration, our SETUP page (setup_t, app.c), and Synth mode's settings and its two
 * banks of 16 user voices (src/synth.c) */
/* OBJ_SEQ_A..D: the sequencer's 16 patterns, four to an object (synth_seq.c sq_obj_t, 3724 bytes each); OBJ_AMB: AMB
 * mode's pieces (src/amb_ui.c am_store_t, 476 bytes, firmware 0.7); OBJ_DUB: DUB mode's pieces (src/dub_ui.c
 * db_store_t, 2396 bytes, firmware 0.9) */
enum { OBJ_PANEL, OBJ_SETUP, OBJ_SYNSET, OBJ_BANK_A, OBJ_BANK_B, OBJ_BLE, OBJ_SEQ_A, OBJ_SEQ_B, OBJ_SEQ_C, OBJ_SEQ_D,
       OBJ_AMB, OBJ_DUB, OBJ_NOBJ };   /* OBJ_BLE: the radio's address and calibration */
#define APP_STORE_MAP {0xDC000u, 0xDA000u, 0xD8000u, 0xD6000u, 0xD4000u, 0xDE000u, 0xD2000u, 0xD0000u, 0xCE000u, 0xCC000u, \
                       0xCA000u, 0xC8000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_sysex(const uint8_t *p, uint32_t n);
void app_sysex_byte(uint8_t b);
void app_overload(void);
uint32_t app_gain(uint32_t m);

/* fm1.toml backup = true: a backup over USB of everything stored but the panel's calibration (base/device/backup.c,
 * tools/fm1_backup.py); its image: magic "R4CB", version 3 (firmware 0.7: OBJ_AMB added at the end, version 2; firmware
 * 0.9: OBJ_DUB after it), these objects in this order. The objects keep their own layout versions (an older RUN4 CTL's
 * parts load). fm1-base restores only its own version: a version 1 or 2 image (firmware up to 0.8) goes through
 * tools/r4ctl_backup.py, which adds the missing parts as absent (the recipes) and restores it, or writes it as version
 * 3 for the web page (`upgrade`). */
#define APP_BACKUP_OBJS {OBJ_SETUP, OBJ_SYNSET, OBJ_BANK_A, OBJ_BANK_B, OBJ_SEQ_A, OBJ_SEQ_B, OBJ_SEQ_C, OBJ_SEQ_D, OBJ_BLE, \
                         OBJ_AMB, OBJ_DUB}
#define APP_BACKUP_MAGIC 0x42433452u     /* "R4CB" */
#define APP_BACKUP_VERSION 3u
#define APP_BACKUP_WHAT "settings, voices, patterns, pieces"
#define APP_BACKUP_BUF r4_io             /* the store's scratch (synth_ui.c), 3840 B: no restore buffer of its own */
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);
#define APP_REDRAW                       /* fm1.toml mirror = true: draw everything again */
void app_redraw(void);

#define APP_HOST_CMD                     /* the simulator's "blesx HEX": a SysEx frame in over Bluetooth, as USB-MIDI packets */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_EXPECT                  /* the simulator's "expect WHAT N" checks */
int app_expect(const char *what, int *got);
