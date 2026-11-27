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
static void display(void *user, const r4p_msg_t *m)
{
    example_t *e = user;
    switch (m->cmd) {
    case R4P_HELLO_ACK:
        memset(&e->screen, 0, sizeof e->screen);
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
int main(void)
{
    example_t *e = &example;
    r4p_msg_t host = {0};
    r4ctl_callbacks_t callbacks = {0};
    callbacks.user = e;
    callbacks.send_sysex = send_sysex;
    callbacks.send_midi = send_midi;
    callbacks.message = display;
    callbacks.connection = connection;
    callbacks.list_selection = list_selection;
    callbacks.list_closed = list_closed;
    assert(r4ctl_init(&e->device, &callbacks, 1, 0)); /* YOUR firmware version */
    r4ctl_master(&e->device, 8192);
    r4ctl_set_link(&e->device, 1);
    r4ctl_poll(&e->device, 0); /* HELLO advertises only profile 1.2 */
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
