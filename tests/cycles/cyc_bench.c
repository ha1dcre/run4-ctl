/* SPDX-License-Identifier: GPL-3.0-only
 * The render's cost in instructions, for tools/cycles.py: one mode's engine (CYC_SYNTH, CYC_DUB or CYC_AMB) built
 * freestanding for an in-order 32-bit core with a single-precision FPU (Cortex-M4F: arm-none-eabi-gcc), run in
 * Unicorn. The FM-1's own core (JieLi pi32v2) has no emulator here; the M4F has the same shape (16 registers, 32 x 32
 * -> 64 multiply, a 32-bit divide, an FPU for float), and tools/cycles.py compares the voices' inner loop with the
 * pi32v2 disassembly CI writes. The same file runs natively (cc -DCYC_NATIVE) to rank the blocks by host time.
 * Calls: cyc_scenario(id, arg) sets a scene up; cyc_block() renders one 256-frame block (the device's APP_BLOCK). */
#include <stdint.h>
#ifdef CYC_NATIVE
#define R4_NATIVE 1
#include <string.h>
#else
/* the device's C library (fm1-base bsp/chip/libc.c gives these three; src/r4_libc.h the rest) */
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}
#endif
#if defined CYC_SYNTH
#include "../../src/synth.c"
#elif defined CYC_DUB
#include "../../src/dub/dub.c"
uint8_t r4_arena[R4_ARENA_BYTES] __attribute__((aligned(4)));
void du_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
#elif defined CYC_AMB
#include "../../src/amb/amb.c"
uint8_t r4_arena[R4_ARENA_BYTES] __attribute__((aligned(4)));
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
#endif
#include "../../src/plate.c"

#define CYC_BLK 256u
#if defined CYC_DUB || defined CYC_AMB
/* the second core, as tools/cycles.py sees it: the job runs at once, between two marks it counts at; the mark at the
 * wait tells how much the first core did meanwhile (the job beyond that is the first core's stall) */
volatile uint32_t cyc_marks;
__attribute__((used, noinline)) void cyc_c2_begin(void) { cyc_marks++; }
__attribute__((used, noinline)) void cyc_c2_end(void) { cyc_marks++; }
__attribute__((used, noinline)) void cyc_c2_waited(void) { cyc_marks++; }
static int cyc_c2_run(void (*fn)(uint32_t), uint32_t arg)
{
    cyc_c2_begin();
    fn(arg);
    cyc_c2_end();
    return 1;
}
static int cyc_c2_wait(void)
{
    cyc_c2_waited();
    return 0;
}
static void cyc_c2(uint32_t on) { plate_offload(on ? cyc_c2_run : 0, on ? cyc_c2_wait : 0); }
#endif
int32_t cyc_out[2 * CYC_BLK];
volatile uint32_t cyc_info[8];                 /* what the scene sounds now (voices, shed step), for the driver */

#if defined CYC_SYNTH
/* the heaviest patch a DX7 dump can bring: TINE EP with every operator at full level and a full sustain (all six
 * sound for as long as a key is down), feedback 7 */
static void cyc_worst_patch(void)
{
    uint32_t k;
    syn_load_patch(0);
    for (k = 0; k < 6u; k++) {
        uint8_t *op = syn.cur + k * FP_OP;
        op[FP_OL] = 99;
        op[FP_L1] = op[FP_L1 + 1] = op[FP_L1 + 2] = 99;
        op[FP_R1 + 3] = 40;
    }
    syn.cur[FP_FB] = 7;
    syn_dirty = 1;
}
/* id 0: factory patch arg (0..7); id 1: the worst patch; id 2: arg notes on (C2 up in fourths, velocity 127),
 * id 3: all effects at their heaviest (drive, chorus, delay, HALL), id 4: all notes off, id 5: arg notes on in one
 * block at once over held ones (a chord struck while others ring), id 6: the shed step arg, id 7: panic (voices cut),
 * id 8: every effect off, id 9: one effect at its heaviest (0 drive, 1 chorus, 2 delay, 3 reverb) */
__attribute__((used, noinline)) void cyc_scenario(uint32_t id, uint32_t arg)
{
    uint32_t i;
    switch (id) {
    case 0: syn_load_patch(arg); break;
    case 1: cyc_worst_patch(); break;
    case 2: for (i = 0; i < arg; i++) syn_event(1, (uint8_t)(36 + i * 5), 127, 0); break;
    case 3:
        syn.drive = 100; syn.chorus = 100; syn.dly_mix = 100; syn.dly_fb = 100; syn.rev_mix = 100;
        syn.rev_type = REV_HALL; syn.rev_size = 100;
        break;
    case 4: syn_event(3, 0, 0, 0); break;
    case 5: for (i = 0; i < arg; i++) syn_event(1, (uint8_t)(38 + i * 5), 127, 0); break;
    case 6: syn_shed = (uint8_t)arg; break;
    case 7: syn_event(6, 0, 0, 0); break;
    case 8: syn.drive = syn.chorus = syn.dly_mix = syn.rev_mix = 0; break;
    case 9:                                  /* one effect at its heaviest: 0 drive, 1 chorus, 2 delay, 3 reverb */
        if (arg == 0) syn.drive = 100;
        else if (arg == 1) syn.chorus = 100;
        else if (arg == 2) syn.dly_mix = 100, syn.dly_fb = 100;
        else syn.rev_mix = 100, syn.rev_type = REV_HALL, syn.rev_size = 100;
        break;
    default: break;
    }
}
__attribute__((used, noinline)) void cyc_block(void)
{
    syn_render(cyc_out, CYC_BLK, 4096);
    cyc_info[0] = syn_active_voices;
    cyc_info[1] = syn_shed;
#ifdef SYN_CM_K0                              /* (the sources before the cost model have none: tools/cycles.py --src) */
    cyc_info[2] = syn_cm_used;               /* the cost model's estimate of this block (syn_cm_seen would take it) */
    cyc_info[3] = syn_cap();
    syn_cm_used = 0;
#else
    cyc_info[2] = 0xFFFFFFFFu;
#endif
}
__attribute__((used, noinline)) void cyc_init(void) { syn_init(); }
#elif defined CYC_DUB
static void cyc_piece(int i)
{
    int16_t par[DP_NPARAMS];
    uint16_t pool;
    int p;
    du_piece(i, par, &pool);
    for (p = 0; p < DP_NPARAMS; p++)
        if (p != DP_TUNE && p != DP_MIDI)
            du_set(p, par[p]);
    du_pool(pool);
    du_pings(0);
    du_regrow();
}
/* id 0: piece arg, playing; id 1: macro arg>>8 to (arg & 0xFF) - 100; id 2: the effects grid mask arg; id 3: perf flags;
 * id 4: seed; id 5: dub hold (freeze) arg; id 6: next chord; id 7: shed step; id 8: parameter arg >> 16 to
 * (arg & 0xFFFF) - 32768; id 9: the plate on the second core (arg 1) or here (0) */
__attribute__((used, noinline)) void cyc_scenario(uint32_t id, uint32_t arg)
{
    switch (id) {
    case 0: du_seed(arg * 977u + 1u); cyc_piece((int)arg); du_play(1); break;
    case 1: du_macro((int)(arg >> 8), (int)(arg & 0xFFu) - 100); break;
    case 2: du_fx(arg); break;
    case 3: du_perf(arg); break;
    case 4: du_seed(arg); break;
    case 5: du_freeze((int)arg); break;
    case 6: du_next(); break;
    case 7: du_shed = (uint8_t)arg; break;
    case 8: du_set((int)(arg >> 16), (int)(arg & 0xFFFFu) - 32768); break;
    case 9: cyc_c2(arg); break;
    default: break;
    }
}
__attribute__((used, noinline)) void cyc_block(void) { du_render(cyc_out, CYC_BLK, 4096); cyc_info[1] = du_shed; }
__attribute__((used, noinline)) void cyc_init(void) { du_init(); du_enter(); }
#elif defined CYC_AMB
static void cyc_piece(int i)
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    int p;
    en_piece(i, par, &pool);
    for (p = 0; p < P_NPARAMS; p++)
        if (p != P_TUNE && p != P_MIDI)
            en_set(p, par[p]);
    en_pool(pool);
    en_regrow();
}
/* id 0: piece arg, growing; id 1: param arg>>8 to arg & 0xFF; id 4: seed; id 5: freeze; id 6: next chord; id 7: shed */
__attribute__((used, noinline)) void cyc_scenario(uint32_t id, uint32_t arg)
{
    switch (id) {
    case 0: en_seed(arg * 977u + 1u); cyc_piece((int)arg); en_play(1); break;
    case 1: en_set((int)(arg >> 8), (int)(arg & 0xFFu)); break;
    case 4: en_seed(arg); break;
    case 5: en_freeze((int)arg); break;
    case 6: en_next(); break;
    case 7: en_shed = (uint8_t)arg; break;
    case 8: en_set((int)(arg >> 16), (int)(arg & 0xFFFFu) - 32768); break;
    case 9: cyc_c2(arg); break;
    default: break;
    }
}
__attribute__((used, noinline)) void cyc_block(void) { en_render(cyc_out, CYC_BLK, 4096); cyc_info[1] = en_shed; }
__attribute__((used, noinline)) void cyc_init(void) { en_init(); en_enter(); }
#endif

#ifdef CYC_NATIVE
/* native: the script (stdin: "s id arg" a scene call, "b n" n blocks) runs; each block's host time on stdout */
#include <stdio.h>
#include <time.h>
int main(void)
{
    char c;
    unsigned a, b;
    cyc_init();
    while (scanf(" %c", &c) == 1) {
        if (c == 's' && scanf("%u %u", &a, &b) == 2) {
            cyc_scenario(a, b);
        } else if (c == 'b' && scanf("%u", &a) == 1) {
            while (a--) {
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                cyc_block();
                clock_gettime(CLOCK_MONOTONIC, &t1);
                printf("%ld\n", (long)((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec)));
            }
        }
    }
    return 0;
}
#else
/* bare metal: no start-up but the stack; the driver calls in. newlib's stubs never reached (no I/O) */
int main(void) { return 0; }
#endif
