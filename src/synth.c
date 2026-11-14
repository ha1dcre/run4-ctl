/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the sound. A six-operator FM engine (Felucca's fm6_core.c, a C port of Dexed's msfa, Apache-2.0,
 * see NOTICE) behind a voice pool with stealing, a patch (the 155-byte DX7 voice layout), DX7 packed voice records
 * and bank / single-voice SysEx, a drive / chorus / tempo delay / reverb chain, an arpeggiator and a step sequencer
 * (synth_seq.c: 16 patterns of up to 64 steps, chords, ties, live and step recording, MIDI clock).
 * Plain C, integer only, no platform calls (the app glue is in app.c / synth_ui.c): native tests link it alone.
 *
 * Threading, as on the device: syn_render() runs in the audio context; the main loop (keys, UI) calls syn_event() (a
 * small ring) and edits the settings, which the render reads at its block starts. */
#include <stdint.h>
#include <string.h>
#include "synth/fm6_tables.h"
#include "synth/fm6_core.c"
#include "synth/fm6_factory.h"

#ifndef PLAT_POOL
#define PLAT_POOL
#endif

#define SYN_VOICES 8
#define SYN_NFACTORY FM6_NFACTORY            /* 8 */
#define SYN_BANK_N 16                        /* user voices per stored object; two objects = 32 */
#define SYN_PATCHES (SYN_NFACTORY + 2 * SYN_BANK_N)

/* ------------------------------------------------------------ settings --- */
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_PLAYED, ARP_N };
enum { REV_ROOM, REV_SPRING, REV_HALL, REV_N };
/* what is stored (OBJ_SYNSET) and what the pages edit */
typedef struct {
    uint8_t ver;
    uint8_t patch_idx;                       /* 0..7 factory, 8..39 user */
    uint8_t cur[FP_SIZE + 1];                /* the sounding patch (155 bytes + 0) */
    int8_t bright, env_spd;                  /* macros: modulator level (-64..63), envelope speed (-64..63) */
    uint8_t level;                           /* patch level 0..100 */
    uint8_t drive, chorus, dly_mix, dly_div, dly_fb, rev_mix, rev_type, rev_size;     /* effects 0..100 (div 0..5, type 0..2) */
    int8_t transpose, tune;                  /* semitones -24..24, cents -50..50 */
    uint8_t chan;                            /* 0 = omni, 1..16 */
    uint8_t bend, vel_sens;                  /* bend range 0..12 semitones; velocity amount 0..100 */
    uint8_t arp_on, arp_mode, arp_rate, arp_oct, arp_gate;     /* rate 0..2 (1/8, 1/16, 1/16T), oct 1..3, gate 10..100 */
    uint8_t seq_pat, seq_clock;              /* the selected pattern 0..15; the clock (SQC_AUTO, SQC_OUT, SQC_INT) */
    int8_t seq_trn;                          /* the sequencer's transpose, -24..24 (SEL + a key) */
    uint8_t rsv;
    uint16_t bpm10;                          /* 400..2400 */
} syn_set_t;
#define SYN_SET_VER 2                        /* 2: the patterns moved to their own objects (synth_seq.c) */
static syn_set_t syn;
static uint8_t syn_dirty = 1;                /* an edit: the next block copies the patch again */

/* ----------------------------------------------------------- patch tools --- */
static const uint8_t FP_MAXV[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
static const uint8_t FP_MAXO[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static uint32_t fp_max(uint32_t i)
{
    if (i < 126u)
        return FP_MAXO[i % 21u];
    return i < FP_NAME ? FP_MAXV[i - 126u] : 126u;
}
static void fp_sanitize(uint8_t *v)          /* every value inside its range */
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fp_max(i))
            v[i] = (uint8_t)fp_max(i);
    }
    v[FP_SIZE] = 0;
}
/* a 128-byte packed record (a DX7 32-voice bank's) <-> the 155-byte voice (as Felucca's eng_fm6.c fm6_unpack / fm6_pack) */
static void fp_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fp_sanitize(v);
}
static void fp_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

/* the user bank: two stored objects of SYN_BANK_N voices (packed), header = a used mask */
typedef struct { uint32_t magic; uint16_t used; uint16_t rsv; uint8_t v[SYN_BANK_N][128]; } syn_bank_t;
#define SYN_BANK_MAGIC 0x4B424E53u           /* "SNBK" */
static syn_bank_t syn_bank[2];
static uint8_t syn_bank_dirty;               /* bit b: object b changed since saved */
static const uint8_t *syn_packed(uint32_t idx)   /* patch idx -> its 128 packed bytes, or 0 (an empty user slot) */
{
    if (idx < SYN_NFACTORY)
        return FM6_FACTORY[idx];
    idx -= SYN_NFACTORY;
    if (idx >= 2u * SYN_BANK_N)
        return 0;
    return (syn_bank[idx / SYN_BANK_N].used >> (idx % SYN_BANK_N)) & 1u ? syn_bank[idx / SYN_BANK_N].v[idx % SYN_BANK_N] : 0;
}
static int syn_patch_used(uint32_t idx) { return syn_packed(idx) != 0; }
static void syn_patch_name(uint32_t idx, char *out)      /* 10 characters and a NUL */
{
    const uint8_t *p = syn_packed(idx);
    uint32_t i;
    for (i = 0; i < 10u; i++) {
        uint8_t c = p ? p[118 + i] : (uint8_t)"- EMPTY -  "[i];
        out[i] = c < 32u || c > 126u ? ' ' : (char)c;
    }
    out[10] = 0;
}
static int syn_load_patch(uint32_t idx)      /* 0 = loaded; an empty user slot refuses */
{
    const uint8_t *p = syn_packed(idx);
    if (!p)
        return -1;
    fp_unpack(p, syn.cur);
    syn.patch_idx = (uint8_t)idx;
    syn_dirty = 1;
    return 0;
}
static int syn_store_patch(uint32_t idx)     /* the sounding patch into user slot idx (8..39); 0 = ok */
{
    uint32_t b, s;
    if (idx < SYN_NFACTORY || idx >= SYN_PATCHES)
        return -1;
    b = (idx - SYN_NFACTORY) / SYN_BANK_N;
    s = (idx - SYN_NFACTORY) % SYN_BANK_N;
    fp_pack(syn.cur, syn_bank[b].v[s]);
    syn_bank[b].magic = SYN_BANK_MAGIC;
    syn_bank[b].used = (uint16_t)(syn_bank[b].used | (1u << s));
    syn_bank_dirty = (uint8_t)(syn_bank_dirty | (1u << b));
    syn.patch_idx = (uint8_t)idx;
    return 0;
}

/* ------------------------------------------------------------ DX7 SysEx --- */
/* F0 43 0n 09 20 00 <4096 bytes> cksum F7: a 32-voice bank; F0 43 0n 00 01 1B <155 bytes> cksum F7: one voice. The
 * checksum is the two's complement of the data sum, 7 bits. Fed byte by byte (a bank is longer than the device's
 * 640-byte SysEx buffer). The voices go into the user slots from `syn_imp.at` on (bank: 32 voices from the first slot;
 * a single voice: the current user slot, or the first free one). */
static struct {
    uint16_t n;                              /* bytes of the frame seen */
    uint16_t want;                           /* the data length (4096 or 155), 0 = not a DX7 dump (yet) */
    uint8_t sum, bad;
    uint8_t hdr[6];
    uint8_t buf[4096];
} syn_imp PLAT_POOL;
static uint8_t syn_imp_done;                 /* a dump finished OK: how many voices went in (0 = none) */
static uint8_t syn_imp_slot;                 /* the user slot a single voice goes to */

static void syn_import_voice(const uint8_t *packed, uint32_t slot)
{
    uint32_t b = slot / SYN_BANK_N, s = slot % SYN_BANK_N;
    memcpy(syn_bank[b].v[s], packed, 128);
    syn_bank[b].magic = SYN_BANK_MAGIC;
    syn_bank[b].used = (uint16_t)(syn_bank[b].used | (1u << s));
    syn_bank_dirty = (uint8_t)(syn_bank_dirty | (1u << b));
}
static void syn_sysex_byte(uint8_t c)
{
    if (c == 0xF0u) {
        syn_imp.n = 0;
        syn_imp.want = 0;
        syn_imp.sum = 0;
        syn_imp.bad = 0;
        return;
    }
    if (c >= 0xF8u)                          /* realtime bytes may sit inside a SysEx: not part of it */
        return;
    if (c == 0xF7u) {
        if (syn_imp.want && !syn_imp.bad && syn_imp.n == 5u + syn_imp.want + 1u) {     /* the data and the checksum */
            if ((syn_imp.sum & 0x7Fu) == 0u) {
                if (syn_imp.want == 4096u) {
                    uint32_t k;
                    for (k = 0; k < 32u; k++)
                        syn_import_voice(syn_imp.buf + k * 128u, k);
                    syn_imp_done = 32;
                } else {
                    uint8_t p[128];
                    uint8_t v[FP_SIZE + 1];
                    memcpy(v, syn_imp.buf, FP_SIZE);
                    v[FP_SIZE] = 0;
                    fp_sanitize(v);
                    fp_pack(v, p);
                    syn_import_voice(p, syn_imp_slot < 2u * SYN_BANK_N ? syn_imp_slot : 0u);
                    syn_imp_done = 1;
                }
            }
        }
        syn_imp.want = 0;
        syn_imp.n = 0xFFFFu;                 /* closed: later bytes belong to no frame */
        return;
    }
    if (c >= 0x80u) {                        /* a status byte inside: a damaged frame */
        syn_imp.bad = 1;
        return;
    }
    if (syn_imp.n == 0xFFFFu)
        return;
    if (syn_imp.n < 5u) {                    /* 43 0n format, byte count hi, lo (Yamaha, device number) */
        syn_imp.hdr[syn_imp.n] = c;
        if (syn_imp.n == 4u) {
            if (syn_imp.hdr[0] == 0x43u && syn_imp.hdr[2] == 0x09u && syn_imp.hdr[3] == 0x20u && syn_imp.hdr[4] == 0x00u)
                syn_imp.want = 4096u;
            else if (syn_imp.hdr[0] == 0x43u && syn_imp.hdr[2] == 0x00u && syn_imp.hdr[3] == 0x01u && syn_imp.hdr[4] == 0x1Bu)
                syn_imp.want = 155u;
            else
                syn_imp.want = 0;
        }
    } else if (syn_imp.want && syn_imp.n < 5u + syn_imp.want) {
        syn_imp.buf[syn_imp.n - 5u] = c;
        syn_imp.sum = (uint8_t)(syn_imp.sum + c);
    } else if (syn_imp.want && syn_imp.n == 5u + syn_imp.want) {
        syn_imp.sum = (uint8_t)(syn_imp.sum + c);       /* the checksum byte: the sum of all data and it is 0 mod 128 */
    }
    if (syn_imp.n != 0xFFFFu)
        syn_imp.n++;
    if (syn_imp.n > 5u + 4096u + 2u)
        syn_imp.want = 0;
}

/* --------------------------------------------------------------- voices --- */
typedef struct {
    fm6_note_t n;
    uint8_t note, key;                       /* the MIDI note it was started on; key: the key is still down */
    uint8_t held;                            /* kept sounding by the sustain pedal after its key came up */
    uint32_t age;
} syn_voice_t;
static syn_voice_t syn_v[SYN_VOICES] PLAT_POOL;
static uint32_t syn_age;
static uint8_t syn_eff[FP_SIZE + 1];         /* the patch as the voices play it: the macros applied */
static uint8_t syn_eff_alg;
static int32_t syn_dt[6];
static fm6_lfo_t syn_lfo;
static int32_t syn_bend;                     /* pitch bend, 1/16 semitones */
static uint8_t syn_sustain;
static uint32_t syn_voices_peak;

static int32_t syn_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void syn_sync(void)                   /* the patch, with the macros; once per block when something changed */
{
    uint8_t *p = syn_eff;
    uint32_t k, car;
    int32_t meg = syn_clamp(syn.env_spd, -64, 63) * 40 / 64;
    memcpy(p, syn.cur, FP_SIZE + 1u);
    syn_eff_alg = p[FP_ALG] & 31u;
    car = fm6_carriers(syn_eff_alg);
    for (k = 0; k < 6u; k++) {
        uint8_t *op = p + (5u - k) * FP_OP;
        uint32_t j;
        syn_dt[k] = 0;
        (void)car;
        for (j = 0; j < 4u; j++)
            op[FP_R1 + j] = (uint8_t)syn_clamp(op[FP_R1 + j] + meg, 0, 99);        /* env speed: all operators */
    }
    fm6_lfo_reset(&syn_lfo, p);
    syn_dirty = 0;
}

static void syn_voice_off(syn_voice_t *v)
{
    fm6_note_key(&v->n, syn_eff, 0);
    v->key = 0;
    v->held = 0;
}
static syn_voice_t *syn_alloc(void)          /* a free voice, else the oldest released one, else the oldest */
{
    uint32_t i;
    syn_voice_t *best = 0;
    for (i = 0; i < SYN_VOICES; i++)
        if (!syn_v[i].n.live)
            return &syn_v[i];
    for (i = 0; i < SYN_VOICES; i++)
        if (!syn_v[i].key && !syn_v[i].held && (!best || syn_v[i].age < best->age))
            best = &syn_v[i];
    if (best)
        return best;
    best = &syn_v[0];
    for (i = 1; i < SYN_VOICES; i++)
        if (syn_v[i].age < best->age)
            best = &syn_v[i];
    return best;
}
static int32_t syn_vel(int32_t vel)
{
    int32_t s = syn.vel_sens;
    return syn_clamp(64 + (vel - 64) * s / 100, 1, 127);
}
static void syn_note_on_now(uint8_t note, uint8_t vel)
{
    syn_voice_t *v;
    uint32_t i;
    int32_t t;
    if (syn_dirty)
        syn_sync();
    for (i = 0; i < SYN_VOICES; i++)            /* the same note again: retrigger its voice */
        if (syn_v[i].n.live && syn_v[i].note == note && syn_v[i].key)
            break;
    v = i < SYN_VOICES ? &syn_v[i] : syn_alloc();
    t = (int32_t)note + syn.transpose + syn_eff[FP_TRNSP] - 24;
    fm6_note_init(&v->n, syn_eff, t, syn_vel(vel), i == SYN_VOICES && !v->n.live);
    if (i == SYN_VOICES && v->n.live)           /* stolen: start from silence, not from the old note's phases */
        fm6_note_init(&v->n, syn_eff, t, syn_vel(vel), 1);
    v->note = note;
    v->key = 1;
    v->held = 0;
    v->age = ++syn_age;
    fm6_lfo_key(&syn_lfo);
}
static void syn_note_off_now(uint8_t note)
{
    uint32_t i;
    for (i = 0; i < SYN_VOICES; i++) {
        syn_voice_t *v = &syn_v[i];
        if (v->n.live && v->key && v->note == note) {
            if (syn_sustain) {
                v->key = 0;
                v->held = 1;
            } else {
                syn_voice_off(v);
            }
        }
    }
}
static void syn_all_off_now(void)
{
    uint32_t i;
    for (i = 0; i < SYN_VOICES; i++)
        if (syn_v[i].n.live)
            syn_voice_off(&syn_v[i]);
}
static void syn_sustain_now(uint8_t on)
{
    uint32_t i;
    syn_sustain = on;
    if (!on)
        for (i = 0; i < SYN_VOICES; i++)
            if (syn_v[i].n.live && syn_v[i].held)
                syn_voice_off(&syn_v[i]);
}

/* ---------------------------------------------------------- event ring --- */
/* the main loop and the MIDI input hand notes to the audio context here; kinds: 1 on, 2 off, 3 all off, 4 bend,
 * 5 sustain, 6 panic (voices cut), 7 patch / settings changed */
typedef struct { uint8_t kind, a, b, src; } syn_ev_t;       /* src 0 = played (arp aware), 1 = the sequencer / arp */
#define SYN_EVQ 64
static volatile uint8_t syn_evh, syn_evt;
static syn_ev_t syn_ev[SYN_EVQ];
static void syn_event(uint8_t kind, uint8_t a, uint8_t b, uint8_t src)
{
    uint8_t n = (uint8_t)((syn_evh + 1u) % SYN_EVQ);
    if (n == syn_evt)
        return;                              /* full: the newest is dropped (never happens at human speed) */
    syn_ev[syn_evh] = (syn_ev_t){kind, a, b, src};
    syn_evh = n;
}

/* ------------------------------------------------------------ sequencer --- */
#include "synth_seq.c"
static uint32_t sq_clock_mode(void) { return syn.seq_clock < SQC_N ? syn.seq_clock : SQC_AUTO; }
static int32_t sq_transpose(void) { return syn.seq_trn; }
static uint32_t sq_sel_pat(void) { return syn.seq_pat % SQ_PATS; }
static uint32_t sq_bpm10(void) { return syn.bpm10 < 400u ? 400u : syn.bpm10 > 2400u ? 2400u : syn.bpm10; }
static uint32_t syn_bpm10_now(void)          /* the tempo that plays: a MIDI clock's while one comes (clock AUTO) */
{
    uint32_t b = sq_ext_bpm10();
    return b ? (b < 400u ? 400u : b > 2400u ? 2400u : b) : sq_bpm10();
}

/* ---------------------------------------------------------- arpeggiator --- */
static struct {
    uint8_t held[8], n_held;                 /* the played notes (arp), in the order played */
    uint8_t arp_note_on;                     /* ... and the arp's */
    int8_t arp_pos, arp_dir;                 /* position in the (octave-extended) list; direction for up/down */
    uint32_t arp_phase;                      /* samples into the current step */
    uint8_t arp_step_open;
    uint32_t rnd;
} syn_sq = {{0}, 0, 255, 0, 1, 0, 0, 12345u};

static uint32_t syn_step_samples(uint32_t rate)    /* samples per step: rate 0 = 1/8, 1 = 1/16, 2 = 1/16 triplet */
{
    uint32_t bpm10 = syn_bpm10_now();
    uint32_t beat = (uint32_t)(44100ull * 600ull / bpm10);     /* samples per quarter note */
    return rate == 0u ? beat / 2u : rate == 1u ? beat / 4u : beat / 6u;
}
static void syn_arp_clear(void) { syn_sq.n_held = 0; }
static void syn_arp_press(uint8_t note, int on)
{
    uint32_t i;
    for (i = 0; i < syn_sq.n_held; i++)
        if (syn_sq.held[i] == note)
            break;
    if (on) {
        if (i == syn_sq.n_held && syn_sq.n_held < 8u)
            syn_sq.held[syn_sq.n_held++] = note;
    } else if (i < syn_sq.n_held) {
        memmove(syn_sq.held + i, syn_sq.held + i + 1, syn_sq.n_held - i - 1u);
        syn_sq.n_held--;
    }
}
static uint8_t syn_arp_pick(void)             /* the next note of the arpeggio (255 = nothing held) */
{
    uint8_t sorted[8], tmp;
    uint32_t n = syn_sq.n_held, i, j, total, oct = syn.arp_oct < 1u ? 1u : syn.arp_oct > 3u ? 3u : syn.arp_oct, idx;
    if (!n)
        return 255;
    memcpy(sorted, syn_sq.held, n);
    if (syn.arp_mode != ARP_PLAYED)
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && sorted[j - 1] > sorted[j]; j--)
                tmp = sorted[j], sorted[j] = sorted[j - 1], sorted[j - 1] = tmp;
    total = n * oct;
    switch (syn.arp_mode) {
    case ARP_DOWN:
        syn_sq.arp_pos = (int8_t)(syn_sq.arp_pos <= 0 ? (int)total - 1 : syn_sq.arp_pos - 1);
        break;
    case ARP_UPDOWN:
        if (total < 2u) { syn_sq.arp_pos = 0; break; }
        if (syn_sq.arp_dir >= 0 && syn_sq.arp_pos >= (int)total - 1) syn_sq.arp_dir = -1;
        else if (syn_sq.arp_dir < 0 && syn_sq.arp_pos <= 0) syn_sq.arp_dir = 1;
        syn_sq.arp_pos = (int8_t)(syn_sq.arp_pos + (syn_sq.arp_dir >= 0 ? 1 : -1));
        break;
    case ARP_RANDOM:
        syn_sq.rnd = syn_sq.rnd * 1664525u + 1013904223u;
        syn_sq.arp_pos = (int8_t)((syn_sq.rnd >> 16) % total);
        break;
    default:                                  /* up, as played */
        syn_sq.arp_pos = (int8_t)((syn_sq.arp_pos + 1) % (int)total);
        break;
    }
    if (syn_sq.arp_pos < 0 || (uint32_t)syn_sq.arp_pos >= total)
        syn_sq.arp_pos = 0;
    idx = (uint32_t)syn_sq.arp_pos % n;
    i = sorted[idx] + 12u * ((uint32_t)syn_sq.arp_pos / n);
    return i > 127u ? 127 : (uint8_t)i;
}
/* once a block: advance the clocks by `n` samples, start and stop the notes they own */
static void syn_clock(uint32_t n)
{
    uint32_t step;
    if (syn.arp_on) {
        step = syn_step_samples(syn.arp_rate);
        if (!syn_sq.arp_step_open && syn_sq.n_held) {
            uint8_t nt = syn_arp_pick();
            if (nt != 255) {
                if (syn_sq.arp_note_on != 255) {
                    syn_note_off_now(syn_sq.arp_note_on);
                    if (sq.run && sq.rec) sq_rec_release(syn_sq.arp_note_on);
                }
                syn_note_on_now(nt, 100);
                if (sq.run && sq.rec) sq_rec_note(nt, 100);       /* recording with the arp on: what it plays */
                syn_sq.arp_note_on = nt;
                syn_sq.arp_step_open = 1;
                syn_sq.arp_phase = 0;
            }
        } else if (syn_sq.arp_step_open) {
            syn_sq.arp_phase += n;
            if (syn_sq.arp_note_on != 255 && syn_sq.arp_phase * 100u >= step * (syn.arp_gate < 10u ? 10u : syn.arp_gate)) {
                syn_note_off_now(syn_sq.arp_note_on);
                if (sq.run && sq.rec) sq_rec_release(syn_sq.arp_note_on);
                syn_sq.arp_note_on = 255;
            }
            if (syn_sq.arp_phase >= step)
                syn_sq.arp_step_open = 0;
        }
    } else if (syn_sq.arp_note_on != 255) {
        syn_note_off_now(syn_sq.arp_note_on);
        syn_sq.arp_note_on = 255;
        syn_sq.arp_step_open = 0;
    }
    sq_clock(n);
}
static void syn_seq_start(int run) { sq_start(run); }

/* ------------------------------------------------------------------ fx --- */
#define DLY_N 16384u                         /* the delay line: 371 ms; longer times are cut to it */
#define CHO_N 1024u
#define RV_N 2048u
static int16_t fx_dly[2][DLY_N] PLAT_POOL;
static int16_t fx_cho[CHO_N] PLAT_POOL;
static int16_t fx_comb[2][4][RV_N] PLAT_POOL;
static int16_t fx_ap[2][2][512] PLAT_POOL;
static struct {
    uint32_t dw, cw, cph;
    uint16_t comb_i[2][4], ap_i[2][2];
    int32_t comb_lp[2][4], cho_lp;
} fx_st;
static const uint16_t RV_LEN[REV_N][4] = {   /* comb lengths in samples (L; R is +23): room, spring, hall */
    {557, 617, 691, 773}, {211, 263, 331, 397}, {1117, 1277, 1423, 1559}};
static const uint16_t AP_LEN[REV_N][2] = {{241, 83}, {113, 41}, {347, 163}};
static const uint8_t DLY_DIV16[6] = {2, 3, 4, 6, 8, 12};      /* the delay time in sixteenths: 1/8, dotted 1/8 ... see syn_fx_div */

static int32_t fx_drive(int32_t x)           /* a cubic soft clip; the amount is a pre-gain of 1x .. 8x */
{
    int64_t v, c;
    if (!syn.drive)
        return x;
    v = (int64_t)x * (256 + syn.drive * 1792 / 100) >> 8;
    v = v > (1 << 23) ? (1 << 23) : v < -(1 << 23) ? -(1 << 23) : v;
    c = (v * v >> 23) * v >> 23;
    return (int32_t)((v - c / 3) * 3 / 2);       /* the rails land at 1.0 again */
}
/* one frame: mono in -> stereo out (Q23-ish); the wet parts are mixed here */
static void fx_run(int32_t in, int32_t *outl, int32_t *outr)
{
    int32_t l, r, x = in, wl = 0, wr = 0;
    x = fx_drive(x);
    l = r = x;
    if (syn.chorus) {                          /* two taps, opposite LFO phase: a triangle sweeping 8..20 ms */
        uint32_t ph = fx_st.cph += 3u * 7000u;    /* ~0.8 Hz */
        int32_t tri = (int32_t)((ph >> 15) & 0xFFFFu);
        int32_t t1, t2, a, b;
        tri = tri < 32768 ? tri : 65535 - tri;     /* 0..32767 */
        t1 = 360 + (tri * 450 >> 15);
        t2 = 360 + ((32767 - tri) * 450 >> 15);
        fx_cho[fx_st.cw % CHO_N] = (int16_t)(x >> 8);
        a = fx_cho[(fx_st.cw - (uint32_t)t1) % CHO_N] << 8;
        b = fx_cho[(fx_st.cw - (uint32_t)t2) % CHO_N] << 8;
        fx_st.cw++;
        l = x + (int32_t)((int64_t)(a - x) * syn.chorus / 200);
        r = x + (int32_t)((int64_t)(b - x) * syn.chorus / 200);
    }
    if (syn.dly_mix) {                         /* a ping-pong: L feeds R, R feeds L */
        uint32_t div16 = DLY_DIV16[syn.dly_div % 6u], tn = syn_step_samples(1) * div16 / 1u;
        uint32_t tt = tn > DLY_N - 1u ? DLY_N - 1u : tn;
        int32_t dl = fx_dly[0][(fx_st.dw - tt) % DLY_N] << 8, dr = fx_dly[1][(fx_st.dw - tt) % DLY_N] << 8;
        int32_t fb = syn.dly_fb * 640 / 100;   /* up to 0.62 */
        fx_dly[0][fx_st.dw % DLY_N] = (int16_t)((l + (int32_t)((int64_t)dr * fb >> 10)) >> 8);
        fx_dly[1][fx_st.dw % DLY_N] = (int16_t)((r + (int32_t)((int64_t)dl * fb >> 10)) >> 8);
        fx_st.dw++;
        l += (int32_t)((int64_t)dl * syn.dly_mix / 100);
        r += (int32_t)((int64_t)dr * syn.dly_mix / 100);
    }
    if (syn.rev_mix) {                         /* 4 parallel damped combs, 2 allpasses, per channel */
        uint32_t ch, k, ty = syn.rev_type % REV_N;
        int32_t fbk = 18000 + syn.rev_size * 130 + (int32_t)ty * 1500;    /* Q15 feedback, ~0.55 .. 0.95 */
        for (ch = 0; ch < 2u; ch++) {
            int32_t s = ((ch ? r : l) >> 2), acc = 0;
            for (k = 0; k < 4u; k++) {
                uint32_t len = RV_LEN[ty][k] + (ch ? 23u : 0u);
                uint16_t *ci = &fx_st.comb_i[ch][k];
                int32_t y = fx_comb[ch][k][*ci], lp;
                lp = fx_st.comb_lp[ch][k] + ((y - fx_st.comb_lp[ch][k]) >> 1);      /* damping */
                fx_st.comb_lp[ch][k] = lp;
                fx_comb[ch][k][*ci] = (int16_t)syn_clamp((s >> 8) + (int32_t)((int64_t)lp * (fbk > 32000 ? 32000 : fbk) >> 15), -32768, 32767);
                if (++*ci >= len)
                    *ci = 0;
                acc += y;
            }
            acc = acc << 6;                    /* 4 combs of int16 -> Q23 ~ (acc << 8) / 4 */
            for (k = 0; k < 2u; k++) {         /* allpass g = 0.5 */
                uint16_t *ai = &fx_st.ap_i[ch][k];
                int32_t d = fx_ap[ch][k][*ai] << 8, y = d - acc / 2;
                fx_ap[ch][k][*ai] = (int16_t)syn_clamp((acc + (d >> 1)) >> 8, -32768, 32767);
                acc = y;
                if (++*ai >= AP_LEN[ty][k])
                    *ai = 0;
            }
            if (ch) wr = acc; else wl = acc;
        }
        l += (int32_t)((int64_t)wl * syn.rev_mix / 60);
        r += (int32_t)((int64_t)wr * syn.rev_mix / 60);
    }
    *outl = l;
    *outr = r;
}

/* ---------------------------------------------------------- the render --- */
static int32_t syn_limit(int32_t x)          /* a soft ceiling: linear to 0.7, then a smooth knee to 1.0 (2^23) */
{
    int64_t a = x < 0 ? -(int64_t)x : x, k = 5872025, r;            /* 0.7 */
    if (a <= k)
        return x;
    r = a - k;                               /* 0 .. : the part over the knee, squeezed asymptotically to 0.3 */
    r = r * 2516582 / (r + 2516582);        /* 0.3 * r / (r + 0.3) */
    a = k + r;
    return (int32_t)(x < 0 ? -a : a);
}
static void syn_apply_event(const syn_ev_t *e)
{
    uint8_t note = e->a;
    switch (e->kind) {
    case 1:
        if (e->src == 0u && syn.arp_on) syn_arp_press(note, 1);
        else syn_note_on_now(note, e->b ? e->b : 100u);
        if (e->src == 0u && !syn.arp_on && sq.run && sq.rec)      /* live recording (the arp records what it plays) */
            sq_rec_note(note, e->b ? e->b : 100u);
        break;
    case 2:
        if (e->src == 0u && syn.arp_on) syn_arp_press(note, 0);
        else syn_note_off_now(note);
        if (e->src == 0u && !syn.arp_on)
            sq_rec_release(note);
        break;
    case 3:
        syn_arp_clear();
        syn_all_off_now();
        break;
    case 4:
        syn_bend = ((int32_t)((e->b << 7) | e->a) - 8192) * syn.bend * 16 / 8192;
        break;
    case 5:
        syn_sustain_now(e->a >= 64u);
        break;
    case 8:
        syn_seq_start(e->a);
        break;
    case 6: {
        uint32_t i;
        for (i = 0; i < SYN_VOICES; i++)
            syn_v[i].n.live = 0;
        syn_arp_clear();
        syn_sq.arp_note_on = 255;
        sq.n_notes = 0;
        sq.hold = 0;
        break;
    }
    default:
        break;
    }
}
/* n frames (a multiple of 32) of the synth into lr[2 * n] (24-bit range, master 0..4096 = 0..1) */
static uint32_t syn_active_voices;
static void syn_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t b, i, k;
    while (syn_evt != syn_evh) {
        syn_apply_event(&syn_ev[syn_evt]);
        syn_evt = (uint8_t)((syn_evt + 1u) % SYN_EVQ);
    }
    for (b = 0; b + FM6_N <= n; b += FM6_N) {
        int32_t mix[FM6_N], bus[FM6_N], lfo_v, lfo_d, lf_fine = syn.tune * 2366 / 1000, fb;
        uint32_t live = 0;
        if (syn_dirty)
            syn_sync();
        syn_clock(FM6_N);
        lfo_v = fm6_lfo_sample(&syn_lfo);
        lfo_d = fm6_lfo_delay(&syn_lfo);
        fb = syn_eff[FP_FB];
        memset(mix, 0, sizeof mix);
        for (k = 0; k < SYN_VOICES; k++) {
            syn_voice_t *v = &syn_v[k];
            int32_t lf, lvl = syn_clamp(syn.bright, -64, 63) * 24;
            if (!v->n.live)
                continue;
            lf = fm6_note_logfreq(((int32_t)v->note + syn.transpose + syn_eff[FP_TRNSP] - 24) * 16 + syn_bend, lf_fine);
            if (!fm6_note_compute(&v->n, syn_eff, bus, lfo_v, lfo_d, lf, syn_eff_alg, fb, syn_dt, lvl << 16)) {
                if (fm6_note_done(&v->n, syn_eff, syn_eff_alg) || !v->key)
                    v->n.live = 0;
                continue;
            }
            live++;
            for (i = 0; i < FM6_N; i++) {
                int32_t x = bus[i] - v->n.dc;
                v->n.dc += x >> 10;           /* the one-pole high-pass of eng_fm6.c: FM puts sidebands at 0 Hz */
                mix[i] += x >> 4;
            }
            if (fm6_note_done(&v->n, syn_eff, syn_eff_alg))
                v->n.live = 0;
        }
        syn_active_voices = live;
        if (live > syn_voices_peak)
            syn_voices_peak = live;
        {
            int32_t gain = (int32_t)(master_q12 * (uint32_t)syn_clamp(syn.level, 0, 100) / 100u);
            for (i = 0; i < FM6_N; i++) {
                int32_t l, r;
                fx_run(mix[i], &l, &r);
                lr[2 * (b + i)] = syn_limit((int32_t)((int64_t)l * gain >> 12));
                lr[2 * (b + i) + 1] = syn_limit((int32_t)((int64_t)r * gain >> 12));
            }
        }
    }
}

static void syn_defaults(void)
{
    memset(&syn, 0, sizeof syn);
    syn.ver = SYN_SET_VER;
    syn.level = 80;
    syn.drive = 0; syn.chorus = 20; syn.dly_mix = 0; syn.dly_div = 2; syn.dly_fb = 40; syn.rev_mix = 20; syn.rev_type = REV_ROOM;
    syn.rev_size = 50;
    syn.bend = 2; syn.vel_sens = 100;
    syn.arp_mode = ARP_UP; syn.arp_rate = 1; syn.arp_oct = 1; syn.arp_gate = 60;
    syn.seq_pat = 0; syn.seq_clock = SQC_AUTO; syn.seq_trn = 0;
    syn.bpm10 = 1200;
    syn_load_patch(0);
    syn_dirty = 1;
}
static void syn_init(void)
{
    memset(syn_v, 0, sizeof syn_v);
    memset(&fx_st, 0, sizeof fx_st);
    memset(syn_bank, 0, sizeof syn_bank);
    syn_bank_dirty = 0;
    syn_evh = syn_evt = 0;
    syn_sq.n_held = 0;
    syn_sq.arp_note_on = 255;
    memset(&sq, 0, sizeof sq);
    sq_defaults();
    syn_sustain = 0;
    syn_bend = 0;
    syn_defaults();
}
