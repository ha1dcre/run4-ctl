/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott
 * RUN4 CTL controller SDK. See README.md and INTEGRATION.md.
 */
#ifndef RUN4CTL_SDK_H
#define RUN4CTL_SDK_H
#include "r4p.h"
#ifdef __cplusplus
extern "C" {
#endif

#define R4CTL_SDK_VERSION "0.2.0"
#define R4CTL_CONTROLLER_MINOR 2 /* default controller profile; the full codec covers 1.8 */
#define R4CTL_HELLO_MS 1000u

enum r4ctl_button_id {
    R4CTL_FX, R4CTL_SEL, R4CTL_ENV, R4CTL_LFO, R4CTL_EDIT, R4CTL_GLO,
    R4CTL_HOME, R4CTL_SAVE, R4CTL_ARP, R4CTL_SEQ, R4CTL_PLAY, R4CTL_REC,
    R4CTL_OCT_DOWN, R4CTL_OCT_UP
};
enum r4ctl_encoder_id {
    R4CTL_SELECT, R4CTL_ALGORITHM, R4CTL_PRESETS,
    R4CTL_KNOB1, R4CTL_KNOB2, R4CTL_KNOB3, R4CTL_KNOB4
};
enum r4ctl_mode { R4CTL_CONTROLLER = 0, R4CTL_LOCAL = 1 };
enum r4ctl_display_profile {
    R4CTL_DISPLAY_TILES = R4CTL_CONTROLLER_MINOR,
    R4CTL_DISPLAY_TAPE = 3, /* waveform, loop/play position and meters */
    R4CTL_DISPLAY_ROLL = 7 /* Tape feedback plus Poly/TÓLF piano roll */
};

typedef struct {
    void *user;
    /* Required: copy/queue the entire F0..F7 frame and return 1, or return 0
     * without sending any bytes. Never block. Pointers expire on return. */
    int (*send_sysex)(void *user, const uint8_t *frame, size_t length);
    /* Optional: the same contract for a complete ordinary MIDI message.
     * These are raw bytes, NOT USB-MIDI event packets or BLE timestamps. */
    int (*send_midi)(void *user, const uint8_t *message, size_t length);
    /* Called after a valid ACK and for supported host display messages.
     * Message storage expires on the next receive call. Copy what you retain. */
    void (*message)(void *user, const r4p_msg_t *message);
    /* Handshake state changes; native/controller mode is queried separately. */
    void (*connection)(void *user, int connected);
    /* Update a LIST highlight immediately after a local encoder move. */
    void (*list_selection)(void *user, uint8_t index);
    /* Close the LIST view after a locally accepted OK/CANCEL. */
    void (*list_closed)(void *user);
} r4ctl_callbacks_t;

/* Public for static allocation, private in meaning: do not edit these fields.
 * No heap, threads, audio engine, hardware driver or global mutable state.
 * One instance per MIDI peer/transport; serialize every call on its owner loop.
 * Callbacks may query state, but must not re-enter mutating SDK calls; queue
 * incoming data for the owner loop instead. */
typedef struct {
    r4ctl_callbacks_t callbacks;
    r4p_msg_t incoming;
    uint8_t rx[R4P_FRAME_MAX];
    uint32_t hello_at, master_at;
    uint16_t rx_n, master, master_sent, suppressed;
    uint8_t fw_major, fw_minor, mode, link, connected, minor, display_profile;
    uint8_t hello_sent, need_sync, have_master, master_valid;
    uint8_t list_total, list_first, list_n, list_sel;
    uint8_t rx_open, rx_overflow;
} r4ctl_t;

/* Return 0 for missing send_sysex, NULL arguments, or firmware version >127. */
int r4ctl_init(r4ctl_t *device, const r4ctl_callbacks_t *callbacks,
              uint8_t firmware_major, uint8_t firmware_minor);
/* Opt in to display feedback through the existing message callback. Call
 * after init, before set_link(1), or with the link down. Returns 0 for an
 * unsupported profile or an active link. Native mode still advertises 1.2. */
int r4ctl_set_display_profile(r4ctl_t *device, enum r4ctl_display_profile profile);
void r4ctl_set_link(r4ctl_t *device, int available);
/* Notify RUN4 on BOTH entering and leaving the controller screen: the helper
 * re-handshakes with mode 0 or 1. LOCAL keeps native UI/engines under your control. */
int r4ctl_set_mode(r4ctl_t *device, enum r4ctl_mode mode);
void r4ctl_poll(r4ctl_t *device, uint32_t monotonic_ms);
int r4ctl_connected(const r4ctl_t *device);
uint8_t r4ctl_host_minor(const r4ctl_t *device); /* negotiated min(host, advertised), 0 if unpaired */

/* Whole SysEx frames include F0/F7. 0 foreign/offline, -1 malformed RUN4 frame,
 * 1 recognized RUN4 frame (may be ignored before ACK or outside the negotiated profile).
 * Byte input tolerates arbitrary fragments, interleaved realtime and overflow;
 * feed ordinary MIDI to your native parser too. No callback for native MIDI. */
int r4ctl_receive_frame(r4ctl_t *device, const uint8_t *frame, size_t length);
int r4ctl_receive_byte(r4ctl_t *device, uint8_t byte);

/* 1 accepted, 0 not accepted: keep the physical edge/delta and retry after poll.
 * Buttons/encoders go only to a paired controller host. LIST interactions are
 * translated to LIST_PICK; matching button releases stay local.
 * Encoder delta must be -63..63; split larger deltas without losing the rest. */
int r4ctl_button(r4ctl_t *device, uint8_t button, int down);
int r4ctl_encoder(r4ctl_t *device, uint8_t encoder, int delta);
/* Remember the latest absolute 0..16383 value even before ACK. poll sends it
 * after SYNC_REQ, then at most every 20ms (6 counts, or exact end points). */
int r4ctl_master(r4ctl_t *device, uint16_t value);

/* Keyboard helpers work before ACK and in LOCAL too: old RUN4 versions and
 * other MIDI hosts still get notes. Caller owns key->note mapping, octave,
 * velocity, held-note/channel tracking and retries. Channels here are 0..15.
 * No duplicate transport or note messages are generated automatically. */
int r4ctl_note_on(r4ctl_t *device, uint8_t channel, uint8_t note, uint8_t velocity);
int r4ctl_note_off(r4ctl_t *device, uint8_t channel, uint8_t note);
int r4ctl_realtime(r4ctl_t *device, uint8_t status);
#ifdef __cplusplus
}
#endif
#endif
