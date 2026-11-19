/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott (see LICENSE-MIT)
 * The codec against hostile input (make fuzz, part of make test; built with AddressSanitizer and UBSan): random frames,
 * random frames with our header and a real command, and every kind of valid frame mutated (bytes changed, cut, grown,
 * spliced). The reader must never read past the bytes it is given (each frame is copied into a buffer exactly its size,
 * so ASan sees a read one byte past it), never write past the message, and whatever it accepts must encode again and
 * parse back to the same message. Seeded: the same run every time. Usage: r4p_fuzz [ITERATIONS] [SEED] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/r4p.h"

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }

static const uint8_t CMDS[] = {
    R4P_HELLO, R4P_BTN, R4P_ENC, R4P_POT, R4P_SYNC_REQ, R4P_LIST_PICK, R4P_HV_STATE, R4P_VOICE_NAMES, R4P_VOICE_DATA,
    R4P_BK_PART, R4P_BK_END, R4P_BK_GET, R4P_MODES, R4P_MODE, R4P_DESC_HEAD, R4P_DESC_PAGE, R4P_DESC_PARAM,
    R4P_DESC_LABELS, R4P_DESC_END, R4P_PVAL, R4P_LSTATE, R4P_LM_ERR, R4P_LVIEW,
    R4P_HELLO_ACK, R4P_PAGE, R4P_VALUE, R4P_LEDS, R4P_STATUS, R4P_TOAST, R4P_LIST, R4P_FOCUS, R4P_HV_ON, R4P_HV_VOICE,
    R4P_HV_PARAM, R4P_NAMES_REQ, R4P_VOICE_GET, R4P_TAPE_WAVE, R4P_TAPE_POS, R4P_METERS, R4P_BK_ACK, R4P_BK_INFO,
    R4P_BK_DATA, R4P_MODES_REQ, R4P_MODE_SET, R4P_DESC_REQ, R4P_VALS_REQ, R4P_PSET, R4P_LVIEW_SET,
    R4P_SCENE_PICK, R4P_SCENES, R4P_SCENE_STATE, R4P_ROLL};
#define NCMDS (sizeof CMDS / sizeof CMDS[0])

/* a valid message of each command, to mutate (filled by hand: plain, short strings) */
static size_t seed_frame(uint8_t cmd, uint8_t *f, size_t cap)
{
    r4p_msg_t m;
    int i;
    memset(&m, 0, sizeof m);
    m.cmd = cmd;
    switch (cmd) {
    case R4P_HELLO: m.u.hello.proto = 1; m.u.hello.fw_minor = 8; m.u.hello.minor = 4; break;
    case R4P_ENC: m.u.enc.id = 3; m.u.enc.delta = -5; break;
    case R4P_HV_STATE: m.u.hvstate.voice = 3; strcpy(m.u.hvstate.name, "E.PIANO"); break;
    case R4P_VOICE_NAMES: m.u.names.total = 40; m.u.names.n = 8; for (i = 0; i < 8; i++) { m.u.names.used[i] = 1; strcpy(m.u.names.name[i], "VOICE"); } break;
    case R4P_VOICE_DATA: case R4P_BK_PART: case R4P_BK_DATA: m.u.voice.used = 1; for (i = 0; i < R4P_PACKED; i++) m.u.voice.data[i] = (uint8_t)(i & 0x7F); break;
    case R4P_BK_END: case R4P_BK_INFO: m.u.bksum.count = 32; m.u.bksum.crc = 0xCBF43926u; break;
    case R4P_MODES:
        m.u.modes.n = 3;
        for (i = 0; i < 3; i++) { m.u.modes.m[i].id = (uint8_t)i; m.u.modes.m[i].flags = 3; strcpy(m.u.modes.m[i].name, "MODE"); }
        break;
    case R4P_MODE: case R4P_DESC_END: m.u.mode.mode = 2; m.u.mode.flags = 3; m.u.mode.hash = 0xDEADBEEFu; break;
    case R4P_DESC_HEAD: m.u.dhead.mode = 2; m.u.dhead.npages = 8; m.u.dhead.nparams = 35; m.u.dhead.nstate = 4; strcpy(m.u.dhead.title, "AMB");
        for (i = 0; i < 4; i++) strcpy(m.u.dhead.state[i], "S"); break;
    case R4P_DESC_PAGE: strcpy(m.u.dpage.title, "PAD"); break;
    case R4P_DESC_PARAM: m.u.dparam.id = 9; m.u.dparam.kind = R4P_LK_ENUM; m.u.dparam.step = 1; m.u.dparam.hi = 7; m.u.dparam.nlabels = 8;
        strcpy(m.u.dparam.name, "PIECE"); strcpy(m.u.dparam.text, "GARDEN"); break;
    case R4P_DESC_LABELS: m.u.dlabels.total = 12; m.u.dlabels.n = 8; for (i = 0; i < 8; i++) strcpy(m.u.dlabels.label[i], "C#"); break;
    case R4P_PVAL: m.u.pval.n = 6; for (i = 0; i < 6; i++) { m.u.pval.id[i] = (uint16_t)i; m.u.pval.v[i] = (int16_t)(i * 99 - 300); strcpy(m.u.pval.text[i], "V"); } break;
    case R4P_LSTATE: m.u.lstate.n = 4; m.u.lstate.flags = 5; for (i = 0; i < 4; i++) strcpy(m.u.lstate.text[i], "T"); break;
    case R4P_LM_ERR: m.u.lerr.code = R4P_LE_RANGE; m.u.lerr.id = 300; break;
    case R4P_HELLO_ACK: m.u.ack.proto = 1; m.u.ack.minor = 4; break;
    case R4P_PAGE: strcpy(m.u.page.title, "TAPE"); for (i = 0; i < 4; i++) { m.u.page.slot[i].kind = 1; strcpy(m.u.page.slot[i].text, "1"); }
        m.u.page.v12 = 1; m.u.page.cursor = 1; m.u.page.item_count = 4; strcpy(m.u.page.path, "A > B"); break;
    case R4P_VALUE: strcpy(m.u.value.text, "3 dB"); m.u.value.flags = 1; break;
    case R4P_TOAST: strcpy(m.u.toast.text, "SAVED"); break;
    case R4P_LIST: m.u.list.total = 9; m.u.list.n = 7; for (i = 0; i < 7; i++) strcpy(m.u.list.item[i], "ITEM"); break;
    case R4P_FOCUS: m.u.focus.item_count = 9; m.u.focus.item_idx = 3; break;
    case R4P_HV_VOICE: m.u.hvvoice.kind = (uint8_t)rn(3); m.u.hvvoice.idx = 5; break;
    case R4P_HV_PARAM: m.u.hvparam.n = 21; for (i = 0; i < 21; i++) m.u.hvparam.id[i] = (uint8_t)i; break;
    case R4P_TAPE_WAVE: m.u.wave.n = 120; strcpy(m.u.wave.label, "T1"); for (i = 0; i < 120; i++) m.u.wave.peak[i] = (uint8_t)i; break;
    case R4P_TAPE_POS: m.u.tpos.in = 10; m.u.tpos.out = 900; break;
    case R4P_PSET: m.u.pset.mode = 2; m.u.pset.n = 16; for (i = 0; i < 16; i++) { m.u.pset.id[i] = (uint16_t)i; m.u.pset.v[i] = (int16_t)-i; } break;
    case R4P_MODE_SET: case R4P_DESC_REQ: case R4P_VALS_REQ: m.u.b1.v = 2; break;
    case R4P_LVIEW: m.u.lview.mode = 1; m.u.lview.page = 11; m.u.lview.n = 4;
        for (i = 0; i < 4; i++) m.u.lview.id[i] = (uint16_t)(i == 2 ? R4P_LVIEW_NONE : 128 + i); break;
    case R4P_LVIEW_SET: m.u.lview.mode = 2; m.u.lview.page = 3; break;
    case R4P_SCENE_PICK: m.u.b1.v = 17; break;
    case R4P_SCENES: m.u.scenes.total = 40; m.u.scenes.first = 16; m.u.scenes.n = 16;
        for (i = 0; i < 16; i++) strcpy(m.u.scenes.name[i], "VERSE"); break;
    case R4P_SCENE_STATE: m.u.scene_state.active = 3; m.u.scene_state.queued = R4P_SCENE_NONE; break;
    case R4P_ROLL: m.u.roll.flags = R4P_RF_PLAY; m.u.roll.lo = 48; m.u.roll.rows = 13; m.u.roll.steps = 16;
        m.u.roll.first = 16; m.u.roll.total = 64; m.u.roll.cur_col = 3; m.u.roll.cur_row = 6; m.u.roll.play = 9;
        m.u.roll.beat = 4; strcpy(m.u.roll.label, "C3-C4"); m.u.roll.n = 20;
        for (i = 0; i < 20; i++) {
            m.u.roll.note[i].col = (uint8_t)(i % 16); m.u.roll.note[i].len = 1; m.u.roll.note[i].row = (uint8_t)(i % 13);
            m.u.roll.note[i].vel = (uint8_t)(60 + i); m.u.roll.note[i].flags = (uint8_t)(i % 32);
        }
        break;
    default: break;
    }
    return r4p_encode(f, cap, &m);
}

static long accepted, rejected, foreign, reencoded;
static int check(const uint8_t *src, size_t n)
{
    uint8_t *f = malloc(n ? n : 1), back[1024];
    r4p_msg_t m, m2;
    int r;
    size_t k;
    memcpy(f, src, n);                       /* exactly n bytes: ASan sees any read past them */
    memset(&m, 0xA5, sizeof m);
    r = r4p_parse(f, n, &m);
    if (r == 1) {
        accepted++;
        k = r4p_encode(back, sizeof back, &m);
        /* what was accepted encodes again (a 1.4 / 1.5 device frame may come in longer than a device sends one) */
        if (!k && !(m.cmd >= R4P_MODES && m.cmd <= R4P_LVIEW)) {
            printf("FAIL: accepted but does not encode (cmd 0x%02X, %zu bytes)\n", m.cmd, n);
            free(f);
            return 1;
        }
        if (k) {
            reencoded++;
            memset(&m2, 0x5A, sizeof m2);
            if (r4p_parse(back, k, &m2) != 1 || m2.cmd != m.cmd) {
                printf("FAIL: re-encoded frame does not parse (cmd 0x%02X)\n", m.cmd);
                free(f);
                return 1;
            }
            {   /* and parses to the same bytes again (a fixed point) */
                uint8_t again[1024];
                size_t k2 = r4p_encode(again, sizeof again, &m2);
                if (k2 != k || memcmp(again, back, k)) {
                    printf("FAIL: not a fixed point (cmd 0x%02X)\n", m.cmd);
                    free(f);
                    return 1;
                }
            }
        }
        (void)r4p_desc_hash_add(R4P_FNV0, f, n);
    } else if (r == 0) {
        foreign++;
    } else {
        rejected++;
    }
    if (n >= 2 && f[0] == 0xF0 && f[n - 1] == 0xF7)
        (void)r4p_desc_hash_add(R4P_FNV0, f, n);   /* (the hash's walk over a hostile frame, too) */
    free(f);
    return 0;
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 1000000L, i;
    static uint8_t f[800], seed[NCMDS][800];
    size_t seedn[NCMDS];
    unsigned c;
    if (argc > 2)
        rng ^= (uint64_t)strtoull(argv[2], 0, 0);
    for (c = 0; c < NCMDS; c++) {
        seedn[c] = seed_frame(CMDS[c], seed[c], sizeof seed[c]);
        if (!seedn[c]) {
            printf("FAIL: seed frame for 0x%02X does not encode\n", CMDS[c]);
            return 1;
        }
    }
    for (i = 0; i < iters; i++) {
        size_t n, k;
        uint32_t how = rn(8);
        if (how == 0) {                      /* noise */
            n = rn(700);
            for (k = 0; k < n; k++) f[k] = (uint8_t)rnd();
        } else if (how == 1) {               /* our header, a real command, a random 7-bit payload */
            n = 6 + rn(300);
            f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x52; f[3] = 0x34; f[4] = CMDS[rn(NCMDS)];
            for (k = 5; k < n - 1; k++) f[k] = (uint8_t)(rn(4) ? rnd() & 0x7F : rn(30));
            f[n - 1] = 0xF7;
        } else {                             /* a valid frame, mutated */
            uint32_t s = rn(NCMDS), muts = 1 + rn(4), j;
            n = seedn[s];
            memcpy(f, seed[s], n);
            for (j = 0; j < muts; j++) {
                uint32_t op = rn(6), at = n > 2 ? 1 + rn((uint32_t)n - 2) : 0;
                if (op == 0 && n > 2) f[at] = (uint8_t)(rnd() & 0x7F);                    /* a byte changed */
                else if (op == 1 && n > 2) f[at] = (uint8_t)(f[at] + (rn(2) ? 1 : 0xFF)); /* nudged */
                else if (op == 2 && n > 6) { n = 5 + rn((uint32_t)n - 5); f[n - 1] = 0xF7; }     /* cut */
                else if (op == 3 && n < 700) {                                       /* grown */
                    uint32_t add = 1 + rn(8);
                    memmove(f + at + add, f + at, n - at);
                    for (k = 0; k < add; k++) f[at + k] = (uint8_t)(rnd() & 0x7F);
                    n += add;
                } else if (op == 4 && n > 7) {                                       /* a run deleted */
                    uint32_t del = 1 + rn(4);
                    if (at + del < n - 1) { memmove(f + at, f + at + del, n - at - del); n -= del; }
                } else if (op == 5 && n > 2) f[at] = (uint8_t)(0x80 | rnd());            /* a status byte inside */
            }
        }
        if (check(f, n))
            return 1;
    }
    printf("r4p fuzz: %ld frames: %ld accepted (%ld encoded again), %ld refused, %ld not ours\n", iters, accepted,
           reencoded, rejected, foreign);
    return 0;
}
