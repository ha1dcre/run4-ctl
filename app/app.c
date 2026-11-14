/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: a thin terminal for the RUN4 iOS app. The only logic here is the firmware-local rules (SEL = SHIFT,
 * OCT-/OCT+ shift the keyboard) and the protocol (docs/PROTOCOL.md); everything else is forwarded as BTN / ENC / POT
 * and drawn from what the host sends. Part of the unity build (host/plat_host.c, device/unity.c), after the platform. */
#include <string.h>
#include "r4p.c"
#include "ui.c"
#include "synth.c"
#include "synth_ui.c"

#define FW_MAJOR 0
#define FW_MINOR 4
enum { MODE_RUN4 = 0, MODE_SYNTH = 1 };      /* SEL + HOME switches (docs/PROTOCOL.md); HELLO reports it */
#define KEY_VEL 100                      /* fixed velocity, and the fallback when a key has no reading */
#define HELLO_MS 1000u
#define POT_STEP 6                       /* 14-bit counts: the ADC's noise stays quiet */
#define POT_MIN_MS 20u
#define TXQ_N 32                         /* pending BTN events; beyond that a change waits in the button state itself */

/* the SETUP page's settings (docs/PROTOCOL.md "Firmware-local"): kept in the store, so they survive a power cycle */
enum { VEL_SOFT, VEL_NORMAL, VEL_HARD, VEL_FIXED, VEL_N };
enum { AT_OFF, AT_POLY, AT_CHANNEL, AT_N };
typedef struct { uint8_t ver, vel, at, led, chan, mode; } setup_t;   /* mode: the mode the FM-1 boots into */       /* chan 0..15 = MIDI channel 1..16; led 0 off, 1 low, 2 high */
#define SETUP_VER 1
static const int8_t VEL_CURVE[VEL_N] = {-3, 0, 3, 0};              /* touch_curve(): needs tuning on the device */
static setup_t setup;
static uint8_t mode;                                              /* MODE_RUN4 / MODE_SYNTH */

static struct {
    uint32_t btn, keys;                  /* held at the last poll */
    uint8_t sounding[NKEYS];             /* ... */
    uint8_t snd_ch[NKEYS];               /* ... and the channel it started on */
    uint8_t prs[NKEYS];                  /* last pressure sent per key (poly aftertouch) */
    uint8_t chan_prs;                    /* last channel pressure sent */
    uint32_t suppress;                   /* buttons whose events stay local until released (the SETUP page's) */
    uint32_t fwd;                        /* Synth mode: buttons pressed with SEL that go to the host (REC, PLAY) until released */
    int8_t setup_acc[4];
    uint8_t setup_dirty;             /* the note each held key started (255 = none), so a note-off always matches */
    int8_t octave;                       /* -3 .. +3, local */
    r4p_enc_acc_t enc;
    uint16_t txq[TXQ_N];                 /* events not yet sent, oldest first (a stalled host): BTN (id << 1) | down,
                                          * or LIST_PICK 0x8000 | action << 8 | index */
    uint8_t host_minor;                  /* the host's protocol minor from its HELLO_ACK (0 = 1.0: no LIST) */
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
    setup = (setup_t){SETUP_VER, VEL_NORMAL, AT_OFF, 2, 0, MODE_RUN4};
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
        t.led <= 2 && t.chan < 16 && t.mode <= MODE_SYNTH)
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

/* The host's link: USB (a session number) or, with no USB computer, Bluetooth MIDI (a counter that moves at each new
 * connection). Frames go to every link that is up; the notes too (plat_midi_send filters to the ones that listen). */
#if defined(FM1B_BLE) && FM1B_BLE
#define HAVE_BLE 1
#define TO_LINKS (TO_USB | TO_BLE_HOST)
static uint32_t ble_gen;
static int ble_was;
#else
#define HAVE_BLE 0
#define TO_LINKS TO_USB
#endif
static uint32_t link_id(void)
{
    uint32_t u = plat_usb_session();
#if HAVE_BLE
    int b = plat_ble_connected();
    if (b && !ble_was)
        ble_gen++;
    ble_was = b;
    if (!u && b)
        return 0x40000000u + ble_gen;
#endif
    return u;
}
#if HAVE_BLE
/* a SysEx frame as USB-MIDI packets (CIN 4 = three bytes and more to come, 5 / 6 / 7 = the last 1 / 2 / 3), to the computer's link */
static uint32_t ble_packets(size_t n) { return (uint32_t)((n + 2u) / 3u); }
static void ble_put(const uint8_t *f, size_t n)
{
    size_t i;
    for (i = 0; i < n; i += 3) {
        size_t left = n - i;
        uint32_t cin = left > 3 ? 4u : left == 3 ? 7u : left == 2 ? 6u : 5u;
        uint32_t pkt = cin | (uint32_t)f[i] << 8 | (left > 1 ? (uint32_t)f[i + 1] << 16 : 0u) | (left > 2 ? (uint32_t)f[i + 2] << 24 : 0u);
        plat_midi_send(pkt, TO_BLE_HOST);
    }
}
#endif

/* Nothing here may wait: plat_sysex_send blocks up to 200 ms when the host stops reading, so a frame goes only when the
 * ring has the room for it whole (plat_sysex_room), and the Bluetooth queue the same. Returns 1 = queued (on every link
 * that is up), 0 = kept for later (the caller retries). */
static int tx_ok(size_t n)                      /* would a frame of n bytes be queued now, on every link that is up? */
{
    int usb = plat_usb_session() != 0, ble = 0;
#if HAVE_BLE
    ble = plat_ble_connected();
    if (ble && plat_midi_out_room(TO_BLE_HOST) < ble_packets(n))
        return 0;
#endif
    return (usb || !ble) ? plat_sysex_room() >= (uint32_t)n : 1;       /* (no link at all: no room, as before) */
}
static int send_frame(const uint8_t *f, size_t n)
{
    int usb = plat_usb_session() != 0, ble = 0;
    if (!n)
        return 0;
    if (!tx_ok(n)) {
        ctl.tx_deferred++;
        return 0;
    }
#if HAVE_BLE
    ble = plat_ble_connected();
#endif
    if ((usb || !ble) && plat_sysex_send(f, (uint32_t)n) != 0)
        return 0;
#if HAVE_BLE
    if (ble)
        ble_put(f, n);
#endif
    ctl.frames_sent++;
    return 1;
}

static void send_hello(void)
{
    uint8_t f[16];
    if (send_frame(f, r4p_hello(f, sizeof f, FW_MAJOR, FW_MINOR, mode == MODE_SYNTH ? R4P_MODE_SYNTH : R4P_MODE_RUN4)))
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
        uint16_t e = ctl.txq[0];
        size_t n = (e & 0x8000u) ? r4p_list_pick(f, sizeof f, (uint8_t)(e & 0x7Fu), (uint8_t)((e >> 8) & 3u))
                                 : r4p_btn(f, sizeof f, (uint8_t)(e >> 1), e & 1u);
        if (!send_frame(f, n))
            return;
        ctl.txq_n--;
        memmove(ctl.txq, ctl.txq + 1, ctl.txq_n * sizeof ctl.txq[0]);
    }
}

/* ----------------------------------------------------------- host LIST --- */
static int ctl_queue(uint16_t e)
{
    if (ctl.txq_n >= TXQ_N)
        return 0;
    ctl.txq[ctl.txq_n++] = e;
    return 1;
}
static void list_pick(int action)                      /* the list closes here; the host may open another */
{
    ctl_queue((uint16_t)(0x8000u | (unsigned)action << 8 | ui.list.sel));
    ui.list_open = 0;
    ui.dirty = D_ALL;
}
static void list_move(int d)
{
    r4p_list_t *l = &ui.list;
    int s = (int)l->sel + d;
    s = s < 0 ? 0 : s >= (int)l->total ? (int)l->total - 1 : s;
    if (s == l->sel)
        return;
    l->sel = (uint8_t)s;
    ui.dirty = D_ALL;
    if (s < l->first || s >= l->first + l->n)          /* outside what the host sent: ask for that window */
        ctl_queue((uint16_t)(0x8000u | (unsigned)R4P_PICK_MOVE << 8 | (unsigned)s));
}

static void octave_step(int d);
/* ------------------------------------------------------------- Synth mode --- */
static void synth_notes_off_keys(void)         /* held keys: the note-offs of the mode being left */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        if (ctl.sounding[k] != 255 && mode == MODE_RUN4 && plat_midi_out_room(TO_LINKS))
            plat_midi_send(0x08u | (0x80u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16, TO_LINKS);
        else if (ctl.sounding[k] != 255 && mode == MODE_SYNTH)
            syn_event(2, ctl.sounding[k], 0, 0);
        ctl.sounding[k] = 255;
    }
}
static void mode_switch(uint8_t to)
{
    if (to == mode)
        return;
    synth_notes_off_keys();
    if (mode == MODE_SYNTH) {                      /* leaving: the sound stops, the settings are kept */
        syn_event(6, 0, 0, 0);
        syn_event(8, 0, 0, 0);
        sq.rec = 0;
        sq_ed.held = -1;
        sq_ed.entry_open = 0;
        if (sui.dirty_set || syn_bank_dirty || sq_dirty)
            syn_save_all();
    }
    mode = to;
    setup.mode = to;
    plat_store_save(OBJ_SETUP, &setup, sizeof setup);
    ui.setup = 0;
    ui.list_open = 0;
    ui_connect(0);                                 /* a new HELLO, with the new mode, until the host answers */
    ctl.hello_at = plat_ms() - HELLO_MS;
    ui.local = to == MODE_SYNTH;
    if (ui.local) {
        syn_event(6, 0, 0, 0);
        sp_build();
    } else {
        ui_reset_page();
    }
    ui.dirty = D_ALL;
}
static int synth_step_rec(void)                /* step recording: the SEQ page, REC armed, stopped */
{
    return sui.page == SP_SEQ && sq.rec && !sq.run && !ui.setup;
}
static void synth_button(uint32_t k)
{
    if (synth_step_rec() && (k == B_EDIT || k == B_GLO)) {   /* step recording: EDIT a rest, GLO a TIE */
        if (k == B_EDIT) sq_ed_rest(); else sq_ed_tie();
        sui.edit_at = plat_ms();
        sp_toast(k == B_EDIT ? "REST" : "TIE");
        sp_build();
        return;
    }
    switch (k) {
    case B_HOME: sp_page_press(SP_HOME); break;
    case B_EDIT: sp_page_press(SP_EDIT); break;
    case B_ENV: sp_page_press(SP_ENV); break;
    case B_LFO: sp_page_press(SP_LFO); break;
    case B_FX: sp_page_press(SP_FX); break;
    case B_GLO: sp_page_press(SP_GLO); break;
    case B_ARP: sp_page_press(SP_ARP); break;
    case B_SEQ: sp_page_press(SP_SEQ); break;
    case B_PLAY:                                   /* the render owns the clock; its state is mirrored in sq.run */
        sq_ed.follow = 1;
        syn_event(8, (uint8_t)!sq.run, 0, 0);
        break;
    case B_REC:
        sq.rec ^= 1u;
        sq_ed.entry_open = 0;
        if (sq.rec && !sq.run && sui.page == SP_SEQ)
            sp_toast("EDIT REST  GLO TIE");
        sp_build();
        break;
    case B_SAVE:
        if (!sq_save_press())
            sp_save_patch();
        break;
    case B_OCTDN: octave_step(-1); break;
    case B_OCTUP: octave_step(1); break;
    default: break;
    }
}
static const uint8_t WHITE_KEYS[16] = PLAT_WHITE_KEYS, BLACK_KEYS[11] = PLAT_BLACK_KEYS;
static int key_white(uint32_t k) { uint32_t i; for (i = 0; i < 16u; i++) if (WHITE_KEYS[i] == k) return (int)i; return -1; }
static int key_black(uint32_t k) { uint32_t i; for (i = 0; i < 11u; i++) if (BLACK_KEYS[i] == k) return (int)i; return -1; }
static int synth_grid(void)                    /* the SEQ page's grid: the white keys are steps, the black ones pages */
{
    return sui.page == SP_SEQ && !sq.rec && !ui.setup;
}
static void synth_keys(uint32_t keys)
{
    uint32_t k;
    int shift = (int)((ctl.btn >> B_SEL) & 1u);
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (!((keys ^ ctl.keys) & bit))
            continue;
        if (keys & bit) {
            int32_t note = 53 + (int32_t)k + 12 * ctl.octave;
            uint32_t j;
            ctl.keys |= bit;
            ctl.sounding[k] = 255;
            if (shift && !ui.setup) {                            /* SEL + a key: the sequence's transpose (C4 = +0) */
                sq_transpose_key(note);
                continue;
            }
            if (synth_grid() && sq_ed.held < 0) {               /* the grid: a step held, or a step page */
                int w = key_white(k), b = key_black(k);
                if (w >= 0) {
                    sq_ed_step_down(sq_ed.view * SQ_PAGE + (uint32_t)w, k);
                    sq_ed.follow = 0;
                } else if (b >= 0 && b < 4 && (uint32_t)b * SQ_PAGE < sq_ed_pat()->len) {
                    sq_ed.view = (uint8_t)b;
                    sq_ed.follow = 0;
                }
                sp_build();
                continue;
            }
            if (sp_op_page() && !ui.setup) {                    /* EDIT / ENV: the six lowest black keys pick the operator */
                for (j = 0; j < 6u; j++)
                    if (SP_BLACK6[j] == k) {
                        sp_select_op(j);
                        break;
                    }
                if (j < 6u) {
                    ctl.sounding[k] = 255;
                    continue;
                }
            }
            if (note < 0 || note > 127) {
                ctl.sounding[k] = 255;
                continue;
            }
            ctl.sounding[k] = (uint8_t)note;
            ctl.notes_on++;
            if (synth_grid() && sq_ed.held >= 0) {              /* a step held: the key is its note (a chord with more) */
                sq_ed_note((uint8_t)note);
                sui.edit_at = plat_ms();
                sp_build();
                syn_event(1, (uint8_t)note, key_velocity(k), 1);     /* (heard, not recorded, no arp) */
                ctl.snd_ch[k] = 1;
                continue;
            }
            if (synth_step_rec()) {                               /* step recording: the keys down together, one step */
                sq_ed_entry((uint8_t)note, key_velocity(k));
                sui.edit_at = plat_ms();
                sp_build();
                syn_event(1, (uint8_t)note, key_velocity(k), 1);
                ctl.snd_ch[k] = 1;
                continue;
            }
            syn_event(1, (uint8_t)note, key_velocity(k), 0);
            ctl.snd_ch[k] = 0;                                    /* (Synth mode: the event's source, for its note-off) */
        } else {
            ctl.keys &= ~bit;
            if (sq_ed.held >= 0 && sq_ed.held_key == k) {       /* the held step let go: a tap toggles it */
                sq_ed_step_up();
                sui.edit_at = plat_ms();
                sp_build();
            }
            if (ctl.sounding[k] != 255) {
                ctl.notes_off++;
                syn_event(2, ctl.sounding[k], 0, ctl.snd_ch[k]);
            }
            ctl.sounding[k] = 255;
        }
    }
    if (sq_ed.entry_open && !keys) {                   /* step recording: all keys up, the next step */
        sq_ed_entry_done();
        sp_build();
    }
}
static void synth_frame(uint32_t keys, uint32_t now)
{
    uint32_t k;
    synth_keys(keys);
    for (k = 0; k < R4P_NENC; k++) {
        int d = plat_enc((int)k);
        if (!d)
            continue;
        if (ui.setup) {
            if (k >= 3)
                for (; d; d += d > 0 ? -1 : 1)
                    setup_turn((int)k - 3, d > 0 ? 1 : -1);
            continue;
        }
        if (k >= 3) {
            sp_turn(k - 3u, d);
        } else if (k == 0 && sui.page == SP_SEQ) {       /* SELECT on the SEQ page: the step page */
            int v = (int)sq_ed.view + (d > 0 ? 1 : -1);
            uint32_t pages = (sq_ed_pat()->len + SQ_PAGE - 1u) / SQ_PAGE;
            sq_ed.view = (uint8_t)(v < 0 ? 0 : v >= (int)pages ? (int)pages - 1 : v);
            sq_ed.follow = 0;
            ui.dirty |= D_TILE0;
        } else if (k == 0) {                             /* SELECT: the operator */
            sp_select_op((uint32_t)((int)sui.op + (d > 0 ? 1 : -1) < 0 ? 0 : (int)sui.op + (d > 0 ? 1 : -1)));
        } else if (k == 1) {                             /* ALGORITHM */
            int a = (int)(syn.cur[FP_ALG] & 31u) + d;
            char t[24] = "ALGORITHM ", u[12];
            a = a < 0 ? 0 : a > 31 ? 31 : a;
            syn.cur[FP_ALG] = (uint8_t)a;
            sp_touch();
            u_int((unsigned)a + 1u, u); u_cat(t, u);
            sp_toast(t);
            sp_build();
        } else {
            sp_patch_step(d);
        }
    }
    /* the transport as the footer shows it */
    {
        r4p_status_t st;
        st.bpm10 = (uint16_t)syn_bpm10_now();
        st.bar = (uint16_t)(sq.run ? sq.ticks / (4u * SQ_PPQN) + 1u : 1u);
        st.beat = (uint8_t)(sq.run ? (sq.ticks / SQ_PPQN) % 4u : 0u);
        st.flags = (uint8_t)((sq.run ? R4P_ST_PLAY : 0u) | (sq.rec ? R4P_ST_REC : 0u));
        if (memcmp(&st, &ui.st, sizeof st)) {
            ui.st = st;
            ui.dirty |= D_FOOT;
        }
    }
    {
        uint32_t i = syn.patch_idx < SYN_NFACTORY ? 0u : syn.patch_idx - SYN_NFACTORY;     /* where a single-voice dump goes */
        if (syn.patch_idx < SYN_NFACTORY)
            for (i = 0; i < 2u * SYN_BANK_N && syn_patch_used(SYN_NFACTORY + i); i++)
                ;
        syn_imp_slot = (uint8_t)(i < 2u * SYN_BANK_N ? i : 0u);
    }
    if (syn_imp_done) {                                  /* a DX7 dump arrived (app_sysex_byte): keep it */
        char t[24] = "IMPORTED ", u[12];
        u_int(syn_imp_done, u); u_cat(t, u); u_cat(t, syn_imp_done == 1u ? " VOICE" : " VOICES");
        syn_imp_done = 0;
        syn_save_all();
        sp_toast(t);
    }
    /* a few seconds after the last edit, and only while the sequencer is stopped: a flash write stops the sound */
    if ((sui.dirty_set || sq_dirty) && !sq.run && sq_ed.held < 0 && now - sui.edit_at > 4000u)
        syn_save_all();
    sq_frame(now);
}
static void synth_lights(uint32_t *lb, uint32_t *lk, uint32_t *db, uint32_t *dk)
{
    static const uint8_t PB[SP_N] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_GLO, B_ARP, B_SEQ};
    uint32_t i;
    *lb |= 1u << PB[sui.page];
    if (sq.run) *lb |= 1u << B_PLAY;
    if (sq.rec) *lb |= ctl.blink ? 1u << B_REC : 0u;
    if (sp_op_page()) {                                  /* the six operator keys */
        for (i = 0; i < 6u; i++)
            *(i == sui.op ? lk : dk) |= 1u << SP_BLACK6[i];
    } else if (synth_grid()) {                           /* the grid: steps on the white keys, step pages on four black */
        const sq_pat_t *pt = sq_ed_pat();
        int playing = sq.run && sq.pat == sq_sel_pat();
        for (i = 0; i < 16u; i++) {
            uint32_t idx = sq_ed.view * SQ_PAGE + i, bit = 1u << WHITE_KEYS[i];
            if (idx >= pt->len)
                continue;
            if (sq_plays(&pt->s[idx]))
                *lk |= bit;                              /* set: lit */
            else if (sq_has(&pt->s[idx]))
                *dk |= bit;                              /* muted (notes kept): dim */
            if (playing && sq.step == idx)
                *lk ^= bit;                              /* the playing step: the other way round */
        }
        for (i = 0; i < 4u; i++) {
            uint32_t bit = 1u << BLACK_KEYS[i];
            if (i * SQ_PAGE >= pt->len)
                continue;
            if (i == sq_ed.view) *lk |= bit; else *dk |= bit;
            if (playing && sq.step / SQ_PAGE == i && ctl.blink && i != sq_ed.view)
                *lk |= bit;
        }
        if (sq_ed.held >= 0) {                           /* a step held: its notes on the keys */
            const sq_step_t *s = &pt->s[sq_ed.held];
            for (i = 0; i < sq_n(s); i++) {
                int32_t k = (int32_t)s->note[i] - 53 - 12 * ctl.octave;
                if (k >= 0 && k < NKEYS)
                    *lk |= 1u << k;
            }
        }
    } else if (synth_step_rec()) {                       /* step recording: the cursor step's notes, dim */
        const sq_step_t *s = &sq_ed_pat()->s[sq_ed.cursor % SQ_STEPS];
        for (i = 0; i < sq_n(s); i++) {
            int32_t k = (int32_t)s->note[i] - 53 - 12 * ctl.octave;
            if (k >= 0 && k < NKEYS)
                *dk |= 1u << k;
        }
    }
    *lk |= ctl.keys;
}

/* SysEx frames from Bluetooth MIDI (USB's come through app_sysex): assembled here from USB-MIDI SysEx packets (CIN 4..7) in
 * the audio context, handed to the main loop whole, where app_sysex takes them as it takes USB's. One frame waits at a
 * time; a frame that finds the slot taken is dropped (counted). */
#define RX_MAX 640
static uint8_t rx_asm[RX_MAX], rx_frame[RX_MAX];
static uint16_t rx_n, rx_len;
static uint8_t rx_open, rx_over;
static volatile uint8_t rx_ready;
static uint32_t rx_drop;
static void rx_byte(uint8_t c)
{
    if (c == 0xF0u) {
        rx_open = 1;
        rx_over = 0;
        rx_n = 0;
        if (mode == MODE_SYNTH)
            syn_sysex_byte(c);
        return;
    }
    if (!rx_open)
        return;
    if (mode == MODE_SYNTH)
        syn_sysex_byte(c);
    if (c == 0xF7u) {
        rx_open = 0;
        if (!rx_over && rx_n) {
            if (rx_ready) {
                rx_drop++;
            } else {
                memcpy(rx_frame, rx_asm, rx_n);
                rx_len = rx_n;
                rx_ready = 1;
            }
        }
        return;
    }
    if (c >= 0x80u) {                                  /* another status inside: the frame is damaged */
        rx_open = 0;
        return;
    }
    if (rx_n < RX_MAX)
        rx_asm[rx_n++] = c;
    else
        rx_over = 1;
}

/* the MIDI in (USB, Bluetooth, the jack): Bluetooth's SysEx packets feed the frame assembler; in Synth mode the channel
 * messages play the synth. Audio context, as the template reads it */
static void midi_pump(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t cin = pkt & 0xFu, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ch = st & 0x0Fu;
        if (cin >= 4u && cin <= 7u) {                  /* SysEx packets: 4 = 3 bytes, more to come; 5, 6, 7 = the last 1, 2, 3 */
            uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u, i;
            if (port == PORT_BLE)
                for (i = 0; i < n; i++)
                    rx_byte((uint8_t)(pkt >> (8 + 8 * i)));
            continue;
        }
        if (mode == MODE_SYNTH && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
            sq_realtime((uint8_t)st);                  /* MIDI clock, start, stop: the sequencer and the arp follow */
            continue;
        }
        if (mode != MODE_SYNTH || st >= 0xF0u)
            continue;
        if (syn.chan && ch != (uint32_t)syn.chan - 1u)
            continue;
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2) { syn_apply_event(&(syn_ev_t){1, (uint8_t)d1, (uint8_t)d2, 0}); break; }
            /* fallthrough: velocity 0 is a note-off */
        case 0x80u:
            syn_apply_event(&(syn_ev_t){2, (uint8_t)d1, 0, 0});
            break;
        case 0xB0u:
            if (d1 == 64u) syn_apply_event(&(syn_ev_t){5, (uint8_t)d2, 0, 0});
            else if (d1 == 123u || d1 == 120u) syn_apply_event(&(syn_ev_t){3, 0, 0, 0});
            break;
        case 0xE0u:
            syn_apply_event(&(syn_ev_t){4, (uint8_t)d1, (uint8_t)d2, 0});
            break;
        default:
            break;
        }
    }
}
void app_sysex_byte(uint8_t b)
{
    if (mode == MODE_SYNTH)
        syn_sysex_byte(b);
}

/* ------------------------------------------------------------------ input --- */
static void keys_poll(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (!((keys ^ ctl.keys) & bit))
            continue;
        if (ctl.session && plat_midi_out_room(TO_LINKS) == 0)
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
            plat_midi_send(0x09u | (0x90u | setup.chan) << 8 | (uint32_t)note << 16 | (uint32_t)key_velocity(k) << 24, TO_LINKS);
        } else {
            ctl.keys &= ~bit;
            if (ctl.sounding[k] != 255) {
                ctl.notes_off++;
                plat_midi_send(0x08u | (0x80u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16, TO_LINKS);
            }
            ctl.sounding[k] = 255;
        }
    }
    if (setup.at != AT_OFF && ctl.session && plat_midi_out_room(TO_LINKS) >= 4u) {      /* pressure of the held keys */
        uint32_t mx = 0;
        for (k = 0; k < NKEYS; k++) {
            uint32_t p;
            if (!(ctl.keys & (1u << k)) || ctl.sounding[k] == 255)
                continue;
            p = touch_pressure(k);
            if (p > mx) mx = p;
            if (setup.at == AT_POLY && (p > ctl.prs[k] ? p - ctl.prs[k] : ctl.prs[k] - p) >= 2u) {
                ctl.prs[k] = (uint8_t)p;
                plat_midi_send(0x0Au | (0xA0u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16 | p << 24, TO_LINKS);
            }
        }
        if (setup.at == AT_CHANNEL && ((mx == 0 && ctl.chan_prs) || (mx > ctl.chan_prs ? mx - ctl.chan_prs : ctl.chan_prs - mx) >= 2u)) {
            ctl.chan_prs = (uint8_t)mx;
            plat_midi_send(0x0Du | (0xD0u | setup.chan) << 8 | mx << 16, TO_LINKS);
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
    if (rx_ready) {                                    /* a frame that came over Bluetooth */
        app_sysex(rx_frame, rx_len);
        rx_ready = 0;
    }
    touch_poll();                                      /* velocity and pressure (lib/touch) */
    keys = touch_keys();
    uint8_t shift = (uint8_t)((btn >> B_SEL) & 1u);   /* SEL held = SHIFT */
    int32_t pot;

    if (link_id() != ctl.session) {           /* a new computer: say hello again */
        ctl.session = link_id();
        ui_connect(0);
        ctl.hello_at = now - HELLO_MS;
        ctl.pot_sent = -1;
        ctl.txq_n = 0;
        ctl.fwd = 0;
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
            if (ui.list_open && !ui.setup && mode == MODE_RUN4) {   /* a host LIST: HOME / PLAY pick, any other page button or SAVE cancels */
                ctl.suppress |= bit;
                if (k == B_HOME || k == B_PLAY)
                    list_pick(R4P_PICK_OK);
                else if (k == B_FX || k == B_ENV || k == B_LFO || k == B_EDIT || k == B_GLO || k == B_ARP || k == B_SEQ || k == B_SAVE)
                    list_pick(R4P_PICK_CANCEL);
                continue;
            }
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
            if (shift && k == B_HOME) {                /* SEL + HOME: the other mode */
                ctl.suppress |= bit;
                mode_switch(mode == MODE_SYNTH ? MODE_RUN4 : MODE_SYNTH);
                return;
            }
            if (mode == MODE_SYNTH && shift && (k == B_REC || k == B_PLAY) && ui.connected) {
                ctl.fwd |= bit;                        /* SEL + REC / PLAY with a host: RUN4's transport, not the synth's */
                ctl.txq[ctl.txq_n++] = (uint16_t)(k << 1 | 1u);
                sp_toast(k == B_REC ? "RUN4 REC" : "RUN4 PLAY");
                continue;
            }
            if (mode == MODE_SYNTH) {                  /* Synth mode keeps every button to itself */
                synth_button(k);
                continue;
            }
            if (!shift && k == B_OCTDN) octave_step(-1);
            else if (!shift && k == B_OCTUP) octave_step(1);
        }
        if (mode == MODE_SYNTH) {
            if (!(btn & bit) && (ctl.fwd & bit)) {     /* the release of a forwarded SEL + REC / PLAY */
                ctl.fwd &= ~bit;
                ctl.txq[ctl.txq_n++] = (uint16_t)(k << 1);
            }
            continue;
        }
        ctl.txq[ctl.txq_n++] = (uint16_t)(k << 1 | ((btn & bit) ? 1u : 0u));
    }
    if (shift != ui.shift) {
        ui.shift = shift;
        ui.dirty |= D_FOOT;
    }
    txq_flush();

    if (mode == MODE_SYNTH) {
        synth_frame(keys, now);
        return;
    }
    keys_poll(keys);

    if (ctl.txq_n)
        return;                                        /* the rest waits behind the buttons */
    if (ctl.want_sync && send_frame(f, r4p_sync_req(f, sizeof f)))
        ctl.want_sync = 0;
    if (!ctl.want_sync && ctl.want_pot && send_pot())
        ctl.want_pot = 0;

    for (k = 0; k < R4P_NENC; k++) {                   /* encoders: the detents of this frame, one frame each; coalesced while the ring is full */
        int d = plat_enc((int)k);
        if (ui.list_open && !ui.setup) {               /* a host LIST: SELECT / PRESETS move the selection */
            if ((k == 0 || k == 2) && d)
                list_move(d);
            continue;
        }
        if (ui.setup) {                                /* SETUP: K1..K4 edit, the rest does nothing; none of it goes out */
            if (k >= 3 && d)
                for (; d; d += d > 0 ? -1 : 1)
                    setup_turn((int)k - 3, d > 0 ? 1 : -1);
            continue;
        }
        if (d)
            r4p_enc_add(&ctl.enc, (uint8_t)k, d);
        if (tx_ok(10u)) {
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
    for (i = 0; i < NB && mode == MODE_RUN4; i++) {
        uint8_t s = ctl.leds.btn[i];
        if (s == R4P_LED_ON || (s == R4P_LED_BLINK && ctl.blink)) lb |= 1u << i;
        else if (s == R4P_LED_DIM) db |= 1u << i;
    }
    for (i = 0; i < NKEYS && mode == MODE_RUN4; i++) {
        uint8_t s = ctl.leds.key[i];
        if (s == R4P_LED_ON || (s == R4P_LED_BLINK && ctl.blink)) lk |= 1u << i;
        else if (s == R4P_LED_DIM) dk |= 1u << i;
    }
    if (mode == MODE_SYNTH)
        synth_lights(&lb, &lk, &db, &dk);
    else if (!ui.connected)
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
        if (mode == MODE_SYNTH) {                      /* the host knows we are a synth: nothing to draw or sync */
            ui.connected = 1;
            ctl.host_minor = m.u.ack.minor;
            break;
        }
        ui_connect(1);
        ctl.leds = (r4p_leds_t){{0}, {0}};
        ctl.host_minor = m.u.ack.minor;
        ctl.pot_sent = -1;
        ctl.want_sync = 1;
        ctl.want_pot = 1;
        break;
    case R4P_LEDS:
        ctl.leds = m.u.leds;
        break;
    case R4P_PAGE: case R4P_VALUE: case R4P_STATUS: case R4P_TOAST: case R4P_LIST:
        if (ui.connected && mode == MODE_RUN4)                              /* (frames before the ack are the previous session's) */
            ui_apply(&m);
        break;
    default:
        break;                                         /* device->host commands from the host: ignored */
    }
    return 1;
}

/* --------------------------------------------------------------- contract --- */
static void sq_send_rt(uint8_t b)                      /* the sequencer's MIDI clock out (clock OUT): render context */
{
    if (plat_midi_out_room(TO_LINKS))
        plat_midi_send(0x0Fu | (uint32_t)b << 8, TO_LINKS);
}
void app_init(void)
{
    memset(&ctl, 0, sizeof ctl);
    memset(ctl.sounding, 255, sizeof ctl.sounding);
    ctl.pot_sent = -1;
    memset(ctl.snd_ch, 0, sizeof ctl.snd_ch);
    ui.setup = 0;
    setup_load();
    syn_init();
    syn_load_all();
    sq_midi_out = sq_send_rt;
    memset(&sui, 0, sizeof sui);
    mode = setup.mode;
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    midi_pump();
    if (mode == MODE_SYNTH) {                          /* Synth mode: the FM voices; MASTER is the volume */
        syn_render(lr, n, master_q12);
        return;
    }
    for (i = 0; i < 2u * n; i++)                       /* RUN4 mode: a controller makes no sound */
        lr[i] = 0;
}
void app_panic(void)
{
    syn_event(6, 0, 0, 0);
}
void app_ui_init(void)
{
    touch_on(1);                                       /* the keys' readings: velocity and pressure */
    setup_apply();
    ctl.session = link_id();
    ctl.hello_at = plat_ms() - HELLO_MS;
    memset(&ui, 0, sizeof ui);
    ui_reset_page();
    ui.local = mode == MODE_SYNTH;
    if (ui.local)
        sp_build();
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
static void run_ms(uint32_t ms);
/* blesx HEX..: a SysEx frame (F0 .. F7) from the computer over Bluetooth, cut into USB-MIDI SysEx packets as the radio
 * hands them over; the stack's own packets are not simulated */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    static uint8_t by[8300];
    size_t n = 0, i;
    const char *p;
    if (strcmp(cmd, "blesx"))
        return 0;
    p = strstr(plat_host_line, "blesx") + 5;
    while (*p) {
        char *e;
        unsigned long v = strtoul(p, &e, 16);
        if (e == p)
            break;
        if (n < sizeof by)
            by[n++] = (uint8_t)v;
        p = e;
    }
    for (i = 0; i < n; i += 3) {
        size_t left = n - i;
        uint32_t cin = left > 3 ? 4u : left == 3 ? 7u : left == 2 ? 6u : 5u;
        min_q[mi_w % MQ] = cin | (uint32_t)by[i] << 8 | (left > 1 ? (uint32_t)by[i + 1] << 16 : 0u) | (left > 2 ? (uint32_t)by[i + 2] << 24 : 0u);
        min_port[mi_w++ % MQ] = PORT_BLE;
        if (mi_w % 64u == 0u)
            run_ms(7);                                 /* (the queue is small: a render block drains it) */
    }
    run_ms(20);
    return 1;
}
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "connected")) *got = ui.connected;
    else if (!strcmp(what, "octave")) *got = ctl.octave;
    else if (!strcmp(what, "shift")) *got = ui.shift;
    else if (!strcmp(what, "page_idx")) *got = ui.page.page_idx;
    else if (!strcmp(what, "setup")) *got = ui.setup;
    else if (!strcmp(what, "rx_drop")) *got = (int)rx_drop;
    else if (!strcmp(what, "link")) *got = ctl.session >= 0x40000000u ? 2 : ctl.session ? 1 : 0;
    else if (!strcmp(what, "mode")) *got = mode;
    else if (!strcmp(what, "syn_voices")) *got = (int)syn_active_voices;
    else if (!strcmp(what, "syn_patch")) *got = syn.patch_idx;
    else if (!strcmp(what, "syn_alg")) *got = (syn.cur[FP_ALG] & 31) + 1;
    else if (!strcmp(what, "syn_page")) *got = sui.page * 10 + sui.sub;
    else if (!strcmp(what, "syn_op")) *got = sui.op + 1;
    else if (!strcmp(what, "syn_seq")) *got = sq.run;
    else if (!strcmp(what, "seq_rec")) *got = sq.rec;
    else if (!strcmp(what, "seq_pat")) *got = syn.seq_pat + 1;             /* the selected pattern, 1..16 */
    else if (!strcmp(what, "seq_playing")) *got = sq.pat + 1;              /* the one playing */
    else if (!strcmp(what, "seq_len")) *got = sq_ed_pat()->len;
    else if (!strcmp(what, "seq_on")) *got = (int)sq_count_on(sq_ed_pat());  /* steps that play */
    else if (!strcmp(what, "seq_cursor")) *got = sq_ed.cursor + 1;
    else if (!strcmp(what, "seq_view")) *got = sq_ed.view + 1;
    else if (!strcmp(what, "seq_step")) *got = sq.step + 1;
    else if (!strcmp(what, "seq_trn")) *got = syn.seq_trn;
    else if (!strcmp(what, "seq_clock_ext")) *got = sq_ext();
    else if (!strcmp(what, "seq_dirty")) *got = sq_dirty;
    else if (!strncmp(what, "seq_n", 5) && what[5] >= '0' && what[5] <= '9') *got = (int)sq_n(&sq_ed_pat()->s[(atoi(what + 5) - 1) & 63]);      /* seq_nS: notes in step S */
    else if (!strncmp(what, "seq_note", 8) && what[8]) *got = sq_ed_pat()->s[(atoi(what + 8) - 1) & 63].note[0];     /* seq_noteS: its first */
    else if (!strncmp(what, "seq_find", 8) && what[8]) {                   /* seq_findN: steps holding note N */
        int i, j, c = 0, nt = atoi(what + 8);
        for (i = 0; i < sq_ed_pat()->len; i++)
            for (j = 0; j < (int)sq_n(&sq_ed_pat()->s[i]); j++)
                c += sq_ed_pat()->s[i].note[j] == nt;
        *got = c;
    }
    else if (!strncmp(what, "seq_tie", 7) && what[7]) *got = sq_is_tie(&sq_ed_pat()->s[(atoi(what + 7) - 1) & 63]);
    else if (!strncmp(what, "seq_plays", 9) && what[9]) *got = sq_plays(&sq_ed_pat()->s[(atoi(what + 9) - 1) & 63]);
    else if (!strcmp(what, "syn_bank_used")) *got = syn_bank[0].used | (syn_bank[1].used << 16);
    else if (!strcmp(what, "syn_arp")) *got = syn.arp_on;
    else if (!strcmp(what, "list")) *got = ui.list_open ? ui.list.sel + 1 : 0;
    else if (!strcmp(what, "host_minor")) *got = ctl.host_minor;
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
