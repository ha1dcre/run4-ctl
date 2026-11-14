/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL protocol v1 (docs/PROTOCOL.md): one codec, compiled into the firmware and natively for the tests.
 * Plain C99, no libc, no floating point, no allocation. A frame is F0 7D 52 34 <cmd> <payload> F7, 7-bit bytes. */
#ifndef R4P_H
#define R4P_H
#include <stddef.h>
#include <stdint.h>

#define R4P_VER 1
#define R4P_MINOR 2               /* 1.1: LIST / LIST_PICK, the optional minor byte in HELLO / HELLO_ACK; 1.2: the PAGE
                                   * trailer (cursor, position, path), FOCUS, VALUE flags */
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

enum {                            /* device -> host */
    R4P_HELLO = 0x01, R4P_BTN = 0x02, R4P_ENC = 0x03, R4P_POT = 0x04, R4P_SYNC_REQ = 0x05, R4P_LIST_PICK = 0x06,
    /* host -> device */
    R4P_HELLO_ACK = 0x41, R4P_PAGE = 0x42, R4P_VALUE = 0x43, R4P_LEDS = 0x44, R4P_STATUS = 0x45, R4P_TOAST = 0x46, R4P_LIST = 0x47,
    R4P_FOCUS = 0x48
};
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
