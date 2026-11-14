/* SPDX-License-Identifier: GPL-3.0-only
 * The reverb Synth mode had up to firmware 0.6 (src/synth.c fx_run: four damped combs and two allpasses a channel,
 * int16 lines), kept here unchanged as the reference for the A/B against the Dattorro plate (tests/reverb_ab.c).
 * Not in the firmware any more. */
#define REF_RV_N 2048u
static int16_t ref_comb[2][4][REF_RV_N];
static int16_t ref_ap[2][2][512];
static struct { uint16_t comb_i[2][4], ap_i[2][2]; int32_t comb_lp[2][4]; } ref_st;
static const uint16_t REF_RV_LEN[3][4] = {{557, 617, 691, 773}, {211, 263, 331, 397}, {1117, 1277, 1423, 1559}};
static const uint16_t REF_AP_LEN[3][2] = {{241, 83}, {113, 41}, {347, 163}};
static int32_t ref_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static void ref_clear(void)
{
    memset(ref_comb, 0, sizeof ref_comb);
    memset(ref_ap, 0, sizeof ref_ap);
    memset(&ref_st, 0, sizeof ref_st);
}
/* one frame: l, r (Q23) -> the wet left and right as fx_run added them before the mix (wl, wr; times rev_mix / 60) */
static void ref_rev(int32_t l, int32_t r, uint32_t type, int32_t size, int shed, int32_t *wl, int32_t *wr)
{
    uint32_t ch, k, ty = type % 3u, nc = shed ? 2u : 4u;
    int32_t fbk = 18000 + size * 130 + (int32_t)ty * 1500;
    for (ch = 0; ch < 2u; ch++) {
        int32_t s = ((ch ? r : l) >> 2), acc = 0;
        for (k = 0; k < nc; k++) {
            uint32_t len = REF_RV_LEN[ty][k] + (ch ? 23u : 0u);
            uint16_t *ci = &ref_st.comb_i[ch][k];
            int32_t y = ref_comb[ch][k][*ci], lp;
            lp = ref_st.comb_lp[ch][k] + ((y - ref_st.comb_lp[ch][k]) >> 1);
            ref_st.comb_lp[ch][k] = lp;
            ref_comb[ch][k][*ci] = (int16_t)ref_clamp((s >> 8) + (int32_t)((int64_t)lp * (fbk > 32000 ? 32000 : fbk) >> 15), -32768, 32767);
            if (++*ci >= len)
                *ci = 0;
            acc += y;
        }
        acc = acc << (nc == 4u ? 6 : 7);
        for (k = 0; k < 2u; k++) {
            uint16_t *ai = &ref_st.ap_i[ch][k];
            int32_t d = ref_ap[ch][k][*ai] << 8, y = d - acc / 2;
            ref_ap[ch][k][*ai] = (int16_t)ref_clamp((acc + (d >> 1)) >> 8, -32768, 32767);
            acc = y;
            if (++*ai >= REF_AP_LEN[ty][k])
                *ai = 0;
        }
        if (ch) *wr = acc; else *wl = acc;
    }
}
