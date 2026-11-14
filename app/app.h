/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: the app's half of the contract with fm1-base (same shape as base/template/app.h). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "RUN4 CTL"
#define APP_VERSION "0.2"
#define APP_SPLASH_RGB RGB(255, 140, 40)
#define APP_BLOCK 256u
#define APP_TOUCH                        /* key velocity and pressure (lib/touch.h); the SETUP page sets how they are used */
#define APP_SYSEX_BYTE                   /* app_sysex_byte: Synth mode reads DX7 bank dumps that outgrow the SysEx buffer */
#define APP_SYSEX                        /* the protocol rides on SysEx: app_sysex gets F0 7D 52 34 .. frames */

/* the store: the platform's panel calibration, our SETUP page (setup_t, app.c), and Synth mode's settings and its two
 * banks of 16 user voices (src/synth.c) */
enum { OBJ_PANEL, OBJ_SETUP, OBJ_SYNSET, OBJ_BANK_A, OBJ_BANK_B, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDA000u, 0xD8000u, 0xD6000u, 0xD4000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_sysex(const uint8_t *p, uint32_t n);
void app_sysex_byte(uint8_t b);

#define APP_HOST_EXPECT                  /* the simulator's "expect WHAT N" checks */
int app_expect(const char *what, int *got);
