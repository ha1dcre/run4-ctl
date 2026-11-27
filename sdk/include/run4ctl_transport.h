/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott */
#ifndef RUN4CTL_TRANSPORT_H
#define RUN4CTL_TRANSPORT_H

#include "run4ctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use one SDK session per selected cable/group and MIDI peer. Inbound helpers
 * filter by cable/group and return 1 for a recognized RUN4 frame, 0 for other
 * input, or -1 for invalid framing on the selected cable/group. Other MIDI
 * still belongs to the platform's native MIDI handler. */
int r4ctl_receive_usb_midi1(r4ctl_t *device, uint8_t cable, const uint8_t packet[4]);
int r4ctl_receive_ump(r4ctl_t *device, uint8_t group, const uint32_t *words, size_t word_count);

/* Encode one complete F0..F7 frame or ordinary MIDI 1.0 message into caller
 * storage. The capacity and return value use USB events (four bytes each) or
 * native-endian UMP words, respectively. Zero leaves the destination untouched.
 * Reserve/queue the entire result before returning success to the SDK. */
size_t r4ctl_pack_usb_midi1(uint8_t cable, const uint8_t *message, size_t length,
                           uint8_t packets[][4], size_t capacity);
size_t r4ctl_pack_ump(uint8_t group, const uint8_t *message, size_t length,
                     uint32_t *words, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
