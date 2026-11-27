/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Eugen Schott
 * Runnable without hardware: a real SDK device talking to an in-process host.
 * Replace the output callbacks with your non-blocking MIDI drivers and the
 * display callback with your own renderer. No firmware or DSP is linked. */
#include "run4ctl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    r4ctl_t device;
    r4p_page_t screen;
    unsigned tape_columns, play_position, roll_steps, roll_notes;
    int blocked, frames, midi_messages;
} example_t;
static example_t example; /* keep bounded state off a small MCU stack */

static void print_bytes(const char *label, const uint8_t *p, size_t n)
{
    size_t i;
    printf("%s", label);
    for (i = 0; i < n; i++) printf(" %02X", p[i]);
    putchar('\n');
}
static int send_sysex(void *user, const uint8_t *frame, size_t n)
{
    example_t *e = user;
    if (e->blocked) return 0; /* queue nothing, retry after the next poll */
    print_bytes("device SysEx:", frame, n);
    e->frames++;
    return 1;
}
static int send_midi(void *user, const uint8_t *bytes, size_t n)
{
    example_t *e = user;
    if (e->blocked) return 0;
    print_bytes("device MIDI: ", bytes, n);
    e->midi_messages++;
    return 1;
}
static void connection(void *user, int connected)
{
    (void)user;
    printf("host %s\n", connected ? "paired" : "unpaired");
}
static void display_roll(const r4p_roll_t *roll)
{
    char steps[R4P_ROLL_STEPS + 1];
    unsigned i;
    if (!roll->rows) {
        puts("Roll closed");
        return;
    }
    memset(steps, '.', roll->steps);
    steps[roll->steps] = '\0';
    for (i = 0; i < roll->n; i++)
        steps[roll->note[i].col] = 'x';
    if (roll->play != R4P_ROLL_NONE)
        steps[roll->play] = '>';
    printf("Roll %s, step %u onward: %s\n", roll->label, roll->first + 1u, steps);
}
static void display(void *user, const r4p_msg_t *m)
{
    example_t *e = user;
    switch (m->cmd) {
    case R4P_HELLO_ACK:
        memset(&e->screen, 0, sizeof e->screen);
        e->tape_columns = e->play_position = e->roll_steps = e->roll_notes = 0;
        printf("controller profile 1.%u\n", r4ctl_host_minor(&e->device));
        break;
    case R4P_PAGE:
        e->screen = m->u.page;
        printf("screen: %s / %s / %s\n", e->screen.track, e->screen.title, e->screen.slot[0].text);
        break;
    case R4P_VALUE:
        e->screen.slot[m->u.value.slot].value = m->u.value.value;
        strcpy(e->screen.slot[m->u.value.slot].text, m->u.value.text);
        printf("tile %u: %s\n", m->u.value.slot + 1u, m->u.value.text);
        break;
    case R4P_LEDS: printf("PLAY LED state %u\n", m->u.leds.btn[R4CTL_PLAY]); break;
    case R4P_STATUS: printf("tempo %u.%u BPM\n", m->u.status.bpm10 / 10u, m->u.status.bpm10 % 10u); break;
    case R4P_TOAST: printf("toast: %s\n", m->u.toast.text); break;
    case R4P_LIST: printf("list: %s (%u items), selected %u\n", m->u.list.title, m->u.list.total, m->u.list.sel); break;
    case R4P_FOCUS: printf("focused tile: %u\n", m->u.focus.cursor); break;
    case R4P_TAPE_WAVE:
        e->tape_columns = m->u.wave.n;
        printf("Tape %s: %u waveform columns\n", m->u.wave.label, e->tape_columns);
        break;
    case R4P_TAPE_POS:
        e->play_position = m->u.tpos.play;
        printf("Tape position %u, loop %u–%u\n", e->play_position, m->u.tpos.in, m->u.tpos.out);
        break;
    case R4P_ROLL:
        e->roll_steps = m->u.roll.steps;
        e->roll_notes = m->u.roll.n;
        display_roll(&m->u.roll);
        break;
    default: break;
    }
}
static void list_selection(void *user, uint8_t index)
{
    (void)user;
    printf("list highlight: %u\n", index);
}
static void list_closed(void *user) { (void)user; puts("list closed"); }
static void host_frame(example_t *e, const r4p_msg_t *m)
{
    uint8_t frame[R4P_FRAME_MAX];
    size_t n = r4p_encode(frame, sizeof frame, m), i;
    assert(n);
    for (i = 0; i < n; i++) {
        if (i == 4) r4ctl_receive_byte(&e->device, 0xF8); /* interleaved clock */
        r4ctl_receive_byte(&e->device, frame[i]);
    }
}
/* This host simulation shows the data a device can map to its own screen,
 * step LEDs, or other controls. RUN4 owns the track, pattern and note edits. */
static void send_display_feedback(example_t *e)
{
    r4p_msg_t host = {0};
    host.cmd = R4P_TAPE_WAVE;
    strcpy(host.u.wave.label, "TAPE");
    host.u.wave.n = 3;
    host.u.wave.peak[0] = 16;
    host.u.wave.peak[1] = 96;
    host.u.wave.peak[2] = 42;
    host_frame(e, &host);
    assert(e->tape_columns == 3);
    memset(&host, 0, sizeof host);
    host.cmd = R4P_TAPE_POS;
    host.u.tpos.play = 4096;
    host.u.tpos.out = 8192;
    host_frame(e, &host);
    assert(e->play_position == 4096);

    memset(&host, 0, sizeof host);
    host.cmd = R4P_ROLL;
    strcpy(host.u.roll.label, "TOLF 1");
    host.u.roll.lo = 60;
    host.u.roll.rows = 12;
    host.u.roll.steps = 16;
    host.u.roll.total = 32;
    host.u.roll.play = 4;
    host.u.roll.n = 1;
    host.u.roll.note[0] = (r4p_rnote_t){2, 1, 0, 100, 0};
    host_frame(e, &host);
    assert(e->roll_steps == 16 && e->roll_notes == 1);
}
int main(int argc, char **argv)
{
    example_t *e = &example;
    r4p_msg_t host = {0};
    r4ctl_callbacks_t callbacks = {0};
    int feedback = argc == 2 && !strcmp(argv[1], "--display-feedback");
    if (argc != 1 && !feedback) {
        fprintf(stderr, "usage: %s [--display-feedback]\n", argv[0]);
        return 2;
    }
    callbacks.user = e;
    callbacks.send_sysex = send_sysex;
    callbacks.send_midi = send_midi;
    callbacks.message = display;
    callbacks.connection = connection;
    callbacks.list_selection = list_selection;
    callbacks.list_closed = list_closed;
    assert(r4ctl_init(&e->device, &callbacks, 1, 0)); /* YOUR firmware version */
    if (feedback)
        assert(r4ctl_set_display_profile(&e->device, R4CTL_DISPLAY_ROLL));
    r4ctl_master(&e->device, 8192);
    r4ctl_set_link(&e->device, 1);
    r4ctl_poll(&e->device, 0); /* HELLO advertises the selected display profile */
    /* No RUN4 required for ordinary MIDI, including RUN4 1.8.5 or a DAW. */
    assert(r4ctl_note_on(&e->device, 0, 60, 100));
    assert(r4ctl_note_off(&e->device, 0, 60));
    host.cmd = R4P_HELLO_ACK; host.u.ack.proto = 1; host.u.ack.minor = 8;
    host_frame(e, &host);
    r4ctl_poll(&e->device, 10); /* SYNC_REQ, then current MASTER */
    memset(&host, 0, sizeof host);
    host.cmd = R4P_PAGE;
    strcpy(host.u.page.title, "TAPE"); strcpy(host.u.page.track, "T1");
    host.u.page.r = 127; host.u.page.cursor = R4P_CURSOR_NONE;
    strcpy(host.u.page.slot[0].name, "GAIN"); host.u.page.slot[0].kind = R4P_KIND_UNI;
    strcpy(host.u.page.slot[0].text, "0.0 dB");
    host_frame(e, &host);
    assert(!strcmp(e->screen.title, "TAPE"));
    if (feedback)
        send_display_feedback(e);
    e->blocked = 1;
    assert(!r4ctl_encoder(&e->device, R4CTL_KNOB1, 1));
    e->blocked = 0;
    assert(r4ctl_encoder(&e->device, R4CTL_KNOB1, 1)); /* retain/retry the SAME detent */
    memset(&host, 0, sizeof host);
    host.cmd = R4P_VALUE; host.u.value.slot = 0; host.u.value.value = 9000;
    strcpy(host.u.value.text, "+1.0 dB");
    host_frame(e, &host);
    assert(!strcmp(e->screen.slot[0].text, "+1.0 dB"));
    assert(r4ctl_button(&e->device, R4CTL_PLAY, 1));
    assert(r4ctl_button(&e->device, R4CTL_PLAY, 0));
    /* Leave the controller page while keeping your own synth/firmware running. */
    assert(r4ctl_set_mode(&e->device, R4CTL_LOCAL));
    r4ctl_poll(&e->device, 20); /* HELLO mode 1 tells RUN4 to stop drawing */
    host.cmd = R4P_HELLO_ACK; host.u.ack.proto = 1; host.u.ack.minor = 8;
    host_frame(e, &host);
    assert(!r4ctl_button(&e->device, R4CTL_PLAY, 1)); /* remains local */
    assert(r4ctl_note_on(&e->device, 0, 64, 90));
    assert(r4ctl_note_off(&e->device, 0, 64));
    r4ctl_set_link(&e->device, 0);
    assert(!r4ctl_connected(&e->device));
    printf("minimal example: OK (%d SysEx frames, %d MIDI messages)\n", e->frames, e->midi_messages);
    return 0;
}
