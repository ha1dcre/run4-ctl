/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: a thin terminal for the RUN4 iOS app. The only logic here is the firmware-local rules (SEL = SHIFT,
 * OCT-/OCT+ shift the keyboard) and the protocol (docs/PROTOCOL.md); everything else is forwarded as BTN / ENC / POT
 * and drawn from what the host sends. Part of the unity build (host/plat_host.c, device/unity.c), after the platform. */
#include <string.h>
#include "r4p.c"
#include "ui.c"

#define FW_MAJOR 0
#define FW_MINOR 2
#define KEY_VEL 100                      /* fixed velocity, and the fallback when a key has no reading */
#define HELLO_MS 1000u
#define POT_STEP 6                       /* 14-bit counts: the ADC's noise stays quiet */
#define POT_MIN_MS 20u
#define TXQ_N 32                         /* pending BTN events; beyond that a change waits in the button state itself */

/* the SETUP page's settings (docs/PROTOCOL.md "Firmware-local"): kept in the store, so they survive a power cycle */
enum { VEL_SOFT, VEL_NORMAL, VEL_HARD, VEL_FIXED, VEL_N };
enum { AT_OFF, AT_POLY, AT_CHANNEL, AT_N };
typedef struct { uint8_t ver, vel, at, led, chan; } setup_t;       /* chan 0..15 = MIDI channel 1..16; led 0 off, 1 low, 2 high */
#define SETUP_VER 1
static const int8_t VEL_CURVE[VEL_N] = {-3, 0, 3, 0};              /* touch_curve(): needs tuning on the device */
static setup_t setup;

static struct {
    uint32_t btn, keys;                  /* held at the last poll */
    uint8_t sounding[NKEYS];             /* ... */
    uint8_t snd_ch[NKEYS];               /* ... and the channel it started on */
    uint8_t prs[NKEYS];                  /* last pressure sent per key (poly aftertouch) */
    uint8_t chan_prs;                    /* last channel pressure sent */
    uint32_t suppress;                   /* buttons whose events stay local until released (the SETUP page's) */
    int8_t setup_acc[4];
    uint8_t setup_dirty;             /* the note each held key started (255 = none), so a note-off always matches */
    int8_t octave;                       /* -3 .. +3, local */
    r4p_enc_acc_t enc;
    uint8_t txq[TXQ_N];                  /* BTN events not yet sent: (id << 1) | down, oldest first (a stalled host) */
    uint8_t txq_n;
    uint8_t want_sync, want_pot;         /* owed after a HELLO_ACK */
    int32_t pot_sent;                    /* last 14-bit value sent, -1 = none yet */
    uint32_t pot_at, hello_at;
    uint32_t session;                    /* plat_usb_session at the last look: a new one is a new computer */
    r4p_leds_t leds;
    uint8_t blink;
    uint32_t blink_at;
    uint32_t tx_deferred;                /* frames that found no room and waited (host-simulator check) */
    uint32_t notes_on, notes_off, frames_sent, frames_in, frames_bad;   /* host-simulator checks */
} ctl;

static uint8_t key_velocity(uint32_t k)
{
    uint32_t v;
    if (setup.vel == VEL_FIXED)
        return KEY_VEL;
    v = touch_vel(k);
    return v ? (uint8_t)v : KEY_VEL;
}

/* ------------------------------------------------------------------ SETUP --- */
static void setup_defaults(void)
{
    setup = (setup_t){SETUP_VER, VEL_NORMAL, AT_OFF, 2, 0};
}
static void setup_apply(void)
{
    touch_curve(VEL_CURVE[setup.vel]);
}
static void setup_load(void)
{
    setup_t t;
    setup_defaults();
    if (plat_store_load(OBJ_SETUP, &t, sizeof t) == (int)sizeof t && t.ver == SETUP_VER && t.vel < VEL_N && t.at < AT_N &&
        t.led <= 2 && t.chan < 16)
        setup = t;
    setup_apply();
}
static void setup_fill(void)                          /* the SETUP page: four tiles, the same look as the host's pages */
{
    static const char *const VN[VEL_N] = {"SOFT", "NORMAL", "HARD", "FIXED 100"};
    static const char *const AN[AT_N] = {"OFF", "POLY", "CHANNEL"};
    static const char *const LN[3] = {"OFF", "LOW", "HIGH"};
    r4p_page_t *p = &ui.setup_page;
    char t[8];
    memset(p, 0, sizeof *p);
    strcpy(p->title, "SETUP");
    strcpy(p->track, "LOCAL");
    p->r = 127; p->g = 70; p->b = 20;
    strcpy(p->slot[0].name, "VELOCITY");   p->slot[0].kind = R4P_KIND_ENUM; p->slot[0].value = (uint16_t)(VEL_N << 7 | setup.vel);
    strcpy(p->slot[0].text, VN[setup.vel]);
    strcpy(p->slot[1].name, "AFTERTOUCH"); p->slot[1].kind = R4P_KIND_ENUM; p->slot[1].value = (uint16_t)(AT_N << 7 | setup.at);
    strcpy(p->slot[1].text, AN[setup.at]);
    strcpy(p->slot[2].name, "LED LEVEL");  p->slot[2].kind = R4P_KIND_ENUM; p->slot[2].value = (uint16_t)(3 << 7 | setup.led);
    strcpy(p->slot[2].text, LN[setup.led]);
    strcpy(p->slot[3].name, "KEY CHANNEL"); p->slot[3].kind = R4P_KIND_UNI;
    p->slot[3].value = (uint16_t)(setup.chan * R4P_V14_MAX / 15);
    strcpy(t, "CH "); u_int((unsigned)setup.chan + 1u, t + 3);
    strcpy(p->slot[3].text, t);
}
static void setup_open(int on)
{
    if (on == ui.setup)
        return;
    ui.setup = (uint8_t)on;
    if (on) {
        setup_fill();
        ctl.setup_dirty = 0;
        memset(ctl.setup_acc, 0, sizeof ctl.setup_acc);
    } else if (ctl.setup_dirty) {
        plat_store_save(OBJ_SETUP, &setup, sizeof setup);
        ctl.setup_dirty = 0;
    }
    ui.dirty = D_ALL;
}
static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }
static void setup_turn(int which, int d)               /* K1..K4: one step per detent */
{
    switch (which) {
    case 0: setup.vel = (uint8_t)wrap(setup.vel, d, VEL_N); setup_apply(); break;
    case 1: setup.at = (uint8_t)wrap(setup.at, d, AT_N); break;
    case 2: setup.led = (uint8_t)wrap(setup.led, d, 3); break;
    default: {
        int c = setup.chan + d;
        setup.chan = (uint8_t)(c < 0 ? 0 : c > 15 ? 15 : c);
        break;
    }
    }
    ctl.setup_dirty = 1;
    setup_fill();
    ui.dirty |= D_TILES;
}

/* Nothing here may wait: plat_sysex_send blocks up to 200 ms when the host stops reading, so a frame goes only when the
 * ring has the room for it whole (plat_sysex_room). Returns 1 = queued, 0 = kept for later (the caller retries). */
static int send_frame(const uint8_t *f, size_t n)
{
    if (!n)
        return 0;
    if (plat_sysex_room() < (uint32_t)n) {
        ctl.tx_deferred++;
        return 0;
    }
    if (plat_sysex_send(f, (uint32_t)n) != 0)
        return 0;
    ctl.frames_sent++;
    return 1;
}

static void send_hello(void)
{
    uint8_t f[16];
    if (send_frame(f, r4p_hello(f, sizeof f, FW_MAJOR, FW_MINOR, R4P_MODE_RUN4)))
        ctl.hello_at = plat_ms();                      /* (no room: try again next frame, not every second) */
}

static int send_pot(void)
{
    uint8_t f[16];
    uint32_t v = (plat_master() * (uint32_t)R4P_V14_MAX + 2048u) / 4096u;
    if (v > R4P_V14_MAX) v = R4P_V14_MAX;
    if (!send_frame(f, r4p_pot(f, sizeof f, (uint16_t)v)))
        return 0;
    ctl.pot_sent = (int32_t)v;
    ctl.pot_at = plat_ms();
    return 1;
}

/* BTN events queue here and go out oldest first, none dropped; everything else waits behind them (order matters) */
static void txq_flush(void)
{
    uint8_t f[16];
    while (ctl.txq_n) {
        uint8_t e = ctl.txq[0];
        if (!send_frame(f, r4p_btn(f, sizeof f, (uint8_t)(e >> 1), e & 1u)))
            return;
        memmove(ctl.txq, ctl.txq + 1, --ctl.txq_n);
    }
}

/* ------------------------------------------------------------------ input --- */
static void keys_poll(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (!((keys ^ ctl.keys) & bit))
            continue;
        if (ctl.session && plat_midi_out_room(TO_USB) == 0)
            continue;                                  /* no room: the change stays unseen and is taken next frame */
        if (keys & bit) {
            int32_t note = 53 + (int32_t)k + 12 * ctl.octave;     /* key 0 is F3 = MIDI 53 */
            ctl.keys |= bit;
            if (note < 0 || note > 127) {
                ctl.sounding[k] = 255;
                continue;
            }
            ctl.sounding[k] = (uint8_t)note;
            ctl.snd_ch[k] = setup.chan;
            ctl.prs[k] = 0;
            ctl.notes_on++;
            plat_midi_send(0x09u | (0x90u | setup.chan) << 8 | (uint32_t)note << 16 | (uint32_t)key_velocity(k) << 24, TO_USB);
        } else {
            ctl.keys &= ~bit;
            if (ctl.sounding[k] != 255) {
                ctl.notes_off++;
                plat_midi_send(0x08u | (0x80u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16, TO_USB);
            }
            ctl.sounding[k] = 255;
        }
    }
    if (setup.at != AT_OFF && ctl.session && plat_midi_out_room(TO_USB) >= 4u) {      /* pressure of the held keys */
        uint32_t mx = 0;
        for (k = 0; k < NKEYS; k++) {
            uint32_t p;
            if (!(ctl.keys & (1u << k)) || ctl.sounding[k] == 255)
                continue;
            p = touch_pressure(k);
            if (p > mx) mx = p;
            if (setup.at == AT_POLY && (p > ctl.prs[k] ? p - ctl.prs[k] : ctl.prs[k] - p) >= 2u) {
                ctl.prs[k] = (uint8_t)p;
                plat_midi_send(0x0Au | (0xA0u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16 | p << 24, TO_USB);
            }
        }
        if (setup.at == AT_CHANNEL && ((mx == 0 && ctl.chan_prs) || (mx > ctl.chan_prs ? mx - ctl.chan_prs : ctl.chan_prs - mx) >= 2u)) {
            ctl.chan_prs = (uint8_t)mx;
            plat_midi_send(0x0Du | (0xD0u | setup.chan) << 8 | mx << 16, TO_USB);
        }
    }
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
    uint32_t btn = plat_buttons(), keys, k, now = plat_ms();
    touch_poll();                                      /* velocity and pressure (lib/touch) */
    keys = touch_keys();
    uint8_t shift = (uint8_t)((btn >> B_SEL) & 1u);   /* SEL held = SHIFT */
    int32_t pot;

    if (plat_usb_session() != ctl.session) {           /* a new computer: say hello again */
        ctl.session = plat_usb_session();
        ui_connect(0);
        ctl.hello_at = now - HELLO_MS;
        ctl.pot_sent = -1;
        ctl.txq_n = 0;
        ctl.want_sync = ctl.want_pot = 0;
    }
    if (!ctl.session)
        ctl.hello_at = now - HELLO_MS;                 /* (nothing to send to yet; try at the first chance) */
    if (!ui.connected && now - ctl.hello_at >= HELLO_MS)
        send_hello();

    for (k = 0; k < NB; k++) {                         /* buttons: forwarded as they are; OCT-/OCT+ also act here */
        uint32_t bit = 1u << k;
        if (!((btn ^ ctl.btn) & bit))
            continue;
        if (ctl.txq_n >= TXQ_N)
            break;                                     /* queue full: this change is seen again next frame */
        ctl.btn = (ctl.btn & ~bit) | (btn & bit);
        if (ctl.suppress & bit) {                      /* SETUP's: stays local until released */
            if (!(btn & bit)) ctl.suppress &= ~bit;
            continue;
        }
        if (btn & bit) {
            if (shift && k == B_GLO && !ui.setup) {    /* SEL + GLO opens the SETUP page; the press stays local */
                setup_open(1);
                ctl.suppress |= bit;
                continue;
            }
            if (ui.setup) {                            /* nothing reaches the host meanwhile; a page button leaves */
                ctl.suppress |= bit;
                if (k == B_FX || k == B_ENV || k == B_LFO || k == B_EDIT || k == B_GLO || k == B_HOME || k == B_ARP || k == B_SEQ)
                    setup_open(0);
                continue;
            }
            if (!shift && k == B_OCTDN) octave_step(-1);
            else if (!shift && k == B_OCTUP) octave_step(1);
        }
        ctl.txq[ctl.txq_n++] = (uint8_t)(k << 1 | ((btn & bit) ? 1u : 0u));
    }
    if (shift != ui.shift) {
        ui.shift = shift;
        ui.dirty |= D_FOOT;
    }
    txq_flush();

    keys_poll(keys);

    if (ctl.txq_n)
        return;                                        /* the rest waits behind the buttons */
    if (ctl.want_sync && send_frame(f, r4p_sync_req(f, sizeof f)))
        ctl.want_sync = 0;
    if (!ctl.want_sync && ctl.want_pot && send_pot())
        ctl.want_pot = 0;

    for (k = 0; k < R4P_NENC; k++) {                   /* encoders: the detents of this frame, one frame each; coalesced while the ring is full */
        int d = plat_enc((int)k);
        if (ui.setup) {                                /* SETUP: K1..K4 edit, the rest does nothing; none of it goes out */
            if (k >= 3 && d)
                for (; d; d += d > 0 ? -1 : 1)
                    setup_turn((int)k - 3, d > 0 ? 1 : -1);
            continue;
        }
        if (d)
            r4p_enc_add(&ctl.enc, (uint8_t)k, d);
        if (plat_sysex_room() >= 10u) {
            int t = r4p_enc_take(&ctl.enc, (uint8_t)k);
            if (t)
                send_frame(f, r4p_enc(f, sizeof f, (uint8_t)k, t));
        }
    }

    pot = (int32_t)((plat_master() * (uint32_t)R4P_V14_MAX + 2048u) / 4096u);   /* MASTER: only real movement goes out */
    if (ctl.pot_sent >= 0 && !ctl.want_pot && now - ctl.pot_at >= POT_MIN_MS &&
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
    plat_lights(lb, lk, db, dk, setup.led);
}

/* ------------------------------------------------------------ host frames --- */
int app_sysex(const uint8_t *p, uint32_t n)
{
    r4p_msg_t m;
    int r = r4p_parse_body(p, n, &m);
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
        ctl.pot_sent = -1;
        ctl.want_sync = 1;
        ctl.want_pot = 1;
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
    memset(ctl.snd_ch, 0, sizeof ctl.snd_ch);
    ui.setup = 0;
    setup_load();
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
    touch_on(1);                                       /* the keys' readings: velocity and pressure */
    setup_apply();
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
    else if (!strcmp(what, "setup")) *got = ui.setup;
    else if (!strcmp(what, "set_vel")) *got = setup.vel;
    else if (!strcmp(what, "set_at")) *got = setup.at;
    else if (!strcmp(what, "set_led")) *got = setup.led;
    else if (!strcmp(what, "set_chan")) *got = setup.chan + 1;
    else if (!strcmp(what, "toast")) *got = ui.toast_until != 0;
    else if (!strcmp(what, "notes_on")) *got = (int)ctl.notes_on;
    else if (!strcmp(what, "notes_off")) *got = (int)ctl.notes_off;
    else if (!strcmp(what, "frames_in")) *got = (int)ctl.frames_in;
    else if (!strcmp(what, "frames_bad")) *got = (int)ctl.frames_bad;
    else if (!strcmp(what, "frames_sent")) *got = (int)ctl.frames_sent;
    else if (!strcmp(what, "btn_pending")) *got = ctl.txq_n;
    else if (!strcmp(what, "tx_deferred")) *got = (int)ctl.tx_deferred;
    else return 0;
    return 1;
}
#endif
