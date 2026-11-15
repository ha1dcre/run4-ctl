/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott (see LICENSE-MIT: the protocol and this codec are MIT, the rest of RUN4 CTL GPL-3.0)
 * RUN4 CTL protocol v1 (docs/PROTOCOL.md): one codec, compiled into the firmware and natively for the tests.
 * Plain C99, no libc, no floating point, no allocation. A frame is F0 7D 52 34 <cmd> <payload> F7, 7-bit bytes. */
#ifndef R4P_H
#define R4P_H
#include <stddef.h>
#include <stdint.h>

#define R4P_VER 1
#define R4P_MINOR 5               /* 1.1: LIST / LIST_PICK, the optional minor byte in HELLO / HELLO_ACK; 1.2: the PAGE
                                   * trailer (cursor, position, path), FOCUS, VALUE flags; 1.3: the host voice, the
                                   * voice list, the tape view, the patch backup; 1.4: the local modes (list, switch,
                                   * self-description, values, state); 1.5: the local mode's page (LVIEW, LVIEW_SET) */
#define R4P_CURSOR_NONE 127
#define R4P_VF_BIG 1              /* VALUE flags bit 0: show the big value view */
#define R4P_LIST_ROWS 7           /* items in one LIST frame (the window) */
#define R4P_STR_MAX 24            /* characters in a string */
#define R4P_NSLOTS 4
#define R4P_NBTN 14               /* plat.h B_* order */
#define R4P_NKEY 27
#define R4P_NENC 7                /* 0 SELECT, 1 ALGO, 2 PRESETS, 3..6 K1..K4 */
#define R4P_V14_MAX 16383
#define R4P_FRAME_MAX 640         /* the most a device takes between F0 and F7 (fm1-base); ours stay below (PAGE 1.2: 569) */
#define R4P_DEV_MAX 136           /* the most a frame from the device takes, F0 and F7 included (1.3 and 1.4) */

enum {                            /* device -> host */
    R4P_HELLO = 0x01, R4P_BTN = 0x02, R4P_ENC = 0x03, R4P_POT = 0x04, R4P_SYNC_REQ = 0x05, R4P_LIST_PICK = 0x06,
    R4P_HV_STATE = 0x07, R4P_VOICE_NAMES = 0x08, R4P_VOICE_DATA = 0x09,                 /* 1.3 */
    R4P_BK_PART = 0x0A, R4P_BK_END = 0x0B, R4P_BK_GET = 0x0C,                           /* 1.3 */
    R4P_MODES = 0x0D, R4P_MODE = 0x0E, R4P_DESC_HEAD = 0x0F, R4P_DESC_PAGE = 0x10, R4P_DESC_PARAM = 0x11,   /* 1.4 */
    R4P_DESC_LABELS = 0x12, R4P_DESC_END = 0x13, R4P_PVAL = 0x14, R4P_LSTATE = 0x15, R4P_LM_ERR = 0x16,     /* 1.4 */
    R4P_LVIEW = 0x17,                                                                                       /* 1.5 */
    /* host -> device */
    R4P_HELLO_ACK = 0x41, R4P_PAGE = 0x42, R4P_VALUE = 0x43, R4P_LEDS = 0x44, R4P_STATUS = 0x45, R4P_TOAST = 0x46, R4P_LIST = 0x47,
    R4P_FOCUS = 0x48,
    R4P_HV_ON = 0x49, R4P_HV_VOICE = 0x4A, R4P_HV_PARAM = 0x4B, R4P_NAMES_REQ = 0x4C, R4P_VOICE_GET = 0x4D,   /* 1.3 */
    R4P_TAPE_WAVE = 0x4E, R4P_TAPE_POS = 0x4F, R4P_METERS = 0x50,                                         /* 1.3 */
    R4P_BK_ACK = 0x51, R4P_BK_INFO = 0x52, R4P_BK_DATA = 0x53,                                            /* 1.3 */
    R4P_MODES_REQ = 0x54, R4P_MODE_SET = 0x55, R4P_DESC_REQ = 0x56, R4P_VALS_REQ = 0x57, R4P_PSET = 0x58, /* 1.4 */
    R4P_LVIEW_SET = 0x59                                                                                  /* 1.5 */
};
/* protocol 1.4: the local modes */
#define R4P_LM_STR 16             /* characters in a name, label, value text, page title, state text */
#define R4P_LM_UNIT 8             /* characters in a unit */
#define R4P_LM_MNAME 12           /* characters in a mode's name */
#define R4P_MODES_MAX 8           /* modes in MODES */
#define R4P_LM_PAGES 32           /* pages in a description */
#define R4P_LM_PARAMS 1024        /* parameters in a description */
#define R4P_LM_NSTATE 4           /* texts in LSTATE */
#define R4P_LM_LROWS 8            /* labels in one DESC_LABELS */
#define R4P_LM_LABELS 127         /* labels of one parameter */
#define R4P_PVAL_MAX 6            /* values in one PVAL */
#define R4P_PSET_MAX 16           /* pairs in one PSET */
#define R4P_S14_MIN (-8192)       /* a signed value on the wire: two 7-bit bytes of v + 8192 */
#define R4P_S14_MAX 8191
#define R4P_LVIEW_MAX 4           /* 1.5: knobs in LVIEW (K1..K4) */
#define R4P_LVIEW_NONE 16383      /* 1.5: LVIEW's id of a knob without a described parameter (never a parameter's id) */
enum { R4P_MID_RUN4, R4P_MID_SYNTH, R4P_MID_AMB };                   /* mode ids (stable) */
enum { R4P_MF_AVAIL = 1, R4P_MF_DESC = 2, R4P_MF_CTRL = 4 };          /* MODES / MODE flags */
enum { R4P_LK_NUMBER, R4P_LK_ENUM, R4P_LK_TOGGLE, R4P_LK_ACTION, R4P_LK_MASK, R4P_LK_COUNT };   /* parameter kinds */
enum { R4P_PF_READONLY = 1, R4P_PF_AUTO = 2, R4P_PF_BIPOLAR = 4, R4P_PF_ALL = 7 };              /* parameter flags */
enum { R4P_LS_PLAYING = 1, R4P_LS_FROZEN = 2, R4P_LS_EXTCLOCK = 4, R4P_LS_WAKING = 8, R4P_LS_RECORDING = 16,
       R4P_LS_ALL = 31 };                                                                       /* LSTATE flags */
enum { R4P_LE_OK, R4P_LE_NOMODE, R4P_LE_WRONGMODE, R4P_LE_NODESC, R4P_LE_UNKNOWN, R4P_LE_RANGE, R4P_LE_READONLY,
       R4P_LE_REFUSED, R4P_LE_CLOCK, R4P_LE_BUSY, R4P_LE_COUNT };                               /* LM_ERR codes */
/* protocol 1.3 */
enum { R4P_HV_OFF, R4P_HV_SOUNDING, R4P_HV_SUSPENDED };            /* HV_STATE state */
enum { R4P_HVV_STORED, R4P_HVV_PACKED, R4P_HVV_VCED };             /* HV_VOICE kind: a stored voice, 128 / 155 bytes */
enum { R4P_HVP_ALG, R4P_HVP_FB, R4P_HVP_BRIGHT, R4P_HVP_ENVSPD, R4P_HVP_VOLUME, R4P_HVP_VIBRATO, R4P_HVP_TREMOLO,
       R4P_HVP_LFOSPD, R4P_HVP_ATDEPTH, R4P_HVP_BEND, R4P_HVP_VELSENS, R4P_HVP_TRANSPOSE, R4P_HVP_TUNE, R4P_HVP_CHORUS,
       R4P_HVP_DELAY, R4P_HVP_REVERB, R4P_HVP_DRIVE, R4P_HVP_DLYTIME,
       R4P_HVP_MORPHSET, R4P_HVP_MORPHMODE, R4P_HVP_MORPH,                  /* 18..20: the morph (firmware 0.6 on) */
       R4P_HVP_COUNT };                                                      /* HV_PARAM ids */
extern const uint8_t R4P_HVP_MAX[R4P_HVP_COUNT];                    /* the largest wire value of each */
#define R4P_NVOICES 40            /* 8 factory + 32 user */
#define R4P_VOICE_HV 127          /* VOICE_GET / VOICE_DATA / HV_STATE: the host voice's own (sent or edited) voice */
#define R4P_NAMES_ROWS 8          /* names in one VOICE_NAMES frame */
#define R4P_NAME_MAX 10           /* characters in a voice name */
#define R4P_PACKED 128            /* a packed voice (the store's, a DX7 bank's record) */
#define R4P_VCED 155              /* a DX7 single voice */
#define R4P_WAVE_MAX 120          /* TAPE_WAVE columns */
#define R4P_NMETERS 5             /* T1..T4, master */
#define R4P_TP_PLAY 1             /* TAPE_POS flags */
#define R4P_TP_REC 2
#define R4P_TP_NOLOOP 4
#define R4P_BK_VOICES 32          /* a backup: the 32 user voices */
#define R4P_BK_SUMMARY 127        /* BK_GET: the summary (BK_INFO) */
enum { R4P_BK_STORED, R4P_BK_BAD, R4P_BK_FAILED };                 /* BK_ACK status */
enum { R4P_KIND_EMPTY, R4P_KIND_UNI, R4P_KIND_BI, R4P_KIND_ENUM, R4P_KIND_TOGGLE, R4P_KIND_COUNT };
enum { R4P_LED_OFF, R4P_LED_DIM, R4P_LED_ON, R4P_LED_BLINK };
enum { R4P_MODE_RUN4, R4P_MODE_SYNTH };
enum { R4P_PICK_OK, R4P_PICK_CANCEL, R4P_PICK_MOVE };     /* LIST_PICK actions */
enum { R4P_ST_PLAY = 1, R4P_ST_REC = 2, R4P_ST_OVERDUB = 4 };

/* Strings are Latin-1 bytes in memory (so "T\xD3" "LF" is TOLF with an acute O), NUL-terminated, at most 24 chars. */
typedef struct { char name[R4P_STR_MAX + 1]; uint8_t kind; uint16_t value; char text[R4P_STR_MAX + 1]; } r4p_slot_t;
typedef struct {
    char title[R4P_STR_MAX + 1], track[R4P_STR_MAX + 1];
    uint8_t r, g, b;              /* 0..127 on the wire; the display scales x2 */
    uint8_t page_idx, page_count, bank_idx, bank_count;
    r4p_slot_t slot[R4P_NSLOTS];
    /* protocol 1.2, the trailer: v12 = 1 when present (encode writes it, parse says whether it came); without it the
     * parser leaves cursor R4P_CURSOR_NONE, item_idx = item_count = 0, path empty */
    uint8_t v12, cursor;
    uint16_t item_idx, item_count;
    char path[R4P_STR_MAX + 1];
} r4p_page_t;
typedef struct { uint8_t slot; uint16_t value; char text[R4P_STR_MAX + 1]; uint8_t flags; } r4p_value_t;   /* flags: 1.2, 0 = absent */
typedef struct { uint8_t cursor; uint16_t item_idx, item_count; } r4p_focus_t;                            /* 1.2 */
typedef struct { uint8_t btn[R4P_NBTN], key[R4P_NKEY]; } r4p_leds_t;      /* R4P_LED_* each */
typedef struct { uint16_t bpm10, bar; uint8_t beat, flags; } r4p_status_t;
typedef struct { char text[R4P_STR_MAX + 1]; uint8_t dur100ms; } r4p_toast_t;
/* protocol 1.1: a scrollable list. total items 0..127 (0 closes the list); this frame carries items first .. first+n-1
 * (n <= R4P_LIST_ROWS); sel is the selected index (absolute, < total) */
typedef struct {
    char title[R4P_STR_MAX + 1];
    uint8_t r, g, b;
    uint8_t total, first, sel, n;
    char item[R4P_LIST_ROWS][R4P_STR_MAX + 1];
} r4p_list_t;
typedef struct { uint8_t index, action; } r4p_pick_t;
/* protocol 1.3 */
typedef struct { uint8_t state, chan, voice; char name[R4P_STR_MAX + 1]; } r4p_hvstate_t;   /* HV_STATE */
typedef struct { uint8_t on, chan; } r4p_hvon_t;                                           /* HV_ON (chan 0..15) */
typedef struct { uint8_t kind, idx; uint8_t data[R4P_VCED]; } r4p_hvvoice_t;  /* HV_VOICE: idx (STORED) or data (128 / 155) */
typedef struct { uint8_t n, id[R4P_HVP_COUNT], val[R4P_HVP_COUNT]; } r4p_hvparam_t;        /* HV_PARAM: 1..21 pairs */
typedef struct {                                                                           /* VOICE_NAMES */
    uint8_t first, total, n;
    uint8_t used[R4P_NAMES_ROWS];
    char name[R4P_NAMES_ROWS][R4P_NAME_MAX + 1];
} r4p_names_t;
typedef struct { uint8_t idx, used; uint8_t data[R4P_PACKED]; } r4p_voice_t;   /* VOICE_DATA (data only when used), BK_PART / BK_DATA (always) */
typedef struct { uint8_t count; uint32_t crc; } r4p_bksum_t;                    /* BK_END, BK_INFO */
typedef struct { uint8_t track; char label[R4P_STR_MAX + 1]; uint8_t n; uint8_t peak[R4P_WAVE_MAX]; } r4p_wave_t;   /* n 0 = off */
typedef struct { uint16_t play, in, out; uint8_t flags; } r4p_tpos_t;           /* TAPE_POS, 0..16383 across the wave */
typedef struct { uint8_t level[R4P_NMETERS], clip; } r4p_meters_t;              /* METERS: 0..127 (127 + 2 x dBFS), clip bits */
/* protocol 1.4 */
typedef struct { uint8_t id, flags; char name[R4P_LM_MNAME + 1]; } r4p_modeent_t;
typedef struct { uint8_t cur, n; r4p_modeent_t m[R4P_MODES_MAX]; } r4p_modes_t;                 /* MODES */
typedef struct { uint8_t mode, flags; uint32_t hash; } r4p_mode_t;                              /* MODE, DESC_END (flags 0) */
typedef struct {                                                                                /* DESC_HEAD */
    uint8_t mode; uint32_t hash; uint8_t npages; uint16_t nparams; uint8_t nstate;
    char title[R4P_LM_STR + 1];
    char state[R4P_LM_NSTATE][R4P_LM_STR + 1];
} r4p_dhead_t;
typedef struct { uint8_t page; char title[R4P_LM_STR + 1]; } r4p_dpage_t;                       /* DESC_PAGE */
typedef struct {                                                                                /* DESC_PARAM */
    uint16_t id; uint8_t page, kind, flags, step;
    int16_t lo, hi, def;
    uint8_t nlabels;
    char name[R4P_LM_STR + 1], unit[R4P_LM_UNIT + 1];
    int16_t value;
    char text[R4P_LM_STR + 1];
} r4p_dparam_t;
typedef struct { uint16_t id; uint8_t first, total, n; char label[R4P_LM_LROWS][R4P_LM_STR + 1]; } r4p_dlabels_t;
typedef struct { uint8_t mode, n; uint16_t id[R4P_PVAL_MAX]; int16_t v[R4P_PVAL_MAX]; char text[R4P_PVAL_MAX][R4P_LM_STR + 1]; } r4p_pval_t;
typedef struct { uint8_t mode, n; uint16_t id[R4P_PSET_MAX]; int16_t v[R4P_PSET_MAX]; } r4p_pset_t;
typedef struct { uint8_t mode, flags, n; char text[R4P_LM_NSTATE][R4P_LM_STR + 1]; } r4p_lstate_t;
typedef struct { uint8_t mode, code; uint16_t id; } r4p_lerr_t;
/* protocol 1.5: the page the device shows (LVIEW: page < R4P_LM_PAGES, n 0..4, the ids of K1..Kn, R4P_LVIEW_NONE for a
 * knob without a described parameter) and the host's pick (LVIEW_SET: mode, page 0..127; n and id unused) */
typedef struct { uint8_t mode, page, n; uint16_t id[R4P_LVIEW_MAX]; } r4p_lview_t;

typedef struct {
    uint8_t cmd;
    union {
        struct { uint8_t proto, fw_major, fw_minor, mode, minor; } hello;   /* minor: protocol minor, 0 when absent */
        struct { uint8_t proto, minor; } ack;
        struct { uint8_t id, state; } btn;
        struct { uint8_t id; int8_t delta; } enc;
        struct { uint16_t value; } pot;
        r4p_page_t page;
        r4p_value_t value;
        r4p_leds_t leds;
        r4p_status_t status;
        r4p_toast_t toast;
        r4p_list_t list;
        r4p_pick_t pick;
        r4p_focus_t focus;
        r4p_hvstate_t hvstate;
        r4p_hvon_t hvon;
        r4p_hvvoice_t hvvoice;
        r4p_hvparam_t hvparam;
        r4p_names_t names;
        r4p_voice_t voice;        /* VOICE_DATA, BK_PART, BK_DATA */
        r4p_bksum_t bksum;        /* BK_END, BK_INFO */
        r4p_wave_t wave;
        r4p_tpos_t tpos;
        r4p_meters_t meters;
        struct { uint8_t v; } b1;  /* VOICE_GET idx, BK_GET part, BK_ACK status (NAMES_REQ: none); 1.4: MODE_SET,
                                    * DESC_REQ, VALS_REQ mode (MODES_REQ: none) */
        r4p_modes_t modes;        /* 1.4 */
        r4p_mode_t mode;          /* MODE, DESC_END */
        r4p_dhead_t dhead;
        r4p_dpage_t dpage;
        r4p_dparam_t dparam;
        r4p_dlabels_t dlabels;
        r4p_pval_t pval;
        r4p_pset_t pset;
        r4p_lstate_t lstate;
        r4p_lerr_t lerr;
        r4p_lview_t lview;        /* 1.5: LVIEW, LVIEW_SET (mode, page) */
    } u;
} r4p_msg_t;

/* Encode msg (any command) as a whole frame F0 .. F7 into buf. Returns the length, 0 on a bad field or a small buf. */
size_t r4p_encode(uint8_t *buf, size_t cap, const r4p_msg_t *msg);
/* Parse the bytes BETWEEN F0 and F7 (what app_sysex gets). 1 = ok, 0 = not ours (other header), -1 = ours but malformed. */
int r4p_parse_body(const uint8_t *p, size_t n, r4p_msg_t *out);
/* Parse a whole frame F0 .. F7. Same results; a frame without F0/F7 is -1. */
int r4p_parse(const uint8_t *frame, size_t n, r4p_msg_t *out);

/* device side shorthands (whole frame into buf, return length) */
size_t r4p_hello(uint8_t *buf, size_t cap, uint8_t fw_major, uint8_t fw_minor, uint8_t mode);
size_t r4p_btn(uint8_t *buf, size_t cap, uint8_t id, uint8_t down);
size_t r4p_enc(uint8_t *buf, size_t cap, uint8_t id, int delta);          /* delta clamped to +-63 */
size_t r4p_pot(uint8_t *buf, size_t cap, uint16_t v14);
size_t r4p_sync_req(uint8_t *buf, size_t cap);
size_t r4p_list_pick(uint8_t *buf, size_t cap, uint8_t index, uint8_t action);

/* protocol 1.3: the backup's checksum, CRC-32 (IEEE 802.3, as zlib's crc32): start with crc = 0, feed bytes in order */
uint32_t r4p_crc32(uint32_t crc, const uint8_t *p, size_t n);
/* the bytes a backup part adds to it: k, used, then its 128 data bytes */
uint32_t r4p_crc32_part(uint32_t crc, const r4p_voice_t *part);

/* protocol 1.4: the description's hash, FNV-1a 32 (offset 0x811C9DC5, prime 0x01000193): start with R4P_FNV0 */
#define R4P_FNV0 0x811C9DC5u
uint32_t r4p_fnv(uint32_t h, const uint8_t *p, size_t n);
/* the bytes of an encoded description frame (F0 .. F7) the hash covers: DESC_HEAD from cmd to its end without the five
 * hash bytes, DESC_PAGE and DESC_LABELS from cmd to the end, DESC_PARAM from cmd up to (not with) its value and text */
uint32_t r4p_desc_hash_add(uint32_t h, const uint8_t *frame, size_t n);

/* encoder detents coalesced per frame: add every detent seen, take once per frame; what clamping left over carries on */
typedef struct { int16_t acc[R4P_NENC]; } r4p_enc_acc_t;
void r4p_enc_add(r4p_enc_acc_t *a, uint8_t id, int delta);
int r4p_enc_take(r4p_enc_acc_t *a, uint8_t id);          /* -63..63, 0 = nothing to send */

/* LED states 3 to a byte, LSB first: 14 buttons then 27 keys = 41 entries = 14 bytes */
#define R4P_LED_BYTES 14
void r4p_leds_pack(const r4p_leds_t *l, uint8_t out[R4P_LED_BYTES]);
void r4p_leds_unpack(const uint8_t in[R4P_LED_BYTES], r4p_leds_t *l);

/* text helpers: UTF-8 to Latin-1 (what a host does before encoding; characters above U+00FF become '?'), and a glyph
 * fold for fonts without Latin-1: the base letter of an accented one (O acute -> O). */
size_t r4p_utf8_to_latin1(char *dst, size_t cap, const char *utf8);
char r4p_fold(unsigned char c);
void r4p_fold_str(char *dst, const char *src);           /* dst as big as src */

#endif
