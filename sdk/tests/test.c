/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott */
#include "run4ctl.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    r4ctl_t device;
    int blocked, messages, connects, disconnects, selections, closes;
    unsigned n, midi_n;
    uint8_t tx[256][R4P_DEV_MAX], midi[256][3];
    size_t len[256], midi_len[256];
    r4p_msg_t last;
} fixture_t;
static fixture_t f, other;
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int sysex(void *user, const uint8_t *bytes, size_t n)
{
    fixture_t *p = user;
    if (p->blocked) return p->blocked < 0 ? -1 : 0;
    assert(p->n < 256 && n <= R4P_DEV_MAX);
    memcpy(p->tx[p->n], bytes, n); p->len[p->n++] = n;
    return 1;
}
static int midi(void *user, const uint8_t *bytes, size_t n)
{
    fixture_t *p = user;
    if (p->blocked) return p->blocked < 0 ? -1 : 0;
    assert(p->midi_n < 256 && n <= 3);
    memcpy(p->midi[p->midi_n], bytes, n); p->midi_len[p->midi_n++] = n;
    return 1;
}
static void message(void *user, const r4p_msg_t *m)
{
    fixture_t *p = user; p->messages++; p->last = *m;
}
static void connection(void *user, int up)
{
    fixture_t *p = user;
    if (up) p->connects++; else p->disconnects++;
}
static void selection(void *user, uint8_t index)
{
    fixture_t *p = user; (void)index; p->selections++;
}
static void closed(void *user) { ((fixture_t *)user)->closes++; }
static void init(fixture_t *p)
{
    r4ctl_callbacks_t cb = {0};
    memset(p, 0, sizeof *p);
    cb.user = p; cb.send_sysex = sysex; cb.send_midi = midi;
    cb.message = message; cb.connection = connection;
    cb.list_selection = selection; cb.list_closed = closed;
    assert(r4ctl_init(&p->device, &cb, 3, 4));
}
static int inject(fixture_t *p, const r4p_msg_t *m)
{
    uint8_t frame[R4P_FRAME_MAX];
    size_t n = r4p_encode(frame, sizeof frame, m);
    assert(n);
    return r4ctl_receive_frame(&p->device, frame, n);
}
static void ack(fixture_t *p, uint8_t minor)
{
    r4p_msg_t m = {0}; m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = minor;
    assert(inject(p, &m) == 1);
}
static r4p_msg_t sent(fixture_t *p, unsigned index)
{
    r4p_msg_t m;
    assert(index < p->n && r4p_parse(p->tx[index], p->len[index], &m) == 1);
    return m;
}
static void ready(fixture_t *p, uint8_t minor)
{
    init(p); r4ctl_set_link(&p->device, 1); ack(p, minor); r4ctl_poll(&p->device, 0);
}
static r4p_msg_t list(void)
{
    r4p_msg_t m = {0};
    m.cmd = R4P_LIST; m.u.list.total = 10; m.u.list.first = 0;
    m.u.list.sel = 0; m.u.list.n = 3;
    strcpy(m.u.list.title, "MODES");
    strcpy(m.u.list.item[0], "TAPE"); strcpy(m.u.list.item[1], "POLY"); strcpy(m.u.list.item[2], "DISC");
    return m;
}
static int test_display_profiles(void)
{
    r4p_msg_t wave = {0}, position = {0}, meters = {0}, roll = {0};
    int before;
    wave.cmd = R4P_TAPE_WAVE;
    wave.u.wave.n = 1;
    wave.u.wave.peak[0] = 82;
    position.cmd = R4P_TAPE_POS;
    position.u.tpos.play = 2048;
    position.u.tpos.out = 8192;
    meters.cmd = R4P_METERS;
    meters.u.meters.level[0] = 51;
    roll.cmd = R4P_ROLL;
    roll.u.roll.lo = 60;
    roll.u.roll.rows = 12;
    roll.u.roll.steps = 16;
    roll.u.roll.total = 32;
    roll.u.roll.play = 3;
    roll.u.roll.n = 1;
    roll.u.roll.note[0] = (r4p_rnote_t){3, 2, 0, 100, 0};

    ready(&f, 8);
    before = f.messages;
    CHECK(inject(&f, &wave) == 1 && inject(&f, &roll) == 1 && f.messages == before);
    CHECK(!r4ctl_set_display_profile(&f.device, R4CTL_DISPLAY_TAPE));
    r4ctl_set_link(&f.device, 0);
    CHECK(!r4ctl_set_display_profile(NULL, R4CTL_DISPLAY_TAPE));
    CHECK(!r4ctl_set_display_profile(&f.device, (enum r4ctl_display_profile)8));
    CHECK(r4ctl_set_display_profile(&f.device, R4CTL_DISPLAY_TAPE));
    r4ctl_set_link(&f.device, 1);
    r4ctl_poll(&f.device, 0);
    CHECK(sent(&f, f.n - 1).u.hello.minor == 3);
    CHECK(inject(&f, &wave) == 1 && f.messages == before); /* wait for ACK */
    ack(&f, 8);
    CHECK(r4ctl_host_minor(&f.device) == 3);
    CHECK(inject(&f, &wave) == 1 && f.last.cmd == R4P_TAPE_WAVE && f.last.u.wave.peak[0] == 82);
    CHECK(inject(&f, &position) == 1 && f.last.cmd == R4P_TAPE_POS && f.last.u.tpos.play == 2048);
    CHECK(inject(&f, &meters) == 1 && f.last.cmd == R4P_METERS && f.last.u.meters.level[0] == 51);
    before = f.messages;
    CHECK(inject(&f, &roll) == 1 && f.messages == before);
    wave.u.wave.n = 0;
    CHECK(inject(&f, &wave) == 1 && f.last.cmd == R4P_TAPE_WAVE && f.last.u.wave.n == 0);

    r4ctl_set_link(&f.device, 0);
    CHECK(r4ctl_set_display_profile(&f.device, R4CTL_DISPLAY_ROLL));
    r4ctl_set_link(&f.device, 1);
    r4ctl_poll(&f.device, 1);
    CHECK(sent(&f, f.n - 1).u.hello.minor == 7);
    ack(&f, 2);
    before = f.messages;
    CHECK(inject(&f, &roll) == 1 && inject(&f, &position) == 1 && f.messages == before);
    ack(&f, 3);
    CHECK(r4ctl_host_minor(&f.device) == 3);
    CHECK(inject(&f, &position) == 1 && f.last.cmd == R4P_TAPE_POS);
    before = f.messages;
    CHECK(inject(&f, &roll) == 1 && f.messages == before);
    ack(&f, 8);
    CHECK(r4ctl_host_minor(&f.device) == 7);
    CHECK(inject(&f, &roll) == 1 && f.last.cmd == R4P_ROLL);
    CHECK(f.last.u.roll.play == 3 && f.last.u.roll.note[0].len == 2);
    memset(&roll.u.roll, 0, sizeof roll.u.roll);
    roll.u.roll.cur_col = roll.u.roll.cur_row = roll.u.roll.play = R4P_ROLL_NONE;
    CHECK(inject(&f, &roll) == 1 && f.last.cmd == R4P_ROLL && f.last.u.roll.rows == 0);

    CHECK(r4ctl_set_mode(&f.device, R4CTL_LOCAL));
    r4ctl_poll(&f.device, 2);
    CHECK(sent(&f, f.n - 1).u.hello.minor == 2);
    ack(&f, 8);
    before = f.messages;
    CHECK(inject(&f, &roll) == 1 && f.messages == before);
    CHECK(r4ctl_set_mode(&f.device, R4CTL_CONTROLLER));
    r4ctl_poll(&f.device, 3);
    CHECK(sent(&f, f.n - 1).u.hello.minor == 7);
    r4ctl_set_link(&f.device, 0);
    CHECK(r4ctl_set_display_profile(&f.device, R4CTL_DISPLAY_TILES));
    return 0;
}
int main(void)
{
    r4p_msg_t m = {0}, decoded;
    uint8_t frame[R4P_FRAME_MAX];
    size_t n, i;
    unsigned before;
    uint32_t random = 1;
    r4ctl_callbacks_t cb = {0};
    CHECK(!r4ctl_init(NULL, &cb, 0, 0));
    CHECK(!r4ctl_init(&f.device, NULL, 0, 0));
    CHECK(!r4ctl_init(&f.device, &cb, 0, 0));
    cb.send_sysex = sysex;
    CHECK(!r4ctl_init(&f.device, &cb, 128, 0));
    init(&f);
    r4ctl_poll(&f.device, 100);
    CHECK(f.n == 0 && !r4ctl_connected(&f.device));
    CHECK(!r4ctl_button(&f.device, R4CTL_PLAY, 1));
    CHECK(!r4ctl_note_on(&f.device, 0, 60, 100));
    r4ctl_set_link(&f.device, 1);
    CHECK(r4ctl_master(&f.device, 8000));
    f.blocked = 1; r4ctl_poll(&f.device, 100);
    CHECK(f.n == 0);
    f.blocked = -1; r4ctl_poll(&f.device, 101);
    CHECK(f.n == 0); /* negative transport error is NOT success */
    f.blocked = 0; r4ctl_poll(&f.device, 102);
    CHECK(f.n == 1 && f.len[0] == 11);
    CHECK(!memcmp(f.tx[0], (uint8_t[]){0xF0,0x7D,0x52,0x34,1,1,3,4,0,2,0xF7}, 11));
    r4ctl_poll(&f.device, 1101); CHECK(f.n == 1);
    r4ctl_poll(&f.device, 1102); CHECK(f.n == 2);
    CHECK(r4ctl_note_on(&f.device, 2, 60, 100));
    CHECK(r4ctl_note_off(&f.device, 2, 60));
    CHECK(!memcmp(f.midi[0], (uint8_t[]){0x92,60,100}, 3));
    CHECK(!memcmp(f.midi[1], (uint8_t[]){0x82,60,0}, 3));
    CHECK(!r4ctl_note_on(&f.device, 16, 60, 100));
    CHECK(!r4ctl_note_on(&f.device, 0, 128, 100));
    CHECK(!r4ctl_note_on(&f.device, 0, 60, 0));
    CHECK(!r4ctl_note_on(&f.device, 0, 60, 128));
    CHECK(!r4ctl_note_off(&f.device, 0, 128));
    CHECK(r4ctl_realtime(&f.device, 0xFA));
    CHECK(f.midi_len[2] == 1 && f.midi[2][0] == 0xFA);
    CHECK(!r4ctl_realtime(&f.device, 0xF0));
    m.cmd = R4P_PAGE; m.u.page.cursor = R4P_CURSOR_NONE;
    CHECK(inject(&f, &m) == 1 && f.messages == 0); /* no display before ACK */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 2; m.u.ack.minor = 8;
    CHECK(inject(&f, &m) == 1 && !r4ctl_connected(&f.device));
    ack(&f, 8);
    CHECK(r4ctl_connected(&f.device) && r4ctl_host_minor(&f.device) == 2 && f.connects == 1);
    CHECK(!r4ctl_button(&f.device, R4CTL_PLAY, 1)); /* sync first */
    f.blocked = 1; r4ctl_poll(&f.device, 1200); CHECK(f.n == 2);
    f.blocked = 0; r4ctl_poll(&f.device, 1201);
    CHECK(f.n == 4 && sent(&f, 2).cmd == R4P_SYNC_REQ && sent(&f, 3).u.pot.value == 8000);
    CHECK(r4ctl_master(&f.device, 8005)); r4ctl_poll(&f.device, 1221); CHECK(f.n == 4);
    CHECK(r4ctl_master(&f.device, 8006)); r4ctl_poll(&f.device, 1222); CHECK(f.n == 5);
    CHECK(r4ctl_master(&f.device, 8007)); r4ctl_poll(&f.device, 1250); CHECK(f.n == 5);
    CHECK(r4ctl_master(&f.device, 0)); r4ctl_poll(&f.device, 1251); CHECK(f.n == 6);
    CHECK(!r4ctl_master(&f.device, 16384));
    f.blocked = 1;
    CHECK(!r4ctl_button(&f.device, R4CTL_PLAY, 1));
    CHECK(!r4ctl_encoder(&f.device, R4CTL_KNOB1, 1));
    CHECK(!r4ctl_note_off(&f.device, 2, 60));
    f.blocked = 0;
    CHECK(r4ctl_button(&f.device, R4CTL_PLAY, 1));
    CHECK(r4ctl_button(&f.device, R4CTL_PLAY, 0));
    CHECK(r4ctl_encoder(&f.device, R4CTL_KNOB1, 1));
    CHECK(sent(&f, 6).u.btn.state == 1 && sent(&f, 7).u.btn.state == 0);
    CHECK(sent(&f, 8).u.enc.id == 3 && sent(&f, 8).u.enc.delta == 1);
    CHECK(!r4ctl_encoder(&f.device, 7, 1));
    CHECK(!r4ctl_encoder(&f.device, R4CTL_KNOB1, INT_MAX));
    CHECK(!r4ctl_button(&f.device, 14, 1));
    before = f.n;
    ack(&f, 2); r4ctl_poll(&f.device, 1300);
    CHECK(f.connects == 1 && f.n == before + 2); /* repeated ACK resyncs */
    memset(&m, 0, sizeof m); m.cmd = R4P_PAGE; m.u.page.cursor = R4P_CURSOR_NONE;
    strcpy(m.u.page.title, "TAPE"); strcpy(m.u.page.slot[0].text, "0.0 dB");
    CHECK(inject(&f, &m) == 1 && !strcmp(f.last.u.page.title, "TAPE"));
    n = r4p_encode(frame, sizeof frame, &m); CHECK(n > 10);
    before = (unsigned)f.messages;
    for (i = 0; i < n; i++) {
        r4ctl_receive_byte(&f.device, 0xF8);
        r4ctl_receive_byte(&f.device, frame[i]);
    }
    CHECK((unsigned)f.messages == before + 1 && !strcmp(f.last.u.page.title, "TAPE"));
    frame[1] = 0x22;
    CHECK(r4ctl_receive_frame(&f.device, frame, n) == 0 && (unsigned)f.messages == before + 1);
    CHECK(r4ctl_receive_frame(&f.device, NULL, 0) == 0);
    frame[1] = 0x7D; frame[n-1] = 0;
    CHECK(r4ctl_receive_frame(&f.device, frame, n) == -1);
    for (i = 0; i < 2000; i++) r4ctl_receive_byte(&f.device, i == 0 ? 0xF0 : i < 4 ? (uint8_t[]){0,0x7D,0x52,0x34}[i] : 1);
    CHECK(r4ctl_receive_byte(&f.device, 0xF7) == -1);
    ack(&f, 2); CHECK(r4ctl_connected(&f.device)); /* recovery */
    r4ctl_receive_byte(&f.device, 0xF0); r4ctl_receive_byte(&f.device, 0x7D);
    r4ctl_receive_byte(&f.device, 0x52); r4ctl_receive_byte(&f.device, 0x34);
    CHECK(r4ctl_receive_byte(&f.device, 0x90) == -1);
    for (i = 0; i < 100000; i++) {
        random = random * 1664525u + 1013904223u;
        r4ctl_receive_byte(&f.device, (uint8_t)(random >> 24));
    }
    /* Arbitrary stream garbage cannot prevent a new complete HELLO_ACK. */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 2;
    n = r4p_encode(frame, sizeof frame, &m);
    for (i = 0; i < n; i++) r4ctl_receive_byte(&f.device, frame[i]);
    CHECK(r4ctl_connected(&f.device) && f.last.cmd == R4P_HELLO_ACK);

    ready(&f, 2);
    CHECK(r4ctl_button(&f.device, R4CTL_SEL, 1));
    memset(&m, 0, sizeof m); m.cmd = R4P_VALUE; m.u.value.slot = 2;
    strcpy(m.u.value.text, "Q 1.0");
    CHECK(inject(&f, &m) == 1 && f.last.cmd == R4P_VALUE && f.last.u.value.slot == 2);
    m.cmd = R4P_LEDS; memset(&m.u.leds, 0, sizeof m.u.leds);
    m.u.leds.btn[R4CTL_PLAY] = R4P_LED_BLINK;
    CHECK(inject(&f, &m) == 1 && f.last.u.leds.btn[R4CTL_PLAY] == R4P_LED_BLINK);
    memset(&m, 0, sizeof m); m.cmd = R4P_STATUS; m.u.status.bpm10 = 1200;
    CHECK(inject(&f, &m) == 1 && f.last.cmd == R4P_STATUS && f.last.u.status.bpm10 == 1200);
    memset(&m, 0, sizeof m); m.cmd = R4P_TOAST; strcpy(m.u.toast.text, "READY");
    CHECK(inject(&f, &m) == 1 && !strcmp(f.last.u.toast.text, "READY"));
    m = list(); CHECK(inject(&f, &m) == 1);
    before = f.n;
    CHECK(r4ctl_button(&f.device, R4CTL_SEL, 0) && f.n == before + 1); /* pre-LIST press gets its release */
    before = f.n;
    CHECK(r4ctl_encoder(&f.device, R4CTL_SELECT, 1));
    CHECK(f.n == before && f.selections == 1);
    f.blocked = 1;
    CHECK(!r4ctl_encoder(&f.device, R4CTL_PRESETS, 3) && f.selections == 1);
    f.blocked = 0;
    CHECK(r4ctl_encoder(&f.device, R4CTL_PRESETS, 3));
    decoded = sent(&f, before);
    CHECK(decoded.cmd == R4P_LIST_PICK && decoded.u.pick.index == 4 && decoded.u.pick.action == R4P_PICK_MOVE);
    before = f.n;
    f.blocked = 1; CHECK(!r4ctl_button(&f.device, R4CTL_HOME, 1) && f.closes == 0);
    f.blocked = 0;
    CHECK(r4ctl_button(&f.device, R4CTL_HOME, 1) && f.closes == 1);
    CHECK(r4ctl_button(&f.device, R4CTL_HOME, 0) && f.n == before + 1);
    CHECK(sent(&f, before).u.pick.action == R4P_PICK_OK);
    m = list(); inject(&f, &m); before = f.n;
    CHECK(r4ctl_button(&f.device, R4CTL_FX, 1));
    CHECK(r4ctl_button(&f.device, R4CTL_FX, 0) && f.n == before + 1 && f.closes == 2);
    CHECK(sent(&f, before).u.pick.action == R4P_PICK_CANCEL);
    m = list(); inject(&f, &m); before = f.n;
    CHECK(r4ctl_encoder(&f.device, R4CTL_KNOB1, 1) && f.n == before);
    CHECK(r4ctl_encoder(&f.device, R4CTL_SELECT, -63)); /* clamped at first item */

    ready(&f, 0); CHECK(r4ctl_host_minor(&f.device) == 0);
    before = (unsigned)f.messages;
    m = list(); inject(&f, &m);
    CHECK((unsigned)f.messages == before); /* no LIST for a 1.0 host */
    memset(&m, 0, sizeof m); m.cmd = R4P_FOCUS; m.u.focus.cursor = R4P_CURSOR_NONE;
    inject(&f, &m); CHECK((unsigned)f.messages == before);
    ready(&f, 2);
    inject(&f, &m); CHECK(f.last.cmd == R4P_FOCUS);
    m.cmd = R4P_AUDIO_ROUTE; m.u.b1.v = 1;
    before = (unsigned)f.messages;
    inject(&f, &m); CHECK((unsigned)f.messages == before); /* does NOT claim audio support */
    CHECK(!r4ctl_set_mode(&f.device, (enum r4ctl_mode)99));
    CHECK(r4ctl_set_mode(&f.device, R4CTL_LOCAL));
    CHECK(!r4ctl_connected(&f.device) && f.disconnects == 1);
    r4ctl_poll(&f.device, 10); CHECK(sent(&f, f.n-1).u.hello.mode == R4P_MODE_SYNTH);
    ack(&f, 8); before = f.n;
    r4ctl_poll(&f.device, 100);
    CHECK(f.n == before && !r4ctl_button(&f.device, R4CTL_PLAY, 1));
    CHECK(r4ctl_note_on(&f.device, 0, 60, 90));
    memset(&m, 0, sizeof m); m.cmd = R4P_PAGE; m.u.page.cursor = R4P_CURSOR_NONE;
    before = (unsigned)f.messages;
    inject(&f, &m); CHECK((unsigned)f.messages == before); /* native screen stays native */
    CHECK(r4ctl_set_mode(&f.device, R4CTL_CONTROLLER));
    r4ctl_poll(&f.device, 110); CHECK(sent(&f, f.n-1).u.hello.mode == 0);
    ack(&f, 2); r4ctl_poll(&f.device, 111);
    CHECK(sent(&f, f.n-1).cmd == R4P_SYNC_REQ);
    r4ctl_set_link(&f.device, 0); before = f.n;
    r4ctl_poll(&f.device, 9000);
    CHECK(f.n == before && !r4ctl_connected(&f.device) && !r4ctl_note_off(&f.device, 0, 60));
    r4ctl_set_link(&f.device, 1); r4ctl_poll(&f.device, UINT32_MAX-500u);
    before = f.n; r4ctl_poll(&f.device, 498); CHECK(f.n == before);
    r4ctl_poll(&f.device, 499); CHECK(f.n == before+1); /* millis wrap */
    init(&other); r4ctl_set_link(&other.device, 1); ack(&other, 1);
    CHECK(r4ctl_host_minor(&other.device) == 1 && !r4ctl_connected(&f.device));
    /* Optional MIDI callback: UI-only ports are supported, not falsely successful. */
    other.device.callbacks.send_midi = NULL;
    CHECK(!r4ctl_note_on(&other.device, 0, 60, 100));
    if (test_display_profiles()) return 1;
    printf("SDK: %u checks passed; 100000 hostile MIDI stream bytes; state %lu bytes\n", checks, (unsigned long)sizeof(r4ctl_t));
    return 0;
}
