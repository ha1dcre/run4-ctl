/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, Synth mode: the sound. A six-operator FM engine (Felucca's fm6_core.c, a C port of Dexed's msfa, Apache-2.0,
 * see NOTICE) behind a voice pool with stealing, a patch (the 155-byte DX7 voice layout), DX7 packed voice records
 * and bank / single-voice SysEx, a drive / chorus / tempo delay / reverb chain, an arpeggiator and a step sequencer
 * (synth_seq.c: 16 patterns of up to 64 steps, chords, ties, live and step recording, MIDI clock).
 * Plain C, integer only, no platform calls (the app glue is in app.c / synth_ui.c): native tests link it alone.
 *
 * Threading, as on the device: syn_render() runs in the audio context; the main loop (keys, UI) calls syn_event() (a
 * small ring) and edits the settings, which the render reads at its block starts. */
#include <stddef.h>
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
#define MO_N 10                              /* the morph's values (MO_ID below) */
enum { MO_OFF, MO_KNOB, MO_DRIFT, MO_NMODES = MO_DRIFT + 10 };   /* MO_DRIFT + i: a ping-pong of MO_SECS[i] seconds */
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
    uint8_t at_depth;                        /* key pressure into BRIGHT, 0..63 at full pressure (was a spare byte: 0
                                              * in stores written before; Synth mode leaves it 0, the host voice sets it) */
    uint16_t bpm10;                          /* 400..2400 */
    /* version 3: the keys (scale and chord), mutate, the song (stores of version 2 end here: migrated, defaults) */
    uint8_t scale, root, keymode, midi_scale;  /* SCL_*; root 0..11 (C..B); KM_SNAP / KM_INKEY; MIDI in snapped too */
    uint8_t chord, inv, spread;              /* CH_*; inversion 0..3; SPR_* */
    uint8_t mut_amt;                         /* MUTATE: 0 small, 1 medium, 2 wild */
    uint8_t song;                            /* the sequencer plays the chain (synth_seq.c) */
    uint8_t rsv3[3];
    /* version 4: the morph (HOME's second sub-page; the host voice's HV_PARAM 18..20); stores of version 3 end here */
    int8_t mo_a[MO_N], mo_b[MO_N];           /* the snapshots A and B of the MO_ID values, in their settings' units */
    uint8_t mo_cap;                          /* bit 0: A taken, bit 1: B */
    uint8_t mo_mode;                         /* MO_OFF, MO_KNOB (mo_pos), MO_DRIFT + i */
    uint8_t mo_pos;                          /* 0..100: A .. B (KNOB; a drift starts there) */
    uint8_t rsv4;
} syn_set_t;
#define SYN_SET_VER 4                        /* 2: the patterns moved to their own objects (synth_seq.c); 3: keys, song;
                                              * 4: the morph */
#define SYN_SET_V2_SIZE 186u                 /* a version 2 store: the same fields up to bpm10 */
#define SYN_SET_V3_SIZE 198u                 /* a version 3 store: the same fields up to rsv3 */
_Static_assert(offsetof(syn_set_t, bpm10) == 184u && offsetof(syn_set_t, scale) == SYN_SET_V2_SIZE &&
               offsetof(syn_set_t, mo_a) == SYN_SET_V3_SIZE && sizeof(syn_set_t) == 222u, "settings store layout");
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
    uint8_t at;                              /* its key's pressure (poly aftertouch), 0..127 */
    uint8_t ofs;                             /* it started this many frames into a sub-block: its output that much later */
    uint32_t age;
    int32_t carry[FM6_N];                    /* (ofs > 0) the last ofs frames of its sub-block, for the next one */
} syn_voice_t;
static syn_voice_t syn_v[SYN_VOICES] PLAT_POOL;
static uint32_t syn_age;
static uint8_t syn_eff[FP_SIZE + 1];         /* the patch as the voices play it: the macros applied */
static uint8_t syn_eff_alg;
static int32_t syn_dt[6];
static fm6_lfo_t syn_lfo;
static int32_t syn_bend;                     /* pitch bend, 1/16 semitones */
static uint8_t syn_sustain;
static uint8_t syn_chan_at;                  /* channel pressure, 0..127 (with each voice's own: the larger counts) */
static uint32_t syn_voices_peak;

static int32_t syn_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------- parameter locks --- */
/* The sequencer's parameter locks (synth_seq.c) override these while a locked step plays: the sound is the settings
 * and the patch with the overrides on top, nothing of them is ever written into syn (a voice or the settings saved
 * meanwhile are the base values). Set in the audio context (sq_fire), read by the render (syn_sync, the effects). */
enum { LK_ALG, LK_FB, LK_BRIGHT, LK_ENVSPD, LK_LEVEL, LK_LFOSPD, LK_LFODLY, LK_PMD, LK_AMD, LK_DRIVE, LK_CHORUS,
       LK_DLYMIX, LK_REVMIX, LK_DLYTIME, LK_DLYFB, LK_REVSIZE, LK_N };
static const int8_t LK_LO[LK_N] = {0, 0, -64, -64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int8_t LK_HI[LK_N] = {31, 7, 63, 63, 100, 99, 99, 99, 99, 100, 100, 100, 100, 5, 100, 100};
static struct { uint32_t mask; int16_t v[LK_N]; } syn_lk;   /* bit id: locked to v[id] */
/* The morph (A / B): while it is on (both snapshots taken, MODE not OFF) its ten values replace the settings' at
 * control rate, a block at a time (syn_morph); the sequencer's locks still win over it. Audio context. */
static const uint8_t MO_ID[MO_N] = {LK_BRIGHT, LK_FB, LK_ENVSPD, LK_LEVEL, LK_DRIVE, LK_CHORUS, LK_DLYMIX, LK_REVMIX,
                                    LK_DLYFB, LK_REVSIZE};
static const uint16_t MO_SECS[MO_NMODES - MO_DRIFT] = {10, 20, 30, 45, 60, 90, 120, 180, 240, 300};
static struct {
    uint32_t mask; int16_t v[LK_N];          /* bit id: the morph sets v[id] */
    uint32_t ph;                             /* the drift's phase (a full A -> B -> A cycle is 2^32) */
    int32_t pos;                             /* where it is, 0..65536 (A .. B), smoothed */
    uint8_t was_mode, rephase;               /* the mode last block; the main loop moved the knob (a drift goes on from there) */
} syn_mo;
static int32_t syn_lkv(uint32_t id, int32_t base)
{
    return (syn_lk.mask >> id) & 1u ? syn_lk.v[id] : (syn_mo.mask >> id) & 1u ? syn_mo.v[id] : base;
}
static int32_t syn_lock_base(uint32_t id)     /* the base value a lock overrides (the settings, the patch) */
{
    switch (id) {
    case LK_ALG: return syn.cur[FP_ALG] & 31;
    case LK_FB: return syn.cur[FP_FB];
    case LK_BRIGHT: return syn.bright;
    case LK_ENVSPD: return syn.env_spd;
    case LK_LEVEL: return syn.level;
    case LK_LFOSPD: return syn.cur[FP_LFS];
    case LK_LFODLY: return syn.cur[FP_LFD];
    case LK_PMD: return syn.cur[FP_LPMD];
    case LK_AMD: return syn.cur[FP_LAMD];
    case LK_DRIVE: return syn.drive;
    case LK_CHORUS: return syn.chorus;
    case LK_DLYMIX: return syn.dly_mix;
    case LK_REVMIX: return syn.rev_mix;
    case LK_DLYTIME: return syn.dly_div;
    case LK_DLYFB: return syn.dly_fb;
    default: return syn.rev_size;
    }
}
static volatile uint8_t syn_shed;            /* the overload guard's step now, 0 = everything (below: syn_shed_calm) */
/* the effects' amounts for one block, the locks on top (the render sets it once a block) */
static struct { int32_t drive, chorus, dly_mix, dly_div, dly_fb, rev_mix, rev_size; } fxv;
static void syn_fxv(void)
{
    fxv.drive = syn_lkv(LK_DRIVE, syn.drive);
    fxv.chorus = syn_lkv(LK_CHORUS, syn.chorus);
    fxv.dly_mix = syn_lkv(LK_DLYMIX, syn.dly_mix);
    fxv.dly_div = syn_lkv(LK_DLYTIME, syn.dly_div);
    fxv.dly_fb = syn_lkv(LK_DLYFB, syn.dly_fb);
    fxv.rev_mix = syn_lkv(LK_REVMIX, syn.rev_mix);
    fxv.rev_size = syn_lkv(LK_REVSIZE, syn.rev_size);
    if (syn_shed >= 3u)                      /* (the overload guard's third step) */
        fxv.chorus = fxv.dly_mix = 0;
}

/* the morph's snapshot (0 = A, 1 = B) of a setting set's values now */
static void syn_morph_take(syn_set_t *s, uint32_t which)
{
    const int32_t v[MO_N] = {s->bright, s->cur[FP_FB], s->env_spd, s->level, s->drive, s->chorus, s->dly_mix, s->rev_mix,
                             s->dly_fb, s->rev_size};
    uint32_t i;
    for (i = 0; i < MO_N; i++)
        (which ? s->mo_b : s->mo_a)[i] = (int8_t)v[i];
    s->mo_cap = (uint8_t)(s->mo_cap | 1u << (which & 1u));
}
static int syn_morph_on(void) { return syn.mo_cap == 3u && syn.mo_mode > MO_OFF && syn.mo_mode < MO_NMODES; }
/* once a render call of n frames: where the morph is, and its values */
static void syn_morph(uint32_t n)
{
    uint32_t i, m = 0;
    int32_t tgt;
    if (!syn_morph_on()) {
        if (syn_mo.mask) {
            syn_mo.mask = 0;
            syn_dirty = 1;
        }
        syn_mo.was_mode = 0;
        return;
    }
    if (syn.mo_mode == MO_KNOB) {
        tgt = (int32_t)(syn.mo_pos > 100u ? 100u : syn.mo_pos) * 65536 / 100;
    } else {
        uint32_t per = MO_SECS[syn.mo_mode - MO_DRIFT] * 44100u;
        if (syn_mo.was_mode < MO_DRIFT || syn_mo.rephase)       /* a drift starts (or the knob moved): from there, to B */
            syn_mo.ph = (uint32_t)((uint64_t)(syn.mo_pos > 100u ? 100u : syn.mo_pos) * 0x7FFFFFFFu / 100u);
        syn_mo.ph += (uint32_t)(((uint64_t)n << 32) / per);
        tgt = (int32_t)((syn_mo.ph < 0x80000000u ? syn_mo.ph : ~syn_mo.ph) >> 15);
    }
    syn_mo.rephase = 0;
    if (!syn_mo.was_mode)
        syn_mo.pos = tgt;                    /* (just on: no glide from wherever it was) */
    syn_mo.was_mode = syn.mo_mode;
    syn_mo.pos += (tgt - syn_mo.pos) / 4 + (tgt > syn_mo.pos ? 1 : tgt < syn_mo.pos ? -1 : 0);
    if ((syn_mo.pos > tgt ? syn_mo.pos - tgt : tgt - syn_mo.pos) < 4)
        syn_mo.pos = tgt;
    for (i = 0; i < MO_N; i++) {
        uint32_t id = MO_ID[i];
        int32_t a = syn.mo_a[i], b = syn.mo_b[i], v = a + (((b - a) * syn_mo.pos + 32768) >> 16);
        v = v < LK_LO[id] ? LK_LO[id] : v > LK_HI[id] ? LK_HI[id] : v;
        if ((id == LK_FB || id == LK_ENVSPD) && (!((syn_mo.mask >> id) & 1u) || syn_mo.v[id] != v))
            syn_dirty = 1;                   /* (the patch's own: the voices take them at the next sync) */
        syn_mo.v[id] = (int16_t)v;
        m |= 1u << id;
    }
    if (!syn_mo.mask)
        syn_dirty = 1;
    syn_mo.mask = m;
}

static void syn_sync(void)                   /* the patch, with the macros; once per block when something changed */
{
    uint8_t *p = syn_eff;
    uint32_t k, car;
    int32_t meg = syn_clamp(syn_lkv(LK_ENVSPD, syn.env_spd), -64, 63) * 40 / 64;
    memcpy(p, syn.cur, FP_SIZE + 1u);
    if (syn_lk.mask | syn_mo.mask) {         /* the sequencer's locks (and the morph) on the patch's own parameters */
        p[FP_ALG] = (uint8_t)syn_lkv(LK_ALG, p[FP_ALG]);
        p[FP_FB] = (uint8_t)syn_lkv(LK_FB, p[FP_FB]);
        p[FP_LFS] = (uint8_t)syn_lkv(LK_LFOSPD, p[FP_LFS]);
        p[FP_LFD] = (uint8_t)syn_lkv(LK_LFODLY, p[FP_LFD]);
        p[FP_LPMD] = (uint8_t)syn_lkv(LK_PMD, p[FP_LPMD]);
        p[FP_LAMD] = (uint8_t)syn_lkv(LK_AMD, p[FP_LAMD]);
    }
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
static uint32_t syn_vmax(void);
static syn_voice_t *syn_alloc(void)          /* a free voice, else the oldest released one, else the oldest (of syn_vmax) */
{
    uint32_t i, vm = syn_vmax();
    syn_voice_t *best = 0;
    for (i = 0; i < vm; i++)
        if (!syn_v[i].n.live)
            return &syn_v[i];
    for (i = 0; i < vm; i++)
        if (!syn_v[i].key && !syn_v[i].held && (!best || syn_v[i].age < best->age))
            best = &syn_v[i];
    if (best)
        return best;
    best = &syn_v[0];
    for (i = 1; i < vm; i++)
        if (syn_v[i].age < best->age)
            best = &syn_v[i];
    return best;
}
static int32_t syn_vel(int32_t vel)
{
    int32_t s = syn.vel_sens;
    return syn_clamp(64 + (vel - 64) * s / 100, 1, 127);
}
static uint8_t syn_ev_ofs;                    /* a placed note-on's frame in the sub-block it falls in (syn_timed) */
static void syn_note_on_now(uint8_t note, uint8_t vel)
{
    syn_voice_t *v;
    uint32_t i, k;
    int32_t t;
    if (syn_dirty)
        syn_sync();
    for (i = 0; i < SYN_VOICES; i++)            /* the same note again: retrigger its voice */
        if (syn_v[i].n.live && syn_v[i].note == note && syn_v[i].key)
            break;
    v = i < SYN_VOICES ? &syn_v[i] : syn_alloc();
    if (!v->n.live)                             /* (sample-accurate starts: what it still owes from before, or nothing) */
        memset(v->carry, 0, sizeof v->carry);
    else
        for (k = v->ofs; k < FM6_N; k++)
            v->carry[k] = 0;
    v->ofs = syn_ev_ofs;
    t = (int32_t)note + syn.transpose + syn_eff[FP_TRNSP] - 24;
    fm6_note_init(&v->n, syn_eff, t, syn_vel(vel), i == SYN_VOICES && !v->n.live);
    if (i == SYN_VOICES && v->n.live)           /* stolen: start from silence, not from the old note's phases */
        fm6_note_init(&v->n, syn_eff, t, syn_vel(vel), 1);
    v->note = note;
    v->key = 1;
    v->held = 0;
    v->at = 0;
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
 * 5 sustain, 6 panic (voices cut), 7 patch / settings changed, 9 poly pressure (a note, b pressure), 10 channel
 * pressure (a) */
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
static int sq_song(void) { return syn.song && sq_ch.len; }
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

/* --------------------------------------------------------- overload guard --- */
/* When a render runs late (fm1-base calls app_overload when a block took over 90 % of its time: device/main_fm1.c), the
 * sound sheds load a step at a time: 1 the reverb on two of its four combs, 2 six voices, 3 no chorus and no delay, 4
 * four voices. A step comes back after SHED_CALM_MS of renders under SHED_CALM_PM of their time (hysteresis: well
 * below where it triggered); the effects' lines start from silence again. The render's own time is measured by the
 * app (app.c app_render) and handed to syn_shed_calm. Audio context; the main loop only reads syn_shed. */
#define SHED_MAX 4u
#define SHED_CALM_MS 3000u
#define SHED_CALM_PM 500u                    /* 50 % of a block's time */
static volatile uint8_t syn_shed_hit;        /* app_overload: a late render since the last block */
static uint32_t syn_shed_calm_smp, syn_shed_peak;
static uint32_t syn_vmax(void) { return syn_shed >= 4u ? 4u : syn_shed >= 2u ? 6u : 8u; }
static void syn_shed_overload(void) { syn_shed_hit = 1; }
static void syn_shed_step(void);             /* (below the effects: their lines) */
/* once a render of n frames that took load_pm per mille of its time (0..): a late one sheds, calm ones bring back */
static void syn_shed_calm(uint32_t n, uint32_t load_pm)
{
    if (syn_shed_hit) {
        syn_shed_hit = 0;
        syn_shed_calm_smp = 0;
        if (syn_shed < SHED_MAX) {
            syn_shed++;
            if (syn_shed > syn_shed_peak)
                syn_shed_peak = syn_shed;
            syn_shed_step();
        }
        return;
    }
    if (!syn_shed)
        return;
    if (load_pm >= SHED_CALM_PM) {
        syn_shed_calm_smp = 0;
        return;
    }
    syn_shed_calm_smp += n;
    if (syn_shed_calm_smp >= SHED_CALM_MS * 441u / 10u) {
        syn_shed_calm_smp = 0;
        syn_shed--;
        syn_shed_step();
    }
}

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
    if (!fxv.drive)
        return x;
    v = (int64_t)x * (256 + fxv.drive * 1792 / 100) >> 8;
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
    if (fxv.chorus) {                          /* two taps, opposite LFO phase: a triangle sweeping 8..20 ms */
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
        l = x + (int32_t)((int64_t)(a - x) * fxv.chorus / 200);
        r = x + (int32_t)((int64_t)(b - x) * fxv.chorus / 200);
    }
    if (fxv.dly_mix) {                         /* a ping-pong: L feeds R, R feeds L */
        uint32_t div16 = DLY_DIV16[(uint32_t)fxv.dly_div % 6u], tn = syn_step_samples(1) * div16 / 1u;
        uint32_t tt = tn > DLY_N - 1u ? DLY_N - 1u : tn;
        int32_t dl = fx_dly[0][(fx_st.dw - tt) % DLY_N] << 8, dr = fx_dly[1][(fx_st.dw - tt) % DLY_N] << 8;
        int32_t fb = fxv.dly_fb * 640 / 100;   /* up to 0.62 */
        fx_dly[0][fx_st.dw % DLY_N] = (int16_t)((l + (int32_t)((int64_t)dr * fb >> 10)) >> 8);
        fx_dly[1][fx_st.dw % DLY_N] = (int16_t)((r + (int32_t)((int64_t)dl * fb >> 10)) >> 8);
        fx_st.dw++;
        l += (int32_t)((int64_t)dl * fxv.dly_mix / 100);
        r += (int32_t)((int64_t)dr * fxv.dly_mix / 100);
    }
    if (fxv.rev_mix) {                         /* 4 parallel damped combs, 2 allpasses, per channel */
        uint32_t ch, k, ty = syn.rev_type % REV_N, nc = syn_shed ? 2u : 4u;     /* (shed: two combs, twice as loud) */
        int32_t fbk = 18000 + fxv.rev_size * 130 + (int32_t)ty * 1500;    /* Q15 feedback, ~0.55 .. 0.95 */
        for (ch = 0; ch < 2u; ch++) {
            int32_t s = ((ch ? r : l) >> 2), acc = 0;
            for (k = 0; k < nc; k++) {
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
            acc = acc << (nc == 4u ? 6 : 7);   /* 4 combs of int16 -> Q23 ~ (acc << 8) / 4 */
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
        l += (int32_t)((int64_t)wl * fxv.rev_mix / 60);
        r += (int32_t)((int64_t)wr * fxv.rev_mix / 60);
    }
    *outl = l;
    *outr = r;
}

/* a shed step taken or given back (audio context): voices over the limit stop; effects coming back start silent */
static void syn_shed_step(void)
{
    uint32_t i, vm = syn_vmax();
    for (i = vm; i < SYN_VOICES; i++)
        syn_v[i].n.live = 0, syn_v[i].key = syn_v[i].held = 0;
    if (syn_shed == 0u)                      /* the two combs that rested */
        for (i = 0; i < 2u; i++) {
            memset(fx_comb[i][2], 0, sizeof fx_comb[i][2]);
            memset(fx_comb[i][3], 0, sizeof fx_comb[i][3]);
            fx_st.comb_lp[i][2] = fx_st.comb_lp[i][3] = 0;
        }
    if (syn_shed == 2u) {                    /* (from 3: the chorus and the delay come back) */
        memset(fx_dly, 0, sizeof fx_dly);
        memset(fx_cho, 0, sizeof fx_cho);
    }
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
        syn_chan_at = 0;
        break;
    case 9: {
        uint32_t i;
        for (i = 0; i < SYN_VOICES; i++)
            if (syn_v[i].n.live && syn_v[i].key && syn_v[i].note == note)
                syn_v[i].at = e->b;
        break;
    }
    case 10:
        syn_chan_at = note;
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
    case 11:                                 /* MIDI real-time (clock, start, stop) at its frame */
        sq_rt_ofs = syn_ev_ofs;
        sq_realtime(e->a);
        sq_rt_ofs = 0;
        break;
    case 6: {
        uint32_t i;
        for (i = 0; i < SYN_VOICES; i++)
            syn_v[i].n.live = 0;
        syn_arp_clear();
        syn_sq.arp_note_on = 255;
        sq.n_notes = 0;
        sq.hold = 0;
        syn_chan_at = 0;
        break;
    }
    default:
        break;
    }
}
/* MIDI in placed in the block (fm1-base midi_stamp, app.c midi_pump): each message comes with its frame in the block
 * about to be rendered, and syn_render applies it at the sub-block (FM6_N frames) it falls in; a note-on starts its
 * voice at the very frame (syn_voice_t ofs: the voice's output delayed by 0..31 frames for its life), a note-off, a
 * controller or a clock at that sub-block (within 0.73 ms), the clock's tempo measured to the frame. As in Mutable
 * Dreams' md_block_begin / md_midi_at (Kid Bunka, MIT OR GPL-3.0-only): what came in during the last block plays at
 * the same point of this one, a constant one-block latency instead of up to a block of jitter. Audio context. */
#define SYN_TQ 32
static struct { syn_ev_t e; uint16_t frame; } syn_tq[SYN_TQ];
static uint8_t syn_tq_n;
static void syn_apply_event(const syn_ev_t *e);
static void syn_timed(const syn_ev_t *e, uint32_t frame)
{
    uint32_t i;
    if (!frame || syn_tq_n >= SYN_TQ) {          /* (now: at the block's start; a full queue too) */
        syn_apply_event(e);
        return;
    }
    i = syn_tq_n++;
    while (i && syn_tq[i - 1u].frame > frame) {  /* in order of their frames */
        syn_tq[i] = syn_tq[i - 1u];
        i--;
    }
    syn_tq[i].e = *e;
    syn_tq[i].frame = (uint16_t)frame;
}
static void syn_timed_due(uint32_t b, uint32_t end)   /* the placed events before frame end, at sub-block b */
{
    while (syn_tq_n && syn_tq[0].frame < end) {
        syn_ev_t e = syn_tq[0].e;
        uint32_t f = syn_tq[0].frame;
        syn_tq_n--;
        memmove(syn_tq, syn_tq + 1, syn_tq_n * sizeof syn_tq[0]);
        syn_ev_ofs = (uint8_t)(f > b && f < b + FM6_N ? f - b : 0u);
        syn_apply_event(&e);
        syn_ev_ofs = 0;
    }
}
/* where in a block of n frames a message stamped t24 (plat_ticks24) lands: the block before this one started at prev
 * and lasted span ticks; a message from before it comes first (0), one after it last (n - 1) */
static uint32_t syn_midi_frame(uint32_t t24, uint32_t prev, uint32_t span, uint32_t n)
{
    int32_t since = (int32_t)(t24 - prev);
    if (since <= 0 || !span || !n)
        return 0;
    if ((uint32_t)since >= span)
        return n - 1u;
    return (uint32_t)((uint64_t)(uint32_t)since * n / span);
}
/* the scope's tap (synth_ui.c draws it, after Felucca's HOME scope): every other output frame, mono, 16 bit, into a ring
 * the main loop copies; the render only stores (16 stores a 32-frame block) */
#define SYN_SCOPE_N 512u
static int16_t syn_scope[SYN_SCOPE_N];
static volatile uint32_t syn_scope_w;
/* n frames (a multiple of 32) of the synth into lr[2 * n] (24-bit range, master 0..4096 = 0..1) */
static uint32_t syn_active_voices;
static volatile uint8_t syn_hold;            /* the main loop swaps the settings (syn_swap): the render stays silent */
static void syn_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t b, i, k;
    if (syn_hold) {
        memset(lr, 0, 2u * n * sizeof lr[0]);
        return;
    }
    while (syn_evt != syn_evh) {
        syn_apply_event(&syn_ev[syn_evt]);
        syn_evt = (uint8_t)((syn_evt + 1u) % SYN_EVQ);
    }
    syn_morph(n);
    for (b = 0; b + FM6_N <= n; b += FM6_N) {
        int32_t mix[FM6_N], bus[FM6_N], lfo_v, lfo_d, lf_fine = syn.tune * 2366 / 1000, fb;
        uint32_t live = 0;
        syn_timed_due(b, b + FM6_N);
        if (syn_dirty)
            syn_sync();
        syn_clock(FM6_N);
        syn_fxv();                           /* (after the clock: a locked step that just started counts) */
        lfo_v = fm6_lfo_sample(&syn_lfo);
        lfo_d = fm6_lfo_delay(&syn_lfo);
        fb = syn_eff[FP_FB];
        memset(mix, 0, sizeof mix);
        for (k = 0; k < SYN_VOICES; k++) {
            syn_voice_t *v = &syn_v[k];
            int32_t lf, lvl = syn_clamp(syn_lkv(LK_BRIGHT, syn.bright), -64, 63) * 24;
            if (!v->n.live)
                continue;
            if (syn.at_depth)                /* pressure opens the modulators: up to at_depth BRIGHT steps */
                lvl += (int32_t)(v->at > syn_chan_at ? v->at : syn_chan_at) * syn_clamp(syn.at_depth, 0, 63) * 24 / 127;
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
                bus[i] = x >> 4;
            }
            if (!v->ofs) {
                for (i = 0; i < FM6_N; i++)
                    mix[i] += bus[i];
            } else {                          /* started inside a sub-block: its output ofs frames later */
                uint32_t o = v->ofs;
                for (i = 0; i < o; i++)
                    mix[i] += v->carry[i];
                for (i = o; i < FM6_N; i++)
                    mix[i] += bus[i - o];
                for (i = 0; i < o; i++)
                    v->carry[i] = bus[FM6_N - o + i];
            }
            if (fm6_note_done(&v->n, syn_eff, syn_eff_alg))
                v->n.live = 0;
        }
        syn_active_voices = live;
        if (live > syn_voices_peak)
            syn_voices_peak = live;
        {
            int32_t gain = (int32_t)(master_q12 * (uint32_t)syn_clamp(syn_lkv(LK_LEVEL, syn.level), 0, 100) / 100u);
            for (i = 0; i < FM6_N; i++) {
                int32_t l, r;
                fx_run(mix[i], &l, &r);
                lr[2 * (b + i)] = syn_limit((int32_t)((int64_t)l * gain >> 12));
                lr[2 * (b + i) + 1] = syn_limit((int32_t)((int64_t)r * gain >> 12));
            }
            {
                uint32_t w = syn_scope_w;
                for (i = 0; i < FM6_N; i += 2u)
                    syn_scope[w++ & (SYN_SCOPE_N - 1u)] = (int16_t)((lr[2 * (b + i)] + lr[2 * (b + i) + 1]) >> 9);
                syn_scope_w = w;
            }
        }
    }
    syn_timed_due(n, 0xFFFFu);               /* (a frame past this block: at the next one's start) */
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
    syn.mut_amt = 1;                         /* (scale chromatic, key mode SNAP, no chord, song off: all 0) */
    syn_load_patch(0);
    syn_dirty = 1;
}
/* RUN4 mode's host voice (protocol 1.3) plays the same engine with its own settings: `syn` is always what sounds, and
 * syn_alt holds the other set (Synth mode's while the host voice is in `syn`, and the other way round). syn_swap trades
 * them between two blocks: the render is held silent meanwhile, the voices, the effects' lines and the event ring
 * start empty (nothing of the one sound rings on in the other). Main loop only. */
static syn_set_t syn_alt;
static void syn_reset_sound(void)
{
    uint32_t i;
    for (i = 0; i < SYN_VOICES; i++)
        syn_v[i].n.live = 0, syn_v[i].key = syn_v[i].held = 0;
    memset(fx_dly, 0, sizeof fx_dly);
    memset(fx_cho, 0, sizeof fx_cho);
    memset(fx_comb, 0, sizeof fx_comb);
    memset(fx_ap, 0, sizeof fx_ap);
    memset(&fx_st, 0, sizeof fx_st);
    syn_evh = syn_evt = 0;
    syn_sq.n_held = 0;
    syn_sq.arp_note_on = 255;
    syn_sq.arp_step_open = 0;
    syn_sustain = 0;
    syn_bend = 0;
    syn_chan_at = 0;
    syn_active_voices = 0;
    syn_lk.mask = 0;
    syn_tq_n = 0;
    syn_mo.mask = 0;
    syn_mo.was_mode = 0;
}
static void syn_swap(void)
{
    syn_set_t t;
    syn_hold = 1;
    t = syn;
    syn = syn_alt;
    syn_alt = t;
    syn_reset_sound();
    syn_dirty = 1;
    syn_hold = 0;
}
/* the host voice's settings at the start: the factory voice 1, Synth mode's default effects, no arpeggiator, pressure
 * into BRIGHT at half depth */
static void syn_host_defaults(syn_set_t *out)
{
    syn_set_t keep = syn;
    syn_defaults();
    syn.at_depth = 32;
    *out = syn;
    syn = keep;
    syn_dirty = 1;
}
/* a version 4 settings store with its new fields in their ranges; from: the size of an older store loaded into it
 * (SYN_SET_V2_SIZE, SYN_SET_V3_SIZE: the fields after it get their defaults), 0 = a version 4 one */
static void syn_set_fix(syn_set_t *t, uint32_t from)
{
    uint32_t i;
    if (from) {
        memset((uint8_t *)t + from, 0, sizeof *t - from);
        if (from <= SYN_SET_V2_SIZE)
            t->mut_amt = 1;
        t->ver = SYN_SET_VER;
    }
    if (t->scale >= 11u) t->scale = 0;
    if (t->root > 11u) t->root = 0;
    if (t->keymode > 1u) t->keymode = 0;
    if (t->midi_scale > 1u) t->midi_scale = 0;
    if (t->chord >= 11u) t->chord = 0;
    if (t->inv > 3u) t->inv = 0;
    if (t->spread > 2u) t->spread = 0;
    if (t->mut_amt > 2u) t->mut_amt = 1;
    if (t->song > 1u) t->song = 0;
    t->mo_cap &= 3u;
    if (t->mo_mode >= MO_NMODES) t->mo_mode = MO_OFF;
    if (t->mo_pos > 100u) t->mo_pos = 0;
    for (i = 0; i < MO_N; i++) {
        uint32_t id = MO_ID[i];
        if (t->mo_a[i] < LK_LO[id] || t->mo_a[i] > LK_HI[id]) t->mo_a[i] = (int8_t)LK_LO[id];
        if (t->mo_b[i] < LK_LO[id] || t->mo_b[i] > LK_HI[id]) t->mo_b[i] = (int8_t)LK_LO[id];
    }
}

/* ---------------------------------------------------------- scale and chord --- */
/* The keys can play in a scale: SNAP moves each key's note to the nearest one in the scale (a tie goes down), IN KEY
 * makes the 16 white keys the scale's notes in a row from the root (the black keys are silent). A chord turns one key
 * into up to four notes (the arpeggiator and the recording get them all). Pure functions: the keys (app.c), MIDI in. */
enum { SCL_CHROM, SCL_MAJOR, SCL_MINOR, SCL_DORIAN, SCL_PHRYG, SCL_LYDIAN, SCL_MIXO, SCL_HARM, SCL_MAJPENT, SCL_MINPENT,
       SCL_BLUES, SCL_N };
enum { KM_SNAP, KM_INKEY };
enum { CH_OFF, CH_MAJ, CH_MIN, CH_7, CH_MAJ7, CH_MIN7, CH_SUS2, CH_SUS4, CH_POWER, CH_SCALE3, CH_SCALE4, CH_N };
enum { SPR_CLOSE, SPR_OPEN, SPR_WIDE, SPR_N };
#define B12(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
static const uint16_t SCL_MASK[SCL_N] = {
    0xFFFu, B12(0, 2, 4, 5, 7, 9, 11), B12(0, 2, 3, 5, 7, 8, 10), B12(0, 2, 3, 5, 7, 9, 10), B12(0, 1, 3, 5, 7, 8, 10),
    B12(0, 2, 4, 6, 7, 9, 11), B12(0, 2, 4, 5, 7, 9, 10), B12(0, 2, 3, 5, 7, 8, 11), B12(0, 2, 4, 7, 9, 9, 9),
    B12(0, 3, 5, 7, 10, 10, 10), B12(0, 3, 5, 6, 7, 10, 10)};
static int syn_in_scale(int32_t n, uint32_t scale, uint32_t root)
{
    return (SCL_MASK[scale % SCL_N] >> (uint32_t)(((n - (int32_t)root) % 12 + 12) % 12)) & 1u;
}
static int32_t syn_snap(int32_t n, uint32_t scale, uint32_t root)   /* the nearest note in the scale, a tie down */
{
    int32_t d;
    if (scale % SCL_N == SCL_CHROM || syn_in_scale(n, scale, root))
        return n;
    for (d = 1; d < 12; d++) {
        if (n - d >= 0 && syn_in_scale(n - d, scale, root))
            return n - d;
        if (n + d <= 127 && syn_in_scale(n + d, scale, root))
            return n + d;
    }
    return n;
}
/* the note i scale steps up from `base` (a note of the scale; i may be negative) */
static int32_t syn_degree(int32_t base, int32_t i, uint32_t scale, uint32_t root)
{
    uint32_t m = SCL_MASK[scale % SCL_N], cnt = 0, k, pc = (uint32_t)(((base - (int32_t)root) % 12 + 12) % 12);
    int32_t j = 0, oct = base - (int32_t)pc;              /* oct: the root at or below base */
    int8_t deg[12];
    for (k = 0; k < 12u; k++)
        if ((m >> k) & 1u) {
            if (k <= pc) j = (int32_t)cnt;                /* base's place in the scale (the one below if it is not in it) */
            deg[cnt++] = (int8_t)k;
        }
    i += j;
    while (i < 0)
        i += (int32_t)cnt, oct -= 12;
    return oct + 12 * (i / (int32_t)cnt) + deg[i % (int32_t)cnt];
}
/* the note a key plays (IN KEY: white key w = degree w from the root of octave 3, + 12 per octave shift; SNAP: snapped);
 * -1 = silent (a black key in IN KEY) */
static int32_t syn_key_note(int32_t chromatic, int white_idx, int32_t octave)
{
    if (syn.scale % SCL_N != SCL_CHROM && syn.keymode == KM_INKEY) {
        if (white_idx < 0)
            return -1;
        return syn_degree(48 + (int32_t)syn.root + 12 * octave, white_idx, syn.scale, syn.root);
    }
    return syn_snap(chromatic, syn.scale, syn.root);
}
static void syn_sort4(uint8_t *v, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && v[j - 1] > v[j]; j--) {
            uint8_t t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
}
/* the chord a note makes (out: up to 4 notes, ascending; returns how many; CH_OFF: the note alone) */
static uint32_t syn_chord(int32_t note, uint32_t chord, uint32_t inv, uint32_t spread, uint32_t scale, uint32_t root, uint8_t *out)
{
    static const int8_t IV[CH_N][4] = {{0, -1, -1, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11},
                                       {0, 3, 7, 10}, {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 7, 12, -1}, {0, 0, 0, -1}, {0, 0, 0, 0}};
    int32_t v[4];
    uint32_t n = 0, i, k;
    chord %= CH_N;
    for (i = 0; i < 4u && IV[chord][i] >= 0; i++) {
        if (chord == CH_SCALE3 || chord == CH_SCALE4)   /* thirds of the scale (a major or minor triad / seventh in key) */
            v[n++] = scale % SCL_N == SCL_CHROM ? note + IV[chord == CH_SCALE3 ? CH_MAJ : CH_7][i]
                     : syn_degree(syn_snap(note, scale, root), 2 * (int32_t)i, scale, root);
        else
            v[n++] = note + IV[chord][i];
    }
    for (k = 0; k < inv && k + 1u < n; k++) {         /* inversions: the lowest goes up an octave */
        int32_t lo = 0;
        for (i = 1; i < n; i++)
            if (v[i] < v[lo]) lo = (int32_t)i;
        v[lo] += 12;
    }
    if (n >= 3u && spread == SPR_OPEN) {              /* open: the second lowest an octave up */
        int32_t lo = 0, lo2 = -1;
        for (i = 1; i < n; i++)
            if (v[i] < v[lo]) lo = (int32_t)i;
        for (i = 0; i < n; i++)
            if ((int32_t)i != lo && (lo2 < 0 || v[i] < v[lo2])) lo2 = (int32_t)i;
        v[lo2] += 12;
    } else if (n >= 2u && spread == SPR_WIDE) {       /* wide: the lowest an octave down, under the rest */
        int32_t lo = 0;
        for (i = 1; i < n; i++)
            if (v[i] < v[lo]) lo = (int32_t)i;
        v[lo] -= 12;
    }
    for (i = 0, k = 0; i < n; i++) {                  /* in range, each once */
        uint32_t j;
        if (v[i] < 0 || v[i] > 127)
            continue;
        for (j = 0; j < k && out[j] != (uint8_t)v[i]; j++)
            ;
        if (j == k)
            out[k++] = (uint8_t)v[i];
    }
    syn_sort4(out, k);
    return k;
}

/* --------------------------------------------------------------- mutate --- */
/* A random variation of a voice (the 155-byte patch), amount 0 small, 1 medium, 2 wild. Musical limits: the ratios
 * stay on a grid (whole numbers, and halves; carriers 0.5 .. 4), detune near the centre, the carriers stay audible
 * (output level 75 and up, the loudest 90 and up, an envelope that rises to 85 and up within a reasonable attack,
 * that releases), the pitch envelope, the operators' level 4 and the name stay. Returns what changed: bit k = OPk+1,
 * bit 6 the algorithm or feedback, bit 7 the LFO. */
static uint32_t syn_rnd(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r >> 8; }
static int32_t syn_jit(uint32_t *r, int32_t v, int32_t span, int32_t lo, int32_t hi)    /* v + -span..span */
{
    return syn_clamp(v + (int32_t)(syn_rnd(r) % (uint32_t)(2 * span + 1)) - span, lo, hi);
}
static uint32_t syn_mutate(uint8_t *v, uint32_t amt, uint32_t *r)
{
    static const uint8_t MOD_FC[14] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 14}, CAR_FC[7] = {0, 1, 1, 1, 2, 2, 3};
    static const uint8_t P_OP[3] = {40, 70, 100}, SPAN_L[3] = {4, 10, 25}, SPAN_E[3] = {3, 8, 20}, SPAN_D[3] = {1, 2, 4};
    uint8_t before[FP_SIZE];
    uint32_t chg = 0, alg, car, k, j, loud = 0;
    amt = amt > 2u ? 2u : amt;
    memcpy(before, v, FP_SIZE);
    alg = v[FP_ALG] & 31u;
    if (amt == 2u && syn_rnd(r) % 2u)
        alg = syn_rnd(r) % 32u;
    else if (amt == 1u && syn_rnd(r) % 100u < 15u)
        alg = (uint32_t)syn_jit(r, (int32_t)alg, 1, 0, 31);
    v[FP_ALG] = (uint8_t)alg;
    if (amt == 2u)
        v[FP_FB] = (uint8_t)(syn_rnd(r) % 8u);
    else if (syn_rnd(r) % 100u < (amt ? 60u : 30u))
        v[FP_FB] = (uint8_t)syn_jit(r, v[FP_FB], amt ? 2 : 1, 0, 7);
    car = fm6_carriers(alg);
    for (k = 0; k < 6u; k++) {                         /* k: OP1..OP6 (OP1 is the last block of the patch) */
        uint8_t *op = v + (5u - k) * FP_OP;
        int carrier = (car >> (5u - k)) & 1u;
        if (syn_rnd(r) % 100u < P_OP[amt]) {
            if (!op[FP_MODE] && amt && syn_rnd(r) % 100u < (amt == 2u ? 70u : 40u)) {     /* the ratio, on the grid */
                op[FP_FC] = carrier ? CAR_FC[syn_rnd(r) % 7u] : MOD_FC[syn_rnd(r) % 14u];
                op[FP_FF] = (uint8_t)(syn_rnd(r) % 4u == 0u ? 50u : 0u);                  /* x.0, sometimes x.5 */
            }
            op[FP_DET] = (uint8_t)syn_jit(r, op[FP_DET], SPAN_D[amt], 0, 14);
            op[FP_OL] = (uint8_t)syn_jit(r, op[FP_OL], carrier ? SPAN_L[amt] / 2 : SPAN_L[amt], 0, 99);
            for (j = 0; j < 4u; j++)
                op[FP_R1 + j] = (uint8_t)syn_jit(r, op[FP_R1 + j], SPAN_E[amt], 0, 99);
            for (j = 0; j < 3u; j++)
                op[FP_L1 + j] = (uint8_t)syn_jit(r, op[FP_L1 + j], SPAN_E[amt], 0, 99);
            if (amt == 2u)
                op[FP_KVS] = (uint8_t)syn_jit(r, op[FP_KVS], 1, 0, 7);
        }
        if (carrier) {                                 /* audible: level, a rising envelope, an attack, a release */
            uint32_t pk = op[FP_L1] > op[FP_L1 + 1] ? op[FP_L1] : op[FP_L1 + 1];
            if (op[FP_OL] < 75u) op[FP_OL] = 75;
            if (pk < 85u) op[FP_L1] = 85;
            if (op[FP_R1] < 40u) op[FP_R1] = 40;
            if (op[FP_R1 + 3] < 20u) op[FP_R1 + 3] = 20;
            if (op[FP_OL] > loud) loud = op[FP_OL];
        }
    }
    if (loud < 90u)                                    /* the loudest carrier at 90 and up */
        for (k = 0; k < 6u; k++)
            if ((car >> (5u - k)) & 1u) {
                uint8_t *op = v + (5u - k) * FP_OP;
                if (op[FP_OL] == loud) { op[FP_OL] = 90; break; }
            }
    if (amt && syn_rnd(r) % 100u < (amt == 2u ? 60u : 30u)) {   /* the LFO */
        v[FP_LFS] = (uint8_t)(amt == 2u ? 10u + syn_rnd(r) % 81u : (uint32_t)syn_jit(r, v[FP_LFS], 10, 0, 99));
        if (amt == 2u) {
            v[FP_LPMD] = (uint8_t)(syn_rnd(r) % 16u);
            v[FP_LAMD] = (uint8_t)(syn_rnd(r) % 16u);
            v[FP_LFW] = (uint8_t)(syn_rnd(r) % 6u);
        }
    }
    fp_sanitize(v);
    for (k = 0; k < 6u; k++)
        if (memcmp(before + (5u - k) * FP_OP, v + (5u - k) * FP_OP, FP_OP))
            chg |= 1u << k;
    if (before[FP_ALG] != v[FP_ALG] || before[FP_FB] != v[FP_FB])
        chg |= 1u << 6;
    if (before[FP_LFS] != v[FP_LFS] || before[FP_LPMD] != v[FP_LPMD] || before[FP_LAMD] != v[FP_LAMD] || before[FP_LFW] != v[FP_LFW])
        chg |= 1u << 7;
    return chg;
}

#include "synth_gen.c"                       /* the SEQ page's generators and phrase tools */

/* ------------------------------------------------------------ MIDI learn --- */
/* The rules of MIDI learn (app.c / synth_learn.c keep the bindings): which CCs may be learned (Mutable Dreams'
 * midi_learn.inc learn_safe, Kid Bunka, MIT OR GPL-3.0-only: 1..119 without data entry 6, the LSBs 32..63, the pedal 64
 * and the (N)RPN / increment numbers 96..101), and the pick-up (soft takeover): a controller changes a value only once
 * it has come near it (within tol) or crossed it since its last message, so a knob at another position never makes the
 * sound jump. */
#define ML_NONE (-32768)
static int syn_ml_safe(uint32_t cc) { return cc >= 1u && cc < 120u && cc != 6u && cc != 64u && !(cc >= 32u && cc <= 63u) && !(cc >= 96u && cc <= 101u); }
static int syn_ml_pickup(int32_t v, int32_t cur, int32_t last, int32_t tol)
{
    if ((v > cur ? v - cur : cur - v) <= tol)
        return 1;
    return last != ML_NONE && ((last <= cur && v >= cur) || (last >= cur && v <= cur));
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
