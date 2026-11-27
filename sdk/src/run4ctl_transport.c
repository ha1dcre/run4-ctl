/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott */
#include "run4ctl_transport.h"
#include <string.h>

static int is_sysex(const uint8_t *message, size_t length)
{
    return length >= 2 && message[0] == 0xF0 && message[length - 1] == 0xF7;
}

static int data_bytes(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (bytes[i] & 0x80u) return 0;
    return 1;
}

static int valid_message(const uint8_t *message, size_t length)
{
    if (!message || !length) return 0;
    if (is_sysex(message, length)) return data_bytes(message + 1, length - 2);
    return r4ctl_midi_message_size(message[0]) == length && data_bytes(message + 1, length - 1);
}

static uint8_t message_cin(uint8_t status, size_t length)
{
    if (status < 0xF0 || status >= 0xF8) return status >> 4;
    if (length == 1) return 5;
    return (uint8_t)length;
}

int r4ctl_receive_usb_midi1(r4ctl_t *device, uint8_t cable, const uint8_t packet[4])
{
    static const uint8_t lengths[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    if (!packet || cable > 15 || packet[0] >> 4 != cable) return 0;
    unsigned cin = packet[0] & 0x0Fu;
    if (cin < 2) return 0;
    size_t length = lengths[cin];
    const uint8_t *bytes = packet + 1;
    int sysex_end = cin >= 5 && cin <= 7 && bytes[length - 1] == 0xF7;
    if (cin == 4 || sysex_end) {
        size_t start = bytes[0] == 0xF0;
        size_t end = length - (size_t)sysex_end;
        if (start > end || !data_bytes(bytes + start, end - start)) {
            r4ctl_receive_byte(device, 0xF6);
            return -1;
        }
    } else if (cin != 15 && (!valid_message(bytes, length) || message_cin(bytes[0], length) != cin)) {
        r4ctl_receive_byte(device, 0xF6);
        return -1;
    }
    int result = 0;
    for (size_t i = 0; i < length; ++i) {
        int received = r4ctl_receive_byte(device, bytes[i]);
        if (received) result = received;
    }
    return result;
}

size_t r4ctl_pack_usb_midi1(uint8_t cable, const uint8_t *message, size_t length,
                           uint8_t packets[][4], size_t capacity)
{
    if (cable > 15 || !packets || !valid_message(message, length)) return 0;
    int sysex = is_sysex(message, length);
    size_t count = sysex ? length / 3u + (length % 3u != 0u) : 1;
    if (count > capacity) return 0;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * 3u;
        size_t remaining = length - offset;
        size_t payload = remaining > 3 ? 3 : remaining;
        uint8_t cin = sysex ? (remaining > 3 ? 4 : (uint8_t)(4u + payload))
                            : message_cin(message[0], length);
        packets[i][0] = (uint8_t)((cable << 4) | cin);
        memset(packets[i] + 1, 0, 3);
        memcpy(packets[i] + 1, message + offset, payload);
    }
    return count;
}

int r4ctl_receive_ump(r4ctl_t *device, uint8_t group, const uint32_t *words, size_t count)
{
    if (!words || !count || group > 15 || words[0] >> 28 != 3 ||
        ((words[0] >> 24) & 0xFu) != group) return 0;
    unsigned status = (words[0] >> 20) & 0xFu;
    size_t length = (words[0] >> 16) & 0xFu;
    if (count != 2 || status > 3 || length > 6) {
        r4ctl_receive_byte(device, 0xF6);
        return -1;
    }
    uint8_t bytes[6] = {
        (uint8_t)(words[0] >> 8), (uint8_t)words[0],
        (uint8_t)(words[1] >> 24), (uint8_t)(words[1] >> 16),
        (uint8_t)(words[1] >> 8), (uint8_t)words[1]
    };
    if (!data_bytes(bytes, length)) {
        r4ctl_receive_byte(device, 0xF6);
        return -1;
    }
    if (status < 2) r4ctl_receive_byte(device, 0xF0);
    r4ctl_receive_bytes(device, bytes, length);
    return status == 0 || status == 3 ? r4ctl_receive_byte(device, 0xF7) : 0;
}

size_t r4ctl_pack_ump(uint8_t group, const uint8_t *message, size_t length,
                     uint32_t *words, size_t capacity)
{
    if (group > 15 || !words || !capacity || !valid_message(message, length)) return 0;
    uint32_t address = (uint32_t)group << 24;
    if (!is_sysex(message, length)) {
        uint32_t type = message[0] < 0xF0 ? 2u : 1u;
        uint32_t word = (type << 28) | address | (uint32_t)message[0] << 16;
        if (length > 1) word |= (uint32_t)message[1] << 8;
        if (length > 2) word |= message[2];
        words[0] = word;
        return 1;
    }
    size_t body = length - 2;
    size_t count = body ? (body - 1u) / 6u + 1u : 1u;
    if (count > capacity / 2u) return 0;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * 6u;
        size_t remaining = body - offset;
        size_t payload = remaining > 6 ? 6 : remaining;
        uint32_t status = count == 1 ? 0 : i == 0 ? 1 : i + 1 == count ? 3 : 2;
        uint32_t packet[2] = {
            0x30000000u | address | status << 20 | (uint32_t)payload << 16, 0
        };
        for (size_t j = 0; j < payload; ++j) {
            size_t byte = j + 2;
            packet[byte / 4u] |= (uint32_t)message[1 + offset + j] << (24u - 8u * (byte % 4u));
        }
        words[i * 2] = packet[0];
        words[i * 2 + 1] = packet[1];
    }
    return count * 2u;
}
