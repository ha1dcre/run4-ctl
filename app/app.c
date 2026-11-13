/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: a thin terminal for the RUN4 iOS app. The only logic here is the firmware-local rules (SEL = SHIFT,
 * OCT-/OCT+ shift the keyboard) and the protocol (docs/PROTOCOL.md); everything else is forwarded as BTN / ENC / POT
 * and drawn from what the host sends. Part of the unity build (host/plat_host.c, device/unity.c), after the platform. */
#include <string.h>
#include "r4p.c"
#include "ui.c"

#define FW_MAJOR 0
#define FW_MINOR 1
#define KEY_CHANNEL 0                    /* MIDI channel 1 */
#define KEY_VEL 100                      /* without APP_TOUCH every key lands at this velocity */
#define HELLO_MS 1000u
#define POT_STEP 6                       /* 14-bit counts: the ADC's noise stays quiet */
#define POT_MIN_MS 20u

static struct {
    uint32_t btn, keys;                  /* held at the last poll */
    uint8_t sounding[NKEYS];             /* the note each held key started (255 = none), so a note-off always matches */
    int8_t octave;                       /* -3 .. +3, local */
    r4p_enc_acc_t enc;
    int32_t pot_sent;                    /* last 14-bit value sent, -1 = none yet */
    uint32_t pot_at, hello_at;
    uint32_t session;                    /* plat_usb_session at the last look: a new one is a new computer */
    r4p_leds_t leds;
    uint8_t blink;
    uint32_t blink_at;
    uint32_t notes_on, notes_off, frames_sent, frames_in, frames_bad;   /* host-simulator checks */
} ctl;

/* hook for velocity and pressure: lib/touch.h once APP_TOUCH is on (app.h); until then a fixed velocity */
static uint8_t key_velocity(uint32_t k)
{
    (void)k;
    return KEY_VEL;
}

static void send_frame(const uint8_t *f, size_t n)
{
    if (n && plat_sysex_send(f, (uint32_t)n) == 0)
        ctl.frames_sent++;
}

static void send_hello(void)
{
    uint8_t f[16];
    send_frame(f, r4p_hello(f, sizeof f, FW_MAJOR, FW_MINOR, R4P_MODE_RUN4));
    ctl.hello_at = plat_ms();
}

static void send_pot(void)
{
    uint8_t f[16];
    uint32_t v = (plat_master() * (uint32_t)R4P_V14_MAX + 2048u) / 4096u;
    if (v > R4P_V14_MAX) v = R4P_V14_MAX;
    ctl.pot_sent = (int32_t)v;
    ctl.pot_at = plat_ms();
    send_frame(f, r4p_pot(f, sizeof f, (uint16_t)v));
}

/* ------------------------------------------------------------------ input --- */
static void keys_poll(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ctl.keys & bit)) {
            int32_t note = 53 + (int32_t)k + 12 * ctl.octave;     /* key 0 is F3 = MIDI 53 */
            if (note < 0 || note > 127) {
                ctl.sounding[k] = 255;
                continue;
            }
            ctl.sounding[k] = (uint8_t)note;
            ctl.notes_on++;
            plat_midi_send(0x09u | (0x90u | KEY_CHANNEL) << 8 | (uint32_t)note << 16 | (uint32_t)key_velocity(k) << 24, TO_USB);
        } else if (!(keys & bit) && (ctl.keys & bit)) {
            if (ctl.sounding[k] != 255) {
                ctl.notes_off++;
                plat_midi_send(0x08u | (0x80u | KEY_CHANNEL) << 8 | (uint32_t)ctl.sounding[k] << 16, TO_USB);
            }
            ctl.sounding[k] = 255;
        }
    }
    ctl.keys = keys;
}

static void octave_step(int d)
{
    int o = ctl.octave + d;
    if (o < -3) o = -3;
    if (o > 3) o = 3;
    if (o == ctl.octave)
        return;
    ctl.octave = (int8_t)o;                            /* held keys keep the note they started: no stuck notes */
    ui.octave = ctl.octave;
    ui.dirty |= D_FOOT | (ui.connected ? 0u : D_HEAD);
}

static void input(void)
{
    uint8_t f[16];
    uint32_t btn = plat_buttons(), keys = plat_keys(), k, now = plat_ms();
    uint8_t shift = (uint8_t)((btn >> B_SEL) & 1u);   /* SEL held = SHIFT */
    int32_t pot;

    if (plat_usb_session() != ctl.session) {           /* a new computer: say hello again */
        ctl.session = plat_usb_session();
        ui_connect(0);
        ctl.hello_at = now - HELLO_MS;
        ctl.pot_sent = -1;
    }
    if (!ctl.session)
        ctl.hello_at = now - HELLO_MS;                 /* (nothing to send to yet; try at the first chance) */
    if (!ui.connected && now - ctl.hello_at >= HELLO_MS)
        send_hello();

    for (k = 0; k < NB; k++) {                         /* buttons: forwarded as they are; OCT-/OCT+ also act here */
        uint32_t bit = 1u << k;
        if (!((btn ^ ctl.btn) & bit))
            continue;
        if (btn & bit) {
            if (!shift && k == B_OCTDN) octave_step(-1);
            else if (!shift && k == B_OCTUP) octave_step(1);
        }
        send_frame(f, r4p_btn(f, sizeof f, (uint8_t)k, (btn & bit) ? 1u : 0u));
    }
    ctl.btn = btn;
    if (shift != ui.shift) {
        ui.shift = shift;
        ui.dirty |= D_FOOT;
    }

    keys_poll(keys);

    for (k = 0; k < R4P_NENC; k++) {                   /* encoders: the detents of this frame, one frame each */
        int d = plat_enc((int)k);
        int t;
        if (d)
            r4p_enc_add(&ctl.enc, (uint8_t)k, d);
        t = r4p_enc_take(&ctl.enc, (uint8_t)k);
        if (t)
            send_frame(f, r4p_enc(f, sizeof f, (uint8_t)k, t));
    }

    pot = (int32_t)((plat_master() * (uint32_t)R4P_V14_MAX + 2048u) / 4096u);   /* MASTER: only real movement goes out */
    if (ctl.pot_sent >= 0 && now - ctl.pot_at >= POT_MIN_MS &&
        (pot - ctl.pot_sent >= POT_STEP || ctl.pot_sent - pot >= POT_STEP || ((pot == 0 || pot == R4P_V14_MAX) && pot != ctl.pot_sent)))
        send_pot();
}

/* the lights: what the host sent; blink at 2 Hz; without a host the held keys light themselves */
static void lights(void)
{
    uint32_t lb = 0, db = 0, lk = 0, dk = 0, i, now = plat_ms();
    if (now - ctl.blink_at >= 250u) {
        ctl.blink_at = now;
        ctl.blink ^= 1u;
    }
    for (i = 0; i < NB; i++) {
        uint8_t s = ctl.leds.btn[i];
        if (s == R4P_LED_ON || (s == R4P_LED_BLINK && ctl.blink)) lb |= 1u << i;
        else if (s == R4P_LED_DIM) db |= 1u << i;
    }
    for (i = 0; i < NKEYS; i++) {
        uint8_t s = ctl.leds.key[i];
        if (s == R4P_LED_ON || (s == R4P_LED_BLINK && ctl.blink)) lk |= 1u << i;
        else if (s == R4P_LED_DIM) dk |= 1u << i;
    }
    if (!ui.connected)
        lk |= ctl.keys;
    plat_lights(lb, lk, db, dk, 1);
}

/* ------------------------------------------------------------ host frames --- */
int app_sysex(const uint8_t *p, uint32_t n)
{
    r4p_msg_t m;
    int r = r4p_parse_body(p, n, &m);
    uint8_t f[16];
    if (r == 0)
        return 0;                                      /* not ours */
    if (r < 0) {
        ctl.frames_bad++;
        return 1;
    }
    ctl.frames_in++;
    switch (m.cmd) {
    case R4P_HELLO_ACK:
        ui_connect(1);
        ctl.leds = (r4p_leds_t){{0}, {0}};
        send_frame(f, r4p_sync_req(f, sizeof f));
        ctl.pot_sent = -1;
        send_pot();
        break;
    case R4P_LEDS:
        ctl.leds = m.u.leds;
        break;
    case R4P_PAGE: case R4P_VALUE: case R4P_STATUS: case R4P_TOAST:
        if (ui.connected)                              /* (frames before the ack are the previous session's) */
            ui_apply(&m);
        break;
    default:
        break;                                         /* device->host commands from the host: ignored */
    }
    return 1;
}

/* --------------------------------------------------------------- contract --- */
void app_init(void)
{
    memset(&ctl, 0, sizeof ctl);
    memset(ctl.sounding, 255, sizeof ctl.sounding);
    ctl.pot_sent = -1;
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)                       /* a controller makes no sound */
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void)
{
    ctl.session = plat_usb_session();
    ctl.hello_at = plat_ms() - HELLO_MS;
    memset(&ui, 0, sizeof ui);
    ui_reset_page();
    ui.dirty = D_ALL;
    lcd_fill(0, 0, 240, 240, R4_BG);
}
void app_frame(void)
{
    input();
    lights();
    ui_draw();
}
void app_input_only(void) { input(); }
void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "connected")) *got = ui.connected;
    else if (!strcmp(what, "octave")) *got = ctl.octave;
    else if (!strcmp(what, "shift")) *got = ui.shift;
    else if (!strcmp(what, "page_idx")) *got = ui.page.page_idx;
    else if (!strcmp(what, "toast")) *got = ui.toast_until != 0;
    else if (!strcmp(what, "notes_on")) *got = (int)ctl.notes_on;
    else if (!strcmp(what, "notes_off")) *got = (int)ctl.notes_off;
    else if (!strcmp(what, "frames_in")) *got = (int)ctl.frames_in;
    else if (!strcmp(what, "frames_bad")) *got = (int)ctl.frames_bad;
    else if (!strcmp(what, "frames_sent")) *got = (int)ctl.frames_sent;
    else return 0;
    return 1;
}
#endif
