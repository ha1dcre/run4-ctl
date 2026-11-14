/* SPDX-License-Identifier: GPL-3.0-only
 * Divisions the FM-1 cannot do: its link has no compiler runtime (no __udivdi3, __divdi3), so no 64-bit divide may
 * reach the device code. Its CPU divides 32 by 32 bits; these two build the rest from that, exactly (the same result as
 * the 64-bit expression they replace), and the host uses them too, so the tests run the device's arithmetic.
 *
 * r4_udiv64_32(n, d): n / d for a 64-bit n whose quotient fits 32 bits (n >> 32 < d); 0xFFFFFFFF when it does not
 * (or d = 0). Two 32-bit divides on 16-bit digits (Hacker's Delight, divlu), one when n fits 32 bits.
 * r4_muldiv_s32(v, c, d): (int64_t)v * c / d as the low 32 bits a cast to int32_t keeps, for a small d > 0 and
 * 0 <= c < 65536 (an effect's mix: d 100, 200): v = q d + m, so v c / d = q c + m c / d, the parts of one sign. */
#pragma once
#include <stdint.h>

static uint32_t r4_udiv64_32(uint64_t n, uint32_t d)
{
    uint32_t u1 = (uint32_t)(n >> 32), u0 = (uint32_t)n, s = 0, d1, d0, un32, un10, un1, un0, un21, q1, q0, rhat;
    if (u1 >= d)
        return 0xFFFFFFFFu;
    if (!u1)
        return u0 / d;
    /* normalise: the divisor's top bit set */
    if (!(d >> 16)) { d <<= 16; s += 16; }
    if (!(d >> 24)) { d <<= 8; s += 8; }
    if (!(d >> 28)) { d <<= 4; s += 4; }
    if (!(d >> 30)) { d <<= 2; s += 2; }
    if (!(d >> 31)) { d <<= 1; s += 1; }
    d1 = d >> 16;
    d0 = d & 0xFFFFu;
    un32 = s ? u1 << s | u0 >> (32u - s) : u1;
    un10 = u0 << s;
    un1 = un10 >> 16;
    un0 = un10 & 0xFFFFu;
    q1 = un32 / d1;                          /* the high digit, at most two too big */
    rhat = un32 - q1 * d1;
    while (q1 >= 0x10000u || q1 * d0 > (rhat << 16 | un1)) {
        q1--;
        rhat += d1;
        if (rhat >= 0x10000u)
            break;
    }
    un21 = (un32 << 16) + un1 - q1 * d;      /* (mod 2^32: the true remainder is below d) */
    q0 = un21 / d1;                          /* the low digit */
    rhat = un21 - q0 * d1;
    while (q0 >= 0x10000u || q0 * d0 > (rhat << 16 | un0)) {
        q0--;
        rhat += d1;
        if (rhat >= 0x10000u)
            break;
    }
    return q1 << 16 | q0;
}

static inline int32_t r4_muldiv_s32(int32_t v, uint32_t c, int32_t d)
{
    int32_t q = v / d, m = v % d;            /* m has v's sign, |m| < d */
    return (int32_t)((uint32_t)q * c + (uint32_t)(m * (int32_t)c / d));
}
