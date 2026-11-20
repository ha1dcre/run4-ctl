/* SPDX-License-Identifier: GPL-3.0-only
 * The level of a take recorded from the FM-1 over USB and played back, against what the player heard live, through
 * fm1-base's own USB audio mix (base/device/uac_mix.h, as main_fm1.c render_block calls it) and RUN4 CTL's MASTER gain
 * (app.c app_gain: MASTER's position squared, 0 with LOCAL OUT: USB ONLY, which sends the render at full level).
 *
 * The rule (firmware 0.9.1): a take recorded at unity in RUN4 and played back at unity is as loud as the FM-1 was live.
 * With a USB audio output (firmware 0.9 and before) the iPad played the take back THROUGH the FM-1, where fm1-base puts
 * the computer's audio at MASTER again (uac_play at master^2): the recording already carried MASTER, so the take came
 * out MASTER twice: -12 dB more at MASTER half way, the "much quieter" Poly (FM-1 source) -> Tape recording owners
 * heard. Built twice (make usblevel): -DTEST_OUT=1 shows the old loss, -DTEST_OUT=0 (the package) checks the rule. */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#ifndef TEST_OUT
#define TEST_OUT 0
#endif
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT TEST_OUT
#define N 64u
static int32_t usb_rec[2 * N];          /* what the iPad receives (uac_tap) */
static int32_t usb_play[2 * N];         /* what the iPad sends back to the FM-1 (uac_play's ring) */
static void uac_tap(const int32_t *o, uint32_t n) { uint32_t i; for (i = 0; i < 2u * n; i++) usb_rec[i] = o[i]; }
#if TEST_OUT
static void uac_play(int32_t *o, uint32_t n, uint32_t q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        o[i] += (int32_t)((int64_t)usb_play[i] * q12 >> 12);
}
#endif
#include "../base/device/uac_mix.h"

static uint32_t app_gain(uint32_t m, int usb_only) { return usb_only ? 0u : m * m >> 12; }   /* app.c app_gain */
#define VOICE 4000000                   /* the FM-1's render at 4096 (24-bit) */

static double db(double a, double b) { return 20.0 * log10(a / b); }
int main(void)
{
    int fails = 0, checks = 0;
    uint32_t master, i;
    for (master = 512; master <= 4096; master += 512) {           /* LOCAL OUT ON: the speaker plays (USB ONLY: silent) */
        uint32_t gain = app_gain(master, 0);
        int32_t o[2 * N];
        double live, take;
        /* live: the FM-1 plays its sound and RUN4 records it; nothing comes back from the iPad meanwhile */
        for (i = 0; i < 2u * N; i++) usb_play[i] = 0, o[i] = (int32_t)((int64_t)VOICE * gain >> 12);
        uac_mix(o, N, gain, master, 0, 4096u);
        live = o[0];                                                  /* the FM-1's speaker / headphones */
        /* the take at unity in RUN4, played back: through the FM-1 (USB out) or on the iPad (no USB out) */
#if TEST_OUT
        for (i = 0; i < 2u * N; i++) usb_play[i] = usb_rec[i], o[i] = 0;   /* the FM-1 silent, the take sent to it */
        uac_mix(o, N, gain, master, 0, 4096u);
        take = o[0];
#else
        take = usb_rec[0];                                            /* the iPad's own speaker at unity */
#endif
        checks++;
        if (fabs(db(take, live)) > 0.1 && fails++ < 4)
            printf("%s MASTER %4u: the take %+.1f dB against live\n", TEST_OUT ? "  usb out:" : "FAIL", master,
                   db(take, live));
    }
    for (master = 0; master <= 4096; master += 512) {
        int32_t o[2 * N];
        for (i = 0; i < 2u * N; i++) { usb_play[i] = 0; o[i] = VOICE; }
        uac_mix(o, N, app_gain(master, 1), master, 1, 4096u);
        checks++;
        if (usb_rec[0] != VOICE || o[0] != 0) fails++;
    }
    if (TEST_OUT) {
        printf("usb level, with a USB audio output: %d of %d MASTER positions play the take back quieter (expected)\n",
               fails, checks);
        return fails == 7 ? 0 : 1;                           /* (at MASTER full both are equal) */
    }
    printf("usb level: %d MASTER positions, the take as loud as live; %d differ\n", checks, fails);
    return fails != 0;
}
