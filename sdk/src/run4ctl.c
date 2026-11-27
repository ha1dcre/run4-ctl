/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott */
#include "run4ctl.h"
#include <string.h>

static int emit(r4ctl_t *d, const r4p_msg_t *m)
{
    uint8_t frame[R4P_DEV_MAX];
    size_t n = r4p_encode(frame, sizeof frame, m);
    return n && d->callbacks.send_sysex(d->callbacks.user, frame, n) == 1;
}
static void reset_session(r4ctl_t *d)
{
    int was_connected = d->connected;
    d->connected = d->minor = d->hello_sent = d->need_sync = 0;
    d->master_valid = d->list_total = 0;
    d->suppressed = d->rx_n = 0;
    d->rx_open = d->rx_overflow = 0;
    if (was_connected && d->callbacks.connection)
        d->callbacks.connection(d->callbacks.user, 0);
}
int r4ctl_init(r4ctl_t *d, const r4ctl_callbacks_t *cb, uint8_t major, uint8_t minor)
{
    if (!d || !cb || !cb->send_sysex || major > 127 || minor > 127)
        return 0;
    {
        r4ctl_callbacks_t copy = *cb;
        memset(d, 0, sizeof *d);
        d->callbacks = copy;
    }
    d->fw_major = major;
    d->fw_minor = minor;
    d->display_profile = R4CTL_DISPLAY_TILES;
    return 1;
}
int r4ctl_set_display_profile(r4ctl_t *d, enum r4ctl_display_profile profile)
{
    if (!d || d->link ||
        (profile != R4CTL_DISPLAY_TILES && profile != R4CTL_DISPLAY_TAPE && profile != R4CTL_DISPLAY_ROLL))
        return 0;
    d->display_profile = (uint8_t)profile;
    return 1;
}
void r4ctl_set_link(r4ctl_t *d, int up)
{
    uint8_t available = (uint8_t)!!up;
    if (d->link != available) {
        d->link = available;
        reset_session(d);
    }
}
int r4ctl_set_mode(r4ctl_t *d, enum r4ctl_mode mode)
{
    if (mode != R4CTL_CONTROLLER && mode != R4CTL_LOCAL)
        return 0;
    if (d->mode != (uint8_t)mode) {
        d->mode = (uint8_t)mode;
        reset_session(d);
    }
    return 1;
}
int r4ctl_connected(const r4ctl_t *d) { return d->connected; }
uint8_t r4ctl_host_minor(const r4ctl_t *d) { return d->minor; }
static uint8_t advertised_minor(const r4ctl_t *d)
{
    return d->mode == R4CTL_CONTROLLER ? d->display_profile : R4CTL_DISPLAY_TILES;
}
void r4ctl_poll(r4ctl_t *d, uint32_t now)
{
    r4p_msg_t m;
    if (!d->link)
        return;
    if (!d->connected) {
        if (!d->hello_sent || (uint32_t)(now - d->hello_at) >= R4CTL_HELLO_MS) {
            memset(&m, 0, sizeof m);
            m.cmd = R4P_HELLO;
            m.u.hello.proto = R4P_VER;
            m.u.hello.fw_major = d->fw_major;
            m.u.hello.fw_minor = d->fw_minor;
            m.u.hello.mode = d->mode;
            /* Advertise this renderer's profile, rather than the codec maximum. */
            m.u.hello.minor = advertised_minor(d);
            if (emit(d, &m)) { d->hello_sent = 1; d->hello_at = now; }
        }
        return;
    }
    if (d->mode != R4CTL_CONTROLLER)
        return;
    if (d->need_sync) {
        m.cmd = R4P_SYNC_REQ;
        if (!emit(d, &m)) return;
        d->need_sync = 0;
    }
    if (d->have_master) {
        unsigned distance = d->master > d->master_sent ? d->master - d->master_sent : d->master_sent - d->master;
        if (!d->master_valid || ((uint32_t)(now - d->master_at) >= 20u &&
            (distance >= 6u || (distance && (d->master == 0 || d->master == R4P_V14_MAX))))) {
            m.cmd = R4P_POT;
            m.u.pot.value = d->master;
            if (emit(d, &m)) {
                d->master_sent = d->master;
                d->master_valid = 1;
                d->master_at = now;
            }
        }
    }
}
static int ours(const uint8_t *f, size_t n)
{
    return f && n >= 4 && f[0] == 0xF0 && f[1] == 0x7D && f[2] == 0x52 && f[3] == 0x34;
}
int r4ctl_receive_frame(r4ctl_t *d, const uint8_t *f, size_t n)
{
    r4p_msg_t *m = &d->incoming;
    int supported = 0;
    if (!d->link || !ours(f, n)) return 0;
    if (n > R4P_FRAME_MAX || r4p_parse(f, n, m) != 1) return -1;
    if (m->cmd == R4P_HELLO_ACK) {
        int was_connected = d->connected;
        uint8_t limit = advertised_minor(d);
        if (m->u.ack.proto != R4P_VER) return 1;
        d->connected = 1;
        d->minor = m->u.ack.minor < limit ? m->u.ack.minor : limit;
        d->need_sync = (uint8_t)(d->mode == R4CTL_CONTROLLER);
        d->master_valid = d->list_total = 0;
        d->suppressed = 0;
        if (!was_connected && d->callbacks.connection)
            d->callbacks.connection(d->callbacks.user, 1);
        supported = 1;
    } else if (d->connected && d->mode == R4CTL_CONTROLLER) {
        switch (m->cmd) {
        case R4P_PAGE: case R4P_VALUE: case R4P_LEDS: case R4P_STATUS: case R4P_TOAST:
            supported = 1; break;
        case R4P_FOCUS: supported = d->minor >= 2; break;
        case R4P_TAPE_WAVE: case R4P_TAPE_POS: case R4P_METERS:
            supported = d->minor >= R4CTL_DISPLAY_TAPE;
            break;
        case R4P_ROLL: supported = d->minor >= R4CTL_DISPLAY_ROLL; break;
        case R4P_LIST:
            if (d->minor >= 1) {
                d->list_total = m->u.list.total;
                d->list_first = m->u.list.first;
                d->list_n = m->u.list.n;
                d->list_sel = m->u.list.sel;
                supported = 1;
            }
            break;
        default: break; /* a codec feature is not automatically a device capability */
        }
    }
    if (supported && d->callbacks.message)
        d->callbacks.message(d->callbacks.user, m);
    return 1;
}
int r4ctl_receive_byte(r4ctl_t *d, uint8_t b)
{
    int result;
    if (!d->link) return 0;
    if (b >= 0xF8) return 0; /* MIDI realtime can appear inside SysEx */
    if (b == 0xF0) {
        d->rx_n = 1; d->rx[0] = b;
        d->rx_open = 1; d->rx_overflow = 0;
        return 0;
    }
    if (!d->rx_open) return 0;
    if (b == 0xF7) {
        d->rx_open = 0;
        if (d->rx_overflow || d->rx_n >= sizeof d->rx)
            return ours(d->rx, d->rx_n) ? -1 : 0;
        d->rx[d->rx_n++] = b;
        result = r4ctl_receive_frame(d, d->rx, d->rx_n);
        d->rx_n = 0;
        return result;
    }
    if (b >= 0x80) { d->rx_open = 0; return ours(d->rx, d->rx_n) ? -1 : 0; }
    if (d->rx_n < sizeof d->rx) d->rx[d->rx_n++] = b;
    else d->rx_overflow = 1;
    return 0;
}
size_t r4ctl_receive_bytes(r4ctl_t *d, const uint8_t *bytes, size_t n)
{
    size_t frames = 0;
    if (bytes)
        for (size_t i = 0; i < n; ++i)
            if (r4ctl_receive_byte(d, bytes[i]) == 1)
                ++frames;
    return frames;
}
int r4ctl_receive_sysex_payload(r4ctl_t *d, const uint8_t *body, size_t n)
{
    if (!d->link || !body || n < 3 || body[0] != 0x7D || body[1] != 0x52 || body[2] != 0x34)
        return 0;
    if (n > R4P_FRAME_MAX - 2u) return -1;
    for (size_t i = 0; i < n; ++i)
        if (body[i] & 0x80u) return -1;
    r4ctl_receive_byte(d, 0xF0);
    r4ctl_receive_bytes(d, body, n);
    return r4ctl_receive_byte(d, 0xF7);
}
static int controls_ready(const r4ctl_t *d)
{
    return d->link && d->connected && d->mode == R4CTL_CONTROLLER && !d->need_sync &&
           (!d->have_master || d->master_valid);
}
static int pick(r4ctl_t *d, uint8_t index, uint8_t action)
{
    r4p_msg_t m;
    m.cmd = R4P_LIST_PICK; m.u.pick.index = index; m.u.pick.action = action;
    return emit(d, &m);
}
int r4ctl_button(r4ctl_t *d, uint8_t id, int down)
{
    r4p_msg_t m;
    uint16_t bit;
    if (id >= R4P_NBTN) return 0;
    bit = (uint16_t)(1u << id);
    if (!down && (d->suppressed & bit)) { d->suppressed &= (uint16_t)~bit; return 1; }
    if (!controls_ready(d)) return 0;
    if (d->list_total && down) {
        if (id == R4CTL_HOME || id == R4CTL_PLAY) {
            if (!pick(d, d->list_sel, R4P_PICK_OK)) return 0;
            d->list_total = 0;
            if (d->callbacks.list_closed) d->callbacks.list_closed(d->callbacks.user);
        } else if (id == R4CTL_SAVE || id == R4CTL_FX || id == R4CTL_ENV || id == R4CTL_LFO ||
                   id == R4CTL_EDIT || id == R4CTL_GLO || id == R4CTL_ARP || id == R4CTL_SEQ) {
            if (!pick(d, d->list_sel, R4P_PICK_CANCEL)) return 0;
            d->list_total = 0;
            if (d->callbacks.list_closed) d->callbacks.list_closed(d->callbacks.user);
        }
        d->suppressed |= bit;
        return 1;
    }
    m.cmd = R4P_BTN; m.u.btn.id = id; m.u.btn.state = (uint8_t)!!down;
    return emit(d, &m);
}
int r4ctl_encoder(r4ctl_t *d, uint8_t id, int delta)
{
    r4p_msg_t m;
    if (id >= R4P_NENC || delta < -63 || delta > 63 || !controls_ready(d)) return 0;
    if (!delta) return 1;
    if (d->list_total) {
        int sel;
        if (id != R4CTL_SELECT && id != R4CTL_PRESETS) return 1;
        sel = (int)d->list_sel + delta;
        if (sel < 0) sel = 0;
        if (sel >= d->list_total) sel = d->list_total - 1;
        if (sel == d->list_sel) return 1;
        if ((sel < d->list_first || sel >= d->list_first + d->list_n) &&
            !pick(d, (uint8_t)sel, R4P_PICK_MOVE)) return 0;
        d->list_sel = (uint8_t)sel;
        if (d->callbacks.list_selection)
            d->callbacks.list_selection(d->callbacks.user, d->list_sel);
        return 1;
    }
    m.cmd = R4P_ENC; m.u.enc.id = id; m.u.enc.delta = (int8_t)delta;
    return emit(d, &m);
}
int r4ctl_master(r4ctl_t *d, uint16_t value)
{
    if (value > R4P_V14_MAX) return 0;
    d->master = value; d->have_master = 1;
    return 1;
}
size_t r4ctl_midi_message_size(uint8_t status)
{
    if (status >= 0x80 && status < 0xF0)
        return (status & 0xE0u) == 0xC0 ? 2 : 3;
    switch (status) {
    case 0xF2: return 3;
    case 0xF1: case 0xF3: return 2;
    case 0xF6: case 0xF8: case 0xFA: case 0xFB: case 0xFC: case 0xFE: case 0xFF:
        return 1;
    default: return 0;
    }
}
int r4ctl_send_midi_message(r4ctl_t *d, const uint8_t *message, size_t n)
{
    if (!d->link || !d->callbacks.send_midi || !message || !n ||
        r4ctl_midi_message_size(message[0]) != n)
        return 0;
    for (size_t i = 1; i < n; ++i)
        if (message[i] & 0x80u) return 0;
    return d->callbacks.send_midi(d->callbacks.user, message, n) == 1;
}
int r4ctl_note_on(r4ctl_t *d, uint8_t ch, uint8_t note, uint8_t velocity)
{
    uint8_t bytes[3];
    if (ch > 15 || note > 127 || !velocity || velocity > 127) return 0;
    bytes[0] = (uint8_t)(0x90u | ch); bytes[1] = note; bytes[2] = velocity;
    return r4ctl_send_midi_message(d, bytes, sizeof bytes);
}
int r4ctl_note_off(r4ctl_t *d, uint8_t ch, uint8_t note)
{
    uint8_t bytes[3];
    if (ch > 15 || note > 127) return 0;
    bytes[0] = (uint8_t)(0x80u | ch); bytes[1] = note; bytes[2] = 0;
    return r4ctl_send_midi_message(d, bytes, sizeof bytes);
}
int r4ctl_realtime(r4ctl_t *d, uint8_t status)
{
    if (status != 0xF8 && status != 0xFA && status != 0xFB && status != 0xFC && status != 0xFE && status != 0xFF)
        return 0;
    return r4ctl_send_midi_message(d, &status, 1);
}
