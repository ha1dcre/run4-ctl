/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: a thin terminal for the RUN4 iOS app. The only logic here is the firmware-local rules (SEL = SHIFT,
 * OCT-/OCT+ shift the keyboard) and the protocol (docs/PROTOCOL.md); everything else is forwarded as BTN / ENC / POT
 * and drawn from what the host sends. Part of the unity build (host/plat_host.c, device/unity.c), after the platform. */
#include <string.h>
#include "r4p.c"
#include "ui.c"
#include "synth.c"
#include "synth_ui.c"
#include "tape_ui.c"

#define FW_MAJOR 0
#define FW_MINOR 6
enum { MODE_RUN4 = 0, MODE_SYNTH = 1 };      /* SEL + HOME switches (docs/PROTOCOL.md); HELLO reports it */
#define KEY_VEL 100                      /* fixed velocity, and the fallback when a key has no reading */
#define HELLO_MS 1000u
#define POT_STEP 6                       /* 14-bit counts: the ADC's noise stays quiet */
#define POT_MIN_MS 20u
#define TXQ_N 32                         /* pending BTN events; beyond that a change waits in the button state itself */

/* the SETUP page's settings (docs/PROTOCOL.md "Firmware-local"): kept in the store, so they survive a power cycle */
enum { VEL_SOFT, VEL_NORMAL, VEL_HARD, VEL_FIXED, VEL_N };
enum { AT_OFF, AT_POLY, AT_CHANNEL, AT_N };
#define ML_N 16                          /* MIDI learn's bindings (src/synth_learn.c) */
typedef struct { uint8_t cc, tgt, opch; } ml_bind_t;   /* cc 0 = free; tgt: page << 4 | sub-page << 2 | knob; opch: the
                                                        * operator (EDIT / ENV) | the channel 1..16 << 3 */
typedef struct {
    uint8_t ver, vel, at, led, chan, mode;   /* mode: the mode the FM-1 boots into; chan 0..15 = MIDI channel 1..16; led 0
                                              * off, 1 low, 2 high (layout 1 ends here: 6 bytes) */
    uint8_t local;                           /* layout 2: LOCAL OUT, 0 ON, 1 USB ONLY (while a computer is connected) */
    uint8_t ml_def;                          /* the default CC map on (1) */
    ml_bind_t ml[ML_N];                      /* MIDI learn */
} setup_t;
#define SETUP_VER 2
#define SETUP_V1_SIZE 6u
_Static_assert(sizeof(setup_t) == 56u, "SETUP store layout");
static const int8_t VEL_CURVE[VEL_N] = {-3, 0, 3, 0};              /* touch_curve(): needs tuning on the device */
static setup_t setup;
static uint8_t mode;                                              /* MODE_RUN4 / MODE_SYNTH */

static struct {
    uint32_t btn, keys;                  /* held at the last poll */
    uint8_t sounding[NKEYS];             /* ... */
    uint8_t snd_ch[NKEYS];               /* ... and the channel it started on */
    uint8_t chord[NKEYS][4], chord_n[NKEYS];   /* Synth mode: the notes each key started (the scale, the chord) */
    uint8_t more_key[NKEYS];             /* Synth mode's grid: the step a key holds with the first one (SEL), 255 = none */
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
    /* protocol 1.3: answers owed to the host (sent behind the buttons, as there is room) */
    uint8_t want_hvstate;                /* HV_STATE */
    uint8_t names_next;                  /* the next VOICE_NAMES frame's first voice; 255 = none owed */
    uint8_t vget1;                       /* VOICE_DATA for voice vget1 - 1 (127 + 1: the host voice's); 0 = none */
    uint8_t setup_sub;                   /* the SETUP page's sub-page: 0 the settings, 1 VOICE BACKUP, 2 AUDIO, 3 MIDI LEARN */
    uint8_t setup_owed;                  /* the SETUP object changed outside the page (a learn): saved when it may be */
    uint32_t rend_ticks, rend_frames;    /* the render's time (host simulator: expect render_permille) */
    uint32_t overloads;                  /* app_overload calls (late renders) */
    uint32_t redraws;                    /* app_redraw calls (the mirror) */
    uint8_t shed_told, shed_shown;       /* the overload guard's toast said; the step the AUDIO page shows */
} ctl;
#define bk_hold sy_hold_store            /* a USB backup streams: nothing is saved meanwhile (synth_ui.c, app_backup_hold) */

/* protocol 1.3, the host voice: RUN4 plays this engine in RUN4 mode (docs/PROTOCOL.md "Host voice"). Its settings
 * live in `syn` while RUN4 mode is on, in syn_alt while Synth mode is (synth.c syn_swap); RAM only. */
static struct {
    uint8_t on;                          /* the host turned it on (it sounds in RUN4 mode; suspended in Synth mode) */
    uint8_t chan;                        /* its MIDI channel, 0..15 */
    uint8_t voice;                       /* where its voice came from: 0..39 a stored one, R4P_VOICE_HV the host's */
} hv;
static syn_set_t *hv_set(void) { return mode == MODE_RUN4 ? &syn : &syn_alt; }

/* protocol 1.3, the patch backup (SETUP > VOICE BACKUP): the 32 user voices to the host and back */
enum { BK_IDLE, BK_ARMED, BK_SEND, BK_SEND_END, BK_WAIT_ACK, BK_WANT_INFO, BK_WAIT_INFO, BK_WANT_PART, BK_WAIT_PART,
       BK_CONFIRM };
#define BK_WAIT_MS 2000u                 /* an answer's patience */
#define BK_TRIES 3                       /* a request asked this often before NO HOST */
static struct {
    uint8_t st, k, tries;
    uint32_t crc, at, used;              /* the running CRC; when the last request went; the restore's used voices */
    r4p_bksum_t info;                    /* the host's BK_INFO */
} bk;
/* the restore's 32 voices wait in the DX7 import's buffer (4096 bytes; only Synth mode's MIDI in feeds that one, and a
 * dump arriving meanwhile shows as a bad checksum) */
#define BK_STAGE syn_imp.buf

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
    memset(&setup, 0, sizeof setup);
    setup.ver = SETUP_VER; setup.vel = VEL_NORMAL; setup.at = AT_OFF; setup.led = 2; setup.chan = 0; setup.mode = MODE_RUN4;
    setup.local = 0;
    setup.ml_def = 1;
}
static void setup_apply(void)
{
    touch_curve(VEL_CURVE[setup.vel]);
}
static void setup_load(void)                           /* layout 2, or layout 1 (6 bytes: the rest at its defaults) */
{
    setup_t t;
    int n;
    uint32_t i;
    setup_defaults();
    n = plat_store_load(OBJ_SETUP, &t, sizeof t);
    if (n == (int)SETUP_V1_SIZE && t.ver == 1u) {
        memcpy((uint8_t *)&t + SETUP_V1_SIZE, (const uint8_t *)&setup + SETUP_V1_SIZE, sizeof t - SETUP_V1_SIZE);
        t.ver = SETUP_VER;
        n = (int)sizeof t;
    }
    if (n == (int)sizeof t && t.ver == SETUP_VER && t.vel < VEL_N && t.at < AT_N && t.led <= 2 && t.chan < 16 &&
        t.mode <= MODE_SYNTH) {
        t.local = t.local > 1u ? 0u : t.local;
        t.ml_def = t.ml_def > 1u ? 1u : t.ml_def;
        for (i = 0; i < ML_N; i++)                      /* a binding out of its ranges is dropped */
            if (t.ml[i].cc && (!syn_ml_safe(t.ml[i].cc) || (t.ml[i].tgt >> 4) >= SP_N || (t.ml[i].opch & 7u) > 5u ||
                               (t.ml[i].opch >> 3) < 1u || (t.ml[i].opch >> 3) > 16u))
                memset(&t.ml[i], 0, sizeof t.ml[i]);
        setup = t;
    }
    setup_apply();
}
static uint32_t bk_user_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 2u * SYN_BANK_N; i++)
        n += (uint32_t)syn_patch_used(SYN_NFACTORY + i);
    return n;
}
static void setup_fill_backup(r4p_page_t *p)         /* SETUP's second sub-page: VOICE BACKUP */
{
    char t[24], u[12];
    uint32_t n = bk_user_count();
    strcpy(p->title, "VOICE BACKUP");
    strcpy(p->slot[0].name, "BACKUP"); p->slot[0].kind = R4P_KIND_TOGGLE;
    p->slot[0].value = bk.st == BK_ARMED || (bk.st >= BK_SEND && bk.st <= BK_WAIT_ACK);
    strcpy(p->slot[0].text, bk.st == BK_ARMED ? "SAVE: SEND" : bk.st >= BK_SEND && bk.st <= BK_WAIT_ACK ? "SENDING" : "SEND");
    strcpy(p->slot[1].name, "RESTORE"); p->slot[1].kind = R4P_KIND_TOGGLE;
    p->slot[1].value = bk.st >= BK_WANT_INFO;
    if (bk.st == BK_CONFIRM) {
        strcpy(p->slot[1].text, "SAVE: WRITE");
    } else if (bk.st >= BK_WANT_INFO) {
        u_int(bk.k, u); strcpy(t, u); u_cat(t, "/32");
        strcpy(p->slot[1].text, t);
    } else {
        strcpy(p->slot[1].text, "LOAD");
    }
    strcpy(p->slot[2].name, "USER VOICES"); p->slot[2].kind = R4P_KIND_UNI;
    p->slot[2].value = (uint16_t)(n * R4P_V14_MAX / 32u);
    u_int(n, u); strcpy(t, u); u_cat(t, "/32");
    strcpy(p->slot[2].text, t);
}
static uint32_t shed_level(void);
static void setup_fill_audio(r4p_page_t *p)          /* SETUP's third sub-page: AUDIO */
{
    char t[16], u[12];
    strcpy(p->title, "AUDIO");
    strcpy(p->slot[0].name, "LOCAL OUT"); p->slot[0].kind = R4P_KIND_ENUM; p->slot[0].value = (uint16_t)(2 << 7 | setup.local);
    strcpy(p->slot[0].text, setup.local ? "USB ONLY" : "ON");
    strcpy(p->slot[1].name, "USB CHANNELS"); p->slot[1].kind = R4P_KIND_UNI;
    p->slot[1].value = (uint16_t)(FM1B_UAC_CH * R4P_V14_MAX / 10u);
    u_int(FM1B_UAC_CH, u); strcpy(t, u); u_cat(t, FM1B_UAC_CH > 2 ? " STEMS" : " STEREO");
    strcpy(p->slot[1].text, t);
    strcpy(p->slot[2].name, "LOAD SHED"); p->slot[2].kind = R4P_KIND_UNI;
    p->slot[2].value = (uint16_t)(shed_level() * R4P_V14_MAX / 4u);
    if (shed_level()) { strcpy(t, "STEP "); u_int(shed_level(), u); u_cat(t, u); } else strcpy(t, "NONE");
    strcpy(p->slot[2].text, t);
}
static void setup_fill_learn(r4p_page_t *p);
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
    if (mode == MODE_SYNTH) { p->r = SY_AR; p->g = SY_AG; p->b = SY_AB; }
    p->page_count = 4;
    p->page_idx = ctl.setup_sub;
    if (ctl.setup_sub == 1) {
        setup_fill_backup(p);
        return;
    }
    if (ctl.setup_sub == 2) {
        setup_fill_audio(p);
        return;
    }
    if (ctl.setup_sub == 3) {
        setup_fill_learn(p);
        return;
    }
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
static void ml_clear_disarm(void);
static void setup_owed_frame(void);
static void setup_open(int on)
{
    if (on == ui.setup)
        return;
    ui.setup = (uint8_t)on;
    if (bk.st == BK_ARMED || bk.st == BK_CONFIRM)    /* an armed BACKUP or a restore not yet written is forgotten */
        bk.st = BK_IDLE;
    ml_clear_disarm();
    if (on) {
        ctl.setup_sub = 0;
        setup_fill();
        ctl.setup_dirty = 0;
        memset(ctl.setup_acc, 0, sizeof ctl.setup_acc);
    } else if (ctl.setup_dirty && !bk_hold) {
        plat_store_save(OBJ_SETUP, &setup, sizeof setup);
        ctl.setup_dirty = ctl.setup_owed = 0;
    } else if (ctl.setup_dirty) {
        ctl.setup_owed = 1;                            /* (a backup streams: saved when it is over) */
    }
    ui.dirty = D_ALL;
}
static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }
static void setup_refresh(void)                        /* the backup's state changed: its tiles */
{
    if (ui.setup && ctl.setup_sub) {
        setup_fill();
        ui.dirty |= D_TILES;
    }
}
static int bk_host(void);
static void bk_restore_start(void);
static void setup_learn_turn(int which, int d);
static void local_out_apply(void);
static void setup_turn(int which, int d)               /* K1..K4: one step per detent */
{
    if (ctl.setup_sub == 2) {                           /* AUDIO: K1 LOCAL OUT (the others say how things are) */
        if (which == 0) {
            setup.local = (uint8_t)(d > 0);
            ctl.setup_dirty = 1;
            local_out_apply();
        }
        setup_fill();
        ui.dirty |= D_TILES;
        return;
    }
    if (ctl.setup_sub == 3) {                           /* MIDI LEARN */
        setup_learn_turn(which, d);
        setup_fill();
        ui.dirty |= D_TILES;
        return;
    }
    if (ctl.setup_sub) {                                /* VOICE BACKUP: K1 arms BACKUP (SAVE sends), K2 fetches */
        if (which == 0 && (bk.st == BK_IDLE || bk.st == BK_CONFIRM)) {
            if (bk_host()) {
                bk.st = BK_ARMED;
                ui_toast("SAVE: SEND BACKUP", 15);
            }
        } else if (which == 1 && (bk.st == BK_IDLE || bk.st == BK_ARMED)) {
            bk_restore_start();
        }
        setup_refresh();
        return;
    }
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

/* -------------------------------------------------------- protocol 1.3 --- */
static int host13(void) { return ctl.session && ctl.host_minor >= 3; }   /* a host that takes the 1.3 frames */
static void hv_name(char *t)                           /* the host voice's name, its spaces at the end cut */
{
    const uint8_t *nm = hv_set()->cur + FP_NAME;
    int i;
    for (i = 0; i < R4P_NAME_MAX; i++)
        t[i] = nm[i] < 32u || nm[i] > 126u ? ' ' : (char)nm[i];
    for (t[i] = 0; i > 0 && t[i - 1] == ' '; i--)
        t[i - 1] = 0;
}
static void hv_off_notes(void)                         /* its notes released (RUN4 mode: it is the one sounding) */
{
    if (mode == MODE_RUN4)
        syn_event(3, 0, 0, 0);
}
static void hv_voice(const r4p_hvvoice_t *v)           /* HV_VOICE: a stored voice, or the host's own */
{
    syn_set_t *s = hv_set();
    if (v->kind == R4P_HVV_STORED) {
        const uint8_t *p = syn_packed(v->idx);
        if (!p)                                        /* an empty user slot: nothing changes (HV_STATE says so) */
            return;
        hv_off_notes();
        fp_unpack(p, s->cur);
        s->patch_idx = v->idx;
        hv.voice = v->idx;
    } else {
        hv_off_notes();
        if (v->kind == R4P_HVV_PACKED) {
            fp_unpack(v->data, s->cur);                /* (masks and clamps every field) */
        } else {
            memcpy(s->cur, v->data, FP_SIZE);
            fp_sanitize(s->cur);
        }
        hv.voice = R4P_VOICE_HV;
    }
    if (s == &syn)
        syn_dirty = 1;
}
static void hv_param(uint8_t id, uint8_t v)            /* HV_PARAM: one parameter (the codec checked the range) */
{
    syn_set_t *s = hv_set();
    switch (id) {
    case R4P_HVP_ALG: s->cur[FP_ALG] = v; break;
    case R4P_HVP_FB: s->cur[FP_FB] = v; break;
    case R4P_HVP_BRIGHT: s->bright = (int8_t)(v - 64); break;
    case R4P_HVP_ENVSPD: s->env_spd = (int8_t)(v - 64); break;
    case R4P_HVP_VOLUME: s->level = v; break;
    case R4P_HVP_VIBRATO: s->cur[FP_LPMD] = v; break;
    case R4P_HVP_TREMOLO: s->cur[FP_LAMD] = v; break;
    case R4P_HVP_LFOSPD: s->cur[FP_LFS] = v; break;
    case R4P_HVP_ATDEPTH: s->at_depth = v; break;
    case R4P_HVP_BEND: s->bend = v; break;
    case R4P_HVP_VELSENS: s->vel_sens = v; break;
    case R4P_HVP_TRANSPOSE: s->transpose = (int8_t)(v - 24); break;
    case R4P_HVP_TUNE: s->tune = (int8_t)(v - 50); break;
    case R4P_HVP_CHORUS: s->chorus = v; break;
    case R4P_HVP_DELAY: s->dly_mix = v; break;
    case R4P_HVP_REVERB: s->rev_mix = v; break;
    case R4P_HVP_DRIVE: s->drive = v; break;
    case R4P_HVP_DLYTIME: s->dly_div = v; break;
    case R4P_HVP_MORPHSET:                             /* (firmware 0.6) the morph: A, B from the values now; 2 clears */
        if (v < 2u) syn_morph_take(s, v); else s->mo_cap = 0;
        break;
    case R4P_HVP_MORPHMODE: s->mo_mode = v; break;
    case R4P_HVP_MORPH:
        s->mo_pos = v;
        if (s == &syn && s->mo_mode >= MO_DRIFT) syn_mo.rephase = 1;
        break;
    default: break;
    }
    if (s == &syn)
        syn_dirty = 1;
}
static void hv_link_lost(void)                         /* a new computer (or none): the host voice is off */
{
    if (hv.on)
        hv_off_notes();
    hv.on = 0;
    ctl.want_hvstate = 0;
    ctl.names_next = 255;
    ctl.vget1 = 0;
    if (bk.st != BK_IDLE && bk.st != BK_CONFIRM)
        bk.st = BK_IDLE;
}

static int bk_host(void)                               /* a 1.3 host to talk to, else the toast */
{
    if (ui.connected && host13())
        return 1;
    ui_toast("NO HOST", 15);
    return 0;
}
static void bk_done(const char *toast)
{
    bk.st = BK_IDLE;
    if (toast)
        ui_toast(toast, 20);
    setup_refresh();
}
static void bk_restore_start(void)
{
    if (!bk_host())
        return;
    bk.st = BK_WANT_INFO;
    bk.k = 0;
    bk.tries = 0;
    ui_toast("LOADING BACKUP", 15);
}
static void bk_part(uint32_t k, r4p_voice_t *p)        /* user voice k (0..31) as a backup part */
{
    const syn_bank_t *b = &syn_bank[k / SYN_BANK_N];
    p->idx = (uint8_t)k;
    p->used = b->magic == SYN_BANK_MAGIC && ((b->used >> (k % SYN_BANK_N)) & 1u);
    if (p->used)
        memcpy(p->data, b->v[k % SYN_BANK_N], R4P_PACKED);
    else
        memset(p->data, 0, R4P_PACKED);
}
static void bk_write(void)                             /* the confirmed restore into both banks and the store */
{
    uint32_t b, i;
    char t[24] = "RESTORED ", u[12];
    for (b = 0; b < 2u; b++) {
        syn_bank[b].magic = SYN_BANK_MAGIC;
        syn_bank[b].used = (uint16_t)(bk.used >> (16u * b));
        for (i = 0; i < SYN_BANK_N; i++)
            memcpy(syn_bank[b].v[i], BK_STAGE + (b * SYN_BANK_N + i) * R4P_PACKED, R4P_PACKED);
        if (!bk_hold)
            plat_store_save(OBJ_BANK_A + b, &syn_bank[b], sizeof syn_bank[b]);
    }
    syn_bank_dirty = bk_hold ? 3u : 0u;                /* (a USB backup streams: saved after it) */
    u_int(bk_user_count(), u); u_cat(t, u); u_cat(t, " VOICES");
    bk_done(t);
}
/* the backup's host frames (app_sysex) */
static void bk_frame(const r4p_msg_t *m)
{
    uint32_t i, n;
    char t[24], u[12];
    switch (m->cmd) {
    case R4P_BK_ACK:
        if (bk.st == BK_WAIT_ACK)
            bk_done(m->u.b1.v == R4P_BK_STORED ? "BACKUP SENT" : m->u.b1.v == R4P_BK_BAD ? "BAD CHECKSUM" : "BACKUP NOT STORED");
        break;
    case R4P_BK_INFO:
        if (bk.st != BK_WAIT_INFO)
            break;
        if (!m->u.bksum.count) {
            bk_done("NO BACKUP");
        } else if (m->u.bksum.count != R4P_BK_VOICES) {
            bk_done("BAD BACKUP");
        } else {
            bk.info = m->u.bksum;
            bk.k = 0;
            bk.used = 0;
            bk.crc = 0;
            bk.tries = 0;
            bk.st = BK_WANT_PART;
            setup_refresh();
        }
        break;
    case R4P_BK_DATA:
        if (bk.st != BK_WAIT_PART || m->u.voice.idx != bk.k)
            break;                                     /* (not the part asked for: a late answer) */
        memcpy(BK_STAGE + bk.k * R4P_PACKED, m->u.voice.data, R4P_PACKED);
        if (m->u.voice.used)
            bk.used |= 1u << bk.k;
        bk.k++;
        bk.tries = 0;
        if (bk.k < R4P_BK_VOICES) {
            bk.st = BK_WANT_PART;
            setup_refresh();
            break;
        }
        for (i = 0, bk.crc = 0; i < R4P_BK_VOICES; i++) {      /* the CRC over what is staged, as the host made it */
            r4p_voice_t p;
            p.idx = (uint8_t)i;
            p.used = (uint8_t)((bk.used >> i) & 1u);
            memcpy(p.data, BK_STAGE + i * R4P_PACKED, R4P_PACKED);
            bk.crc = r4p_crc32_part(bk.crc, &p);
        }
        if (bk.crc != bk.info.crc) {
            bk_done("BAD CHECKSUM");
            break;
        }
        for (i = 0, n = 0; i < R4P_BK_VOICES; i++)
            n += (bk.used >> i) & 1u;
        bk.st = BK_CONFIRM;
        strcpy(t, "SAVE: WRITE "); u_int(n, u); u_cat(t, u);
        ui_toast(t, 30);
        setup_refresh();
        break;
    default:
        break;
    }
}
/* what the host is owed, behind the buttons, as there is room (one frame at most a call: the others' turn too) */
static void jobs_send(uint32_t now)
{
    static uint8_t f[200];
    r4p_msg_t m;
    if (!ctl.session || ctl.txq_n)
        return;
    if ((bk.st == BK_WAIT_ACK || bk.st == BK_WAIT_INFO || bk.st == BK_WAIT_PART) && now - bk.at >= BK_WAIT_MS) {
        if (bk.st == BK_WAIT_ACK || ++bk.tries >= BK_TRIES)
            bk_done("NO HOST");
        else
            bk.st = bk.st == BK_WAIT_INFO ? BK_WANT_INFO : BK_WANT_PART;       /* asked again */
    }
    if (!host13())
        return;
    if (ctl.want_hvstate) {
        m.cmd = R4P_HV_STATE;
        m.u.hvstate.state = !hv.on ? R4P_HV_OFF : mode == MODE_RUN4 ? R4P_HV_SOUNDING : R4P_HV_SUSPENDED;
        m.u.hvstate.chan = hv.chan;
        m.u.hvstate.voice = hv.voice;
        hv_name(m.u.hvstate.name);
        if (send_frame(f, r4p_encode(f, sizeof f, &m)))
            ctl.want_hvstate = 0;
        return;
    }
    if (ctl.vget1) {
        uint32_t idx = ctl.vget1 - 1u;
        const uint8_t *p = idx == R4P_VOICE_HV ? 0 : syn_packed(idx);
        m.cmd = R4P_VOICE_DATA;
        m.u.voice.idx = (uint8_t)idx;
        m.u.voice.used = idx == R4P_VOICE_HV || p != 0;
        if (idx == R4P_VOICE_HV)
            fp_pack(hv_set()->cur, m.u.voice.data);   /* the host voice as it sounds, its live edits in it */
        else if (p)
            memcpy(m.u.voice.data, p, R4P_PACKED);
        if (send_frame(f, r4p_encode(f, sizeof f, &m)))
            ctl.vget1 = 0;
        return;
    }
    if (ctl.names_next < R4P_NVOICES) {
        uint32_t i;
        m.cmd = R4P_VOICE_NAMES;
        m.u.names.first = ctl.names_next;
        m.u.names.total = R4P_NVOICES;
        m.u.names.n = R4P_NAMES_ROWS;
        for (i = 0; i < R4P_NAMES_ROWS; i++) {
            uint32_t idx = ctl.names_next + i, k;
            m.u.names.used[i] = (uint8_t)syn_patch_used(idx);
            if (m.u.names.used[i]) {
                syn_patch_name(idx, m.u.names.name[i]);
                for (k = R4P_NAME_MAX; k > 0 && m.u.names.name[i][k - 1] == ' '; k--)
                    m.u.names.name[i][k - 1] = 0;
            } else {
                m.u.names.name[i][0] = 0;
            }
        }
        if (send_frame(f, r4p_encode(f, sizeof f, &m)))
            ctl.names_next = (uint8_t)(ctl.names_next + R4P_NAMES_ROWS);
        return;
    }
    switch (bk.st) {
    case BK_SEND: {
        uint32_t c;
        m.cmd = R4P_BK_PART;
        bk_part(bk.k, &m.u.voice);
        c = r4p_crc32_part(bk.crc, &m.u.voice);
        if (send_frame(f, r4p_encode(f, sizeof f, &m))) {
            bk.crc = c;
            if (++bk.k >= R4P_BK_VOICES)
                bk.st = BK_SEND_END;
        }
        break;
    }
    case BK_SEND_END:
        m.cmd = R4P_BK_END;
        m.u.bksum.count = R4P_BK_VOICES;
        m.u.bksum.crc = bk.crc;
        if (send_frame(f, r4p_encode(f, sizeof f, &m))) {
            bk.st = BK_WAIT_ACK;
            bk.at = now;
        }
        break;
    case BK_WANT_INFO: case BK_WANT_PART:
        m.cmd = R4P_BK_GET;
        m.u.b1.v = bk.st == BK_WANT_INFO ? R4P_BK_SUMMARY : bk.k;
        if (send_frame(f, r4p_encode(f, sizeof f, &m))) {
            bk.st = bk.st == BK_WANT_INFO ? BK_WAIT_INFO : BK_WAIT_PART;
            bk.at = now;
        }
        break;
    default:
        break;
    }
}
static void setup_learn_save(void);
static void setup_save_press(void)                     /* SAVE on the SETUP page: VOICE BACKUP's armed action */
{
    if (ctl.setup_sub == 3) {                           /* MIDI LEARN: an armed CLEAR */
        setup_learn_save();
        return;
    }
    if (ctl.setup_sub != 1)
        return;
    if (bk.st == BK_ARMED) {
        if (!bk_host()) {
            bk.st = BK_IDLE;
        } else {
            bk.st = BK_SEND;
            bk.k = 0;
            bk.crc = 0;
            ui_toast("SENDING BACKUP", 15);
        }
    } else if (bk.st == BK_CONFIRM) {
        bk_write();
    }
    setup_refresh();
}
static void setup_enc(uint32_t k, int d)               /* the SETUP page's encoders: K1..K4 its tiles, SELECT / PRESETS its sub-page */
{
    if (k >= 3) {
        for (; d; d += d > 0 ? -1 : 1)
            setup_turn((int)k - 3, d > 0 ? 1 : -1);
    } else if ((k == 0 || k == 2) && d) {
        ctl.setup_sub = (uint8_t)((ctl.setup_sub + (d > 0 ? 1u : 3u)) % 4u);   /* four sub-pages, round */
        if (bk.st == BK_ARMED)
            bk.st = BK_IDLE;
        setup_fill();
        ui.dirty |= D_HEAD | D_TILES | D_FOOT;
    }
}

/* ------------------------------------------------------------ MIDI learn --- */
#include "synth_learn.c"
static void setup_fill_learn(r4p_page_t *p)          /* SETUP's fourth sub-page: MIDI LEARN (the bindings one by one) */
{
    const ml_bind_t *b = &setup.ml[ml.slot % ML_N];
    char t[24], u[12];
    strcpy(p->title, "MIDI LEARN");
    strcpy(p->slot[0].name, "BINDING"); p->slot[0].kind = R4P_KIND_UNI;
    p->slot[0].value = (uint16_t)(ml.slot * R4P_V14_MAX / (ML_N - 1u));
    u_int(ml.slot + 1u, u); strcpy(t, u); u_cat(t, "/16");
    strcpy(p->slot[0].text, t);
    if (b->cc) {
        strcpy(t, "CC "); u_int(b->cc, u); u_cat(t, u); u_cat(t, " CH "); u_int(b->opch >> 3, u); u_cat(t, u);
        strcpy(p->slot[1].name, t);
        ml_name(b->tgt, b->opch & 7u, p->slot[1].text);
    } else {
        strcpy(p->slot[1].name, "NO CC");
        strcpy(p->slot[1].text, "--");
    }
    p->slot[1].kind = R4P_KIND_UNI;
    p->slot[1].value = (uint16_t)(b->cc * R4P_V14_MAX / 127u);
    strcpy(p->slot[2].name, "DEFAULT CCS"); p->slot[2].kind = R4P_KIND_TOGGLE; p->slot[2].value = setup.ml_def;
    strcpy(p->slot[2].text, setup.ml_def ? "ON" : "OFF");
    strcpy(p->slot[3].name, "CLEAR"); p->slot[3].kind = R4P_KIND_TOGGLE; p->slot[3].value = ml.clear_arm;
    u_int(ml_count(), u); strcpy(t, ml.clear_arm ? "SURE?" : u); if (!ml.clear_arm) u_cat(t, " SET");
    strcpy(p->slot[3].text, t);
}
static void setup_learn_turn(int which, int d)
{
    if (which == 0) {
        int x = (int)ml.slot + d;
        ml.slot = (uint8_t)(x < 0 ? 0 : x >= ML_N ? ML_N - 1 : x);
        ml.clear_arm = 0;
    } else if (which == 2) {
        setup.ml_def = (uint8_t)(d > 0);
        ctl.setup_dirty = 1;
    } else if (which == 3) {
        ml.clear_arm = (uint8_t)(d > 0 && setup.ml[ml.slot % ML_N].cc);
        if (ml.clear_arm)
            ui_toast("SAVE: CLEAR BINDING", 15);
    }
}
static void ml_clear_disarm(void) { ml.clear_arm = 0; }
static void setup_learn_save(void)
{
    if (!ml.clear_arm)
        return;
    memset(&setup.ml[ml.slot % ML_N], 0, sizeof setup.ml[0]);
    ml.clear_arm = 0;
    ctl.setup_dirty = 1;
    ui_toast("BINDING CLEARED", 15);
    setup_fill();
    ui.dirty |= D_TILES;
}

static void octave_step(int d);
/* ------------------------------------------------------------- Synth mode --- */
static void synth_notes_off_keys(void)         /* held keys: the note-offs of the mode being left */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        if (ctl.sounding[k] != 255 && mode == MODE_RUN4 && plat_midi_out_room(TO_LINKS))
            plat_midi_send(0x08u | (0x80u | ctl.snd_ch[k]) << 8 | (uint32_t)ctl.sounding[k] << 16, TO_LINKS);
        else if (mode == MODE_SYNTH) {
            uint32_t j;
            for (j = 0; j < ctl.chord_n[k]; j++)
                syn_event(2, ctl.chord[k][j], 0, 0);
        }
        ctl.chord_n[k] = 0;
        ctl.sounding[k] = 255;
    }
}
static void mode_switch(uint8_t to)
{
    if (to == mode)
        return;
    synth_notes_off_keys();
    if (mode == MODE_SYNTH) {                      /* leaving: the sound stops, the settings are kept */
        syn_hold = 1;                              /* (silent from here to the swap) */
        sq_start(0);
        sq.rec = 0;
        sq.fill = 0;
        sq_ed.held = -1;
        sq_ed.entry_open = 0;
        if (sui.dirty_set || syn_bank_dirty || sq_dirty) {   /* (even under a backup's hold: the set is about to swap) */
            uint8_t h = sy_hold_store;
            sy_hold_store = 0;
            syn_save_all();
            sy_hold_store = h;
        }
    } else if (hv.on) {
        ctl.want_hvstate = 1;                      /* 1.3: the host voice is suspended while Synth mode plays */
    }
    syn_swap();                                    /* the other mode's settings sound (synth.c): Synth mode's, or the
                                                    * host voice's; nothing of the one rings on in the other */
    mode = to;
    setup.mode = to;
    if (bk_hold) ctl.setup_owed = 1; else plat_store_save(OBJ_SETUP, &setup, sizeof setup);
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
    int shift = (int)((ctl.btn >> B_SEL) & 1u);
    if (ml.arm && k != B_SEL) {                    /* learning: SAVE unlearns the tile, anything else leaves it */
        if (k == B_SAVE) {
            ml_unlearn_armed();
            return;
        }
        ml.arm = 0;
        sp_toast("LEARN OFF");
    }
    if (sui.page == SP_SEQ && sq_ed.held >= 0 && k != B_SAVE && k != B_PLAY && k != B_REC && k != B_OCTDN && k != B_OCTUP) {
        /* a step held on the grid: a page button picks which of its parameters K1..K4 lock (SEQ: the step's own) */
        if (k == B_HOME) sq_ed.lview = 1;
        else if (k == B_LFO) sq_ed.lview = 2;
        else if (k == B_FX) sq_ed.lview = sq_ed.lview == 3 ? 4 : 3;
        else if (k == B_SEQ) sq_ed.lview = sq_ed.lview == 0 ? SQ_LV_COND : 0;   /* SEQ: its own page, again: COND */
        else if (k == B_EDIT) sq_copy_held();       /* EDIT: COPY the held steps; GLO: PASTE from the held step */
        else if (k == B_GLO) { sq_paste_held(); return; }
        else if (k != B_SEL) sp_toast("NO LOCKS ON THAT PAGE");
        sq_ed.held_used = 1;
        sp_build();
        return;
    }
    if (shift && k == B_SEQ) {                     /* SEL + SEQ held: FILL (trig conditions FILL / NOT FILL) */
        sq.fill = 1;
        sp_toast("FILL");
        if (sui.page == SP_SEQ)
            ui.dirty |= D_TILE0;
        return;
    }
    if (shift && k == B_EDIT) {                    /* SEL + EDIT: MUTATE the voice; SEL + SAVE: undo / redo it */
        sp_mutate();
        return;
    }
    if (shift && k == B_SAVE) {                    /* (on the SEQ page: the last pattern change, if there is one) */
        if (sui.page == SP_SEQ && sgu.valid)
            sq_undo_press();
        else
            sp_mutate_undo();
        return;
    }
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
    return sui.page == SP_SEQ && sui.sub != 3u && !sq.rec && !ui.setup;      /* (SONG: the keys play) */
}
/* a key's notes in Synth mode: the scale (SNAP / IN KEY) and the chord (synth.c syn_key_note, syn_chord); 0 = silent */
static uint32_t synth_key_notes(uint32_t k, uint8_t *out)
{
    int32_t base = syn_key_note(53 + (int32_t)k + 12 * ctl.octave, key_white(k), ctl.octave);
    if (base < 0 || base > 127)
        return 0;
    return syn_chord(base, syn.chord, syn.inv, syn.spread, syn.scale, syn.root, out);
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
            uint8_t nt[SQ_NOTES];
            uint32_t j, n;
            ctl.keys |= bit;
            ctl.sounding[k] = 255;
            ctl.chord_n[k] = 0;
            if (shift && synth_grid() && sq_ed.held >= 0 && key_white(k) >= 0) {   /* SEL + another step: held with it */
                uint32_t i = sq_ed.view * SQ_PAGE + (uint32_t)key_white(k);
                if (i < sq_ed_pat()->len && i != (uint32_t)sq_ed.held) {
                    sq_ed.more |= 1ull << i;
                    ctl.more_key[k] = (uint8_t)i;
                }
                sp_build();
                continue;
            }
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
                if (j < 6u)
                    continue;
            }
            n = synth_key_notes(k, nt);                         /* the scale and the chord */
            if (!n)
                continue;
            memcpy(ctl.chord[k], nt, n);
            ctl.chord_n[k] = (uint8_t)n;
            ctl.sounding[k] = nt[0];
            ctl.notes_on++;
            if (synth_grid() && sq_ed.held >= 0) {              /* a step held: the key is its note (a chord with more) */
                for (j = 0; j < n; j++) {
                    sq_ed_note(nt[j]);
                    syn_event(1, nt[j], key_velocity(k), 1);     /* (heard, not recorded, no arp) */
                }
                sui.edit_at = plat_ms();
                sp_build();
                ctl.snd_ch[k] = 1;
                continue;
            }
            if (synth_step_rec()) {                               /* step recording: the keys down together, one step */
                for (j = 0; j < n; j++) {
                    sq_ed_entry(nt[j], key_velocity(k));
                    syn_event(1, nt[j], key_velocity(k), 1);
                }
                sui.edit_at = plat_ms();
                sp_build();
                ctl.snd_ch[k] = 1;
                continue;
            }
            for (j = 0; j < n; j++)
                syn_event(1, nt[j], key_velocity(k), 0);
            ctl.snd_ch[k] = 0;                                    /* (Synth mode: the event's source, for its note-off) */
        } else {
            uint32_t j;
            ctl.keys &= ~bit;
            if (sq_ed.held >= 0 && sq_ed.held_key == k) {       /* the held step let go: a tap toggles it */
                sq_ed_step_up();
                memset(ctl.more_key, 255, sizeof ctl.more_key);
                sui.edit_at = plat_ms();
                sp_build();
            } else if (ctl.more_key[k] != 255) {                /* a step held with it let go */
                sq_ed.more &= ~(1ull << ctl.more_key[k]);
                ctl.more_key[k] = 255;
                sp_build();
            }
            if (ctl.chord_n[k]) {
                ctl.notes_off++;
                for (j = 0; j < ctl.chord_n[k]; j++)
                    syn_event(2, ctl.chord[k][j], 0, ctl.snd_ch[k]);
            }
            ctl.chord_n[k] = 0;
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
            setup_enc(k, d);
            continue;
        }
        if (k >= 3 && (ctl.btn >> B_SEL & 1u) && sui.page != SP_SEQ) {
            ml_arm_tile(k - 3u);                         /* SEL + K1..K4: MIDI learn for that tile */
        } else if (k >= 3) {
            sp_turn(k - 3u, d);
        } else if (k == 0 && sui.page == SP_SEQ) {       /* SELECT on the SEQ page: the step page */
            int v = (int)sq_ed.view + (d > 0 ? 1 : -1);
            uint32_t pages = (sq_ed_pat()->len + SQ_PAGE - 1u) / SQ_PAGE;
            sq_ed.view = (uint8_t)(v < 0 ? 0 : v >= (int)pages ? (int)pages - 1 : v);
            sq_ed.follow = 0;
            ui.dirty |= D_TILE0;
        } else if (k == 0) {                             /* SELECT: the operator */
            sp_select_op((uint32_t)((int)sui.op + (d > 0 ? 1 : -1) < 0 ? 0 : (int)sui.op + (d > 0 ? 1 : -1)));
        } else if (k == 1 && sui.page == SP_SEQ && sq_ed.held >= 0) {   /* ALGORITHM on a held step: its lock */
            sq_lock_alg(d);
            sq_build();
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
    ml_frame(now);
    sq_frame(now);
    sp_live(now);
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
    } else if (syn.scale % SCL_N != SCL_CHROM && !synth_step_rec()) {   /* a scale: its notes dim (IN KEY: the roots) */
        uint32_t cnt = 0, m = SCL_MASK[syn.scale % SCL_N];
        for (i = 0; i < 12u; i++)
            cnt += (m >> i) & 1u;
        for (i = 0; i < NKEYS; i++) {
            int w = key_white(i);
            if (syn.keymode == KM_INKEY ? (w >= 0 && (uint32_t)w % cnt == 0u)
                                         : syn_in_scale(53 + (int32_t)i + 12 * ctl.octave, syn.scale, syn.root))
                *dk |= 1u << i;
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
#ifdef PLAT_HOST
/* the simulator's "midiat FRAME B0 B1 B2": a message as if it had landed at that frame of the last block (the host's
 * plat_midi_in_t24 is when it was read: no stamps to test with) */
static struct { uint32_t pkt; uint16_t frame; } hq[16];
static uint8_t hq_n;
#endif
static uint32_t midi_frame;                            /* the frame in this block of the message being taken */
static void midi_pump(uint32_t n)
{
    uint32_t pkt;
    int port;
#if FM1B_MIDI_STAMP && !defined(PLAT_HOST)
    /* midi_stamp: USB's and the jack's messages carry when they landed; what landed during the last block plays at the
     * same point of this one (synth.c syn_timed). Bluetooth's are stamped when read: at once, as before */
    static uint32_t last_t24;
    static uint8_t have_last;
    uint32_t bt = plat_ticks24(), span = bt - last_t24;
    int placed = have_last && span > 0u && span < 20000u * PLAT_TICKS_PER_US;
    last_t24 = bt;
    have_last = 1;
#endif
#ifdef PLAT_HOST
    uint32_t hi = 0;
#endif
    for (;;) {
        uint32_t cin, st, d1, d2, ch;
#ifdef PLAT_HOST
        if (hi < hq_n) {
            pkt = hq[hi].pkt;
            port = PORT_USB;
            midi_frame = hq[hi].frame < n ? hq[hi].frame : n - 1u;
            if (++hi == hq_n)
                hq_n = 0;
        } else
#endif
        if (plat_midi_in(&pkt, &port)) {
            midi_frame = 0;
#if FM1B_MIDI_STAMP && !defined(PLAT_HOST)
            if (placed && port != PORT_BLE)
                midi_frame = syn_midi_frame(plat_midi_in_t24(), bt - span, span, n);
#endif
        } else {
            break;
        }
        cin = pkt & 0xFu; st = (pkt >> 8) & 0xFFu; d1 = (pkt >> 16) & 0x7Fu; d2 = (pkt >> 24) & 0x7Fu; ch = st & 0x0Fu;
        if (cin >= 4u && cin <= 7u) {                  /* SysEx packets: 4 = 3 bytes, more to come; 5, 6, 7 = the last 1, 2, 3 */
            uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u, i;
            if (port == PORT_BLE)
                for (i = 0; i < n; i++)
                    rx_byte((uint8_t)(pkt >> (8 + 8 * i)));
            continue;
        }
        if (mode == MODE_SYNTH && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
            syn_timed(&(syn_ev_t){11, (uint8_t)st, 0, 0}, midi_frame);   /* MIDI clock, start, stop: the sequencer and the arp follow */
            continue;
        }
        {   /* 1.3: in RUN4 mode the host voice plays its channel from the host's links (not the jack) */
            int host_voice = mode == MODE_RUN4 && hv.on && (port == PORT_USB || port == PORT_BLE);
            if ((mode != MODE_SYNTH && !host_voice) || st >= 0xF0u)
                continue;
            if (host_voice ? ch != hv.chan : (syn.chan && ch != (uint32_t)syn.chan - 1u))
                continue;
            if (!host_voice && ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u || (st & 0xF0u) == 0xA0u)) {
                /* Synth mode, MIDI IN = SCALE: the notes snapped too (each note-off finds what its note-on played) */
                static uint8_t snapped[128];
                if ((st & 0xF0u) == 0x90u && d2)
                    snapped[d1] = (uint8_t)(syn.midi_scale ? syn_snap((int32_t)d1, syn.scale, syn.root) : (int32_t)d1);
                else if (!snapped[d1] && d1)
                    snapped[d1] = (uint8_t)d1;
                d1 = snapped[d1];
            }
        }
        switch (st & 0xF0u) {                          /* (each at its frame in the block: syn_timed) */
        case 0x90u:
            if (d2) { syn_timed(&(syn_ev_t){1, (uint8_t)d1, (uint8_t)d2, 0}, midi_frame); break; }
            /* fallthrough: velocity 0 is a note-off */
        case 0x80u:
            syn_timed(&(syn_ev_t){2, (uint8_t)d1, 0, 0}, midi_frame);
            break;
        case 0xB0u:
            if (d1 == 64u) syn_timed(&(syn_ev_t){5, (uint8_t)d2, 0, 0}, midi_frame);
            else if (d1 == 123u || d1 == 120u) syn_timed(&(syn_ev_t){3, 0, 0, 0}, midi_frame);
            else if (mode == MODE_SYNTH) ml_cc_in(ch, d1, d2);   /* MIDI learn and the default CCs (main loop) */
            break;
        case 0xE0u:
            syn_timed(&(syn_ev_t){4, (uint8_t)d1, (uint8_t)d2, 0}, midi_frame);
            break;
        case 0xA0u:                                    /* pressure: into BRIGHT by at_depth (the host voice's; 0 in Synth mode) */
            syn_timed(&(syn_ev_t){9, (uint8_t)d1, (uint8_t)d2, 0}, midi_frame);
            break;
        case 0xD0u:
            syn_timed(&(syn_ev_t){10, (uint8_t)d1, 0, 0}, midi_frame);
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
        ctl.host_minor = 0;
        hv_link_lost();
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
                else if (k == B_SAVE)
                    setup_save_press();
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
            if (!(btn & bit) && k == B_SEQ && sq.fill) {   /* FILL ends with SEQ's release */
                sq.fill = 0;
                if (sui.page == SP_SEQ)
                    ui.dirty |= D_TILE0;
            }
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
    if (!ctl.want_sync)
        jobs_send(now);                                /* 1.3: answers owed, the backup (both modes) */

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
        if (ui.setup) {                                /* SETUP: K1..K4 edit, SELECT / PRESETS the sub-page; none of it goes out */
            setup_enc(k, d);
            continue;
        }
        if (d)
            r4p_enc_add(&ctl.enc, (uint8_t)k, d);
        if (d && k >= 3)
            ui_touch(k - 3u);                          /* 1.2: frame the tile; its VALUE shows it big */
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

/* ------------------------------------------------------- local out, load --- */
/* LOCAL OUT: USB ONLY (SETUP > AUDIO), while a computer is connected over USB: the render goes to the USB audio input
 * at full level (fm1-base's USB AUDIO full: plat_usb_audio_full) and the speaker and headphones get nothing
 * (APP_GAIN 0 after the recording's tap), so RUN4 records the FM-1 without it being heard twice. fm1-base mixes the
 * computer's own audio in before that gain, so it is muted on the FM-1 too: listen on the iPad. */
static volatile uint8_t local_off;
uint32_t app_gain(uint32_t m) { return local_off ? 0u : m * m >> 12; }
static void local_out_apply(void)
{
    uint8_t off = (uint8_t)(setup.local && plat_usb_session() != 0);
    if (off != local_off) {
        local_off = off;
        plat_usb_audio_full(off);
    }
}
static uint32_t shed_level(void) { return syn_shed; }
/* the overload guard (synth.c syn_shed_*): fm1-base calls this from the audio interrupt when a render took over 90 %
 * of its time; the render sheds a step at its next block. Says so once (a toast) until it is all back */
void app_overload(void)
{
    ctl.overloads++;
    syn_shed_overload();
}
static void shed_frame(void)
{
    if (syn_shed && !ctl.shed_told) {
        ctl.shed_told = 1;
        ui_toast("CPU HIGH: SOUND LIGHTER", 20);
    } else if (!syn_shed) {
        ctl.shed_told = 0;
    }
    if (ui.setup && ctl.setup_sub == 2 && ctl.shed_shown != syn_shed) {   /* (the AUDIO page shows the step) */
        ctl.shed_shown = syn_shed;
        setup_fill();
        ui.dirty |= D_TILES;
    }
}
/* the SETUP object changed outside its page (a MIDI learn): saved when no sequence plays (a flash write stops the sound
 * for a moment) and no backup holds the store */
static void setup_owed_frame(void)
{
    if (ctl.setup_owed && !ui.setup && !sq.run && !bk_hold) {
        ctl.setup_owed = 0;
        ctl.setup_dirty = 0;
        plat_store_save(OBJ_SETUP, &setup, sizeof setup);
    }
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
        ctl.want_hvstate = hv.on;                      /* 1.3: where its voice stands (sent to a 1.3 host only) */
        if (mode == MODE_SYNTH) {                      /* the host knows we are a synth: nothing to draw or sync */
            ui.connected = 1;
            ctl.host_minor = m.u.ack.minor;
            break;
        }
        ui_connect(1);
        ctl.leds = (r4p_leds_t){{0}, {0}};
        ctl.host_minor = m.u.ack.minor;
        ui.v12 = m.u.ack.minor >= 2;                   /* protocol 1.2: the big value view (ui.c) */
        ctl.pot_sent = -1;
        ctl.want_sync = 1;
        ctl.want_pot = 1;
        break;
    case R4P_LEDS:
        ctl.leds = m.u.leds;
        break;
    case R4P_STATUS:                               /* 1.3: the host's tempo is the host voice's (its delay) */
        if (ui.connected && mode == MODE_RUN4 && m.u.status.bpm10 >= 400u && m.u.status.bpm10 <= 2400u)
            syn.bpm10 = m.u.status.bpm10;
        /* fallthrough */
    case R4P_PAGE: case R4P_VALUE: case R4P_TOAST: case R4P_LIST: case R4P_FOCUS:
    case R4P_TAPE_WAVE: case R4P_TAPE_POS: case R4P_METERS:
        if (ui.connected && mode == MODE_RUN4)                              /* (frames before the ack are the previous session's) */
            ui_apply(&m);
        break;
    /* 1.3: the host voice, the voice list and the backup, in both modes (in Synth mode the host voice's settings wait) */
    case R4P_HV_ON:
        if (!ui.connected)
            break;
        if (hv.on && (!m.u.hvon.on || m.u.hvon.chan != hv.chan))
            hv_off_notes();
        hv.on = m.u.hvon.on;
        hv.chan = m.u.hvon.chan;
        ctl.want_hvstate = 1;
        break;
    case R4P_HV_VOICE:
        if (ui.connected) {
            hv_voice(&m.u.hvvoice);
            ctl.want_hvstate = 1;
        }
        break;
    case R4P_HV_PARAM:
        if (ui.connected) {
            uint32_t i;
            for (i = 0; i < m.u.hvparam.n; i++)
                hv_param(m.u.hvparam.id[i], m.u.hvparam.val[i]);
        }
        break;
    case R4P_NAMES_REQ:
        if (ui.connected)
            ctl.names_next = 0;
        break;
    case R4P_VOICE_GET:
        if (ui.connected)
            ctl.vget1 = (uint8_t)(m.u.b1.v + 1u);
        break;
    case R4P_BK_ACK: case R4P_BK_INFO: case R4P_BK_DATA:
        if (ui.connected)
            bk_frame(&m);
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
    memset(ctl.more_key, 255, sizeof ctl.more_key);
    ctl.pot_sent = -1;
    memset(ctl.snd_ch, 0, sizeof ctl.snd_ch);
    ui.setup = 0;
    setup_load();
    syn_init();
    syn_load_all();
    sq_midi_out = sq_send_rt;
    memset(&sui, 0, sizeof sui);
    mode = setup.mode;
    memset(&hv, 0, sizeof hv);
    hv.chan = 15;                                      /* (HV_STATE's channel before any HV_ON) */
    memset(&bk, 0, sizeof bk);
    ctl.names_next = 255;
    syn_host_defaults(&syn_alt);                       /* 1.3: the host voice's settings, parked */
    ml_init();
    sy_hold_store = 0;
    local_off = 0;
    if (mode == MODE_RUN4)
        syn_swap();                                    /* RUN4 mode: they are the ones that sound */
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    midi_pump(n);
    /* Synth mode: the FM voices; RUN4 mode: the host voice while it is on (and until its last note has rung out).
     * MASTER is the volume either way, as for the computer's own audio (base/device/uac_mix.h) */
    if (mode == MODE_SYNTH || hv.on || syn_active_voices) {
        uint32_t t0 = plat_ticks24(), dt;
        syn_render(lr, n, master_q12);
        dt = plat_ticks24() - t0;
        ctl.rend_ticks += dt;
        ctl.rend_frames += n;
        /* the overload guard: this block's share of its time (n frames at 44.1 kHz), per mille */
        syn_shed_calm(n, (uint32_t)((uint64_t)dt * 44100u / PLAT_TICKS_PER_US / 1000u / n));
        return;
    }
    syn_shed_calm(n, 0);
    syn_timed_due(0, 0xFFFFu);                         /* (nothing renders: what was placed is taken now) */
    for (i = 0; i < 2u * n; i++)                       /* RUN4 mode without it: a controller makes no sound */
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
    local_out_apply();
    shed_frame();
    setup_owed_frame();
    lights();
    ui_draw();
}
void app_input_only(void) { input(); }
void app_say(const char *a, const char *b) { (void)a; (void)b; }

/* ------------------------------------------- USB backup (fm1-base backup) --- */
/* fm1.toml backup = true (base/device/backup.c, tools/fm1_backup.py): everything the store holds but the panel's
 * calibration (the unit's own) as one image, app.h APP_BACKUP_*. Each object carries its own layout version and loads
 * older ones (SETUP 1 / 2, settings 2 / 3 / 4, patterns 1 / 2), so a backup of an older RUN4 CTL restores. */
int app_backup_playing(void) { return sq.run; }      /* the sequencer plays: STOP FIRST */
void app_backup_flush(void)                            /* a backup starts: what is not saved yet, saved */
{
    if (mode == MODE_SYNTH && (sui.dirty_set || syn_bank_dirty || sq_dirty))
        syn_save_all();                                /* (RUN4 mode: Synth mode's were saved when it was left) */
    if (ctl.setup_dirty || ctl.setup_owed) {
        plat_store_save(OBJ_SETUP, &setup, sizeof setup);
        ctl.setup_dirty = ctl.setup_owed = 0;
    }
}
void app_backup_hold(int on) { sy_hold_store = (uint8_t)(on != 0); }
void app_backup_restored(void)                         /* a restore: everything from the store again, as at power-on */
{
    uint8_t m = mode;
    syn_set_t hvs = syn;
    setup_load();
    setup.mode = m;                                    /* (the mode playing stays; the restored one is the next boot's) */
    memset(syn_bank, 0, sizeof syn_bank);
    syn_bank_dirty = 0;
    sq_defaults();
    syn_defaults();
    syn_load_all();                                    /* Synth mode's settings, the banks, the patterns */
    if (m == MODE_RUN4) {                              /* RUN4 mode: the host voice's set stays the sounding one */
        syn_alt = syn;
        syn = hvs;
    }
    syn_dirty = 1;
    syn_event(6, 0, 0, 0);
    ml_init();
    sq.fill = 0;
    sq_ed.held = -1;
    sq_ed.more = 0;
    sgu.valid = 0;
    smu.valid = 0;
    ctl.setup_dirty = ctl.setup_owed = 0;
    ui.setup = 0;
    setup_apply();
    if (mode == MODE_SYNTH)
        sp_build();
    ui.dirty = D_ALL;
    ui_toast("RESTORED", 20);
}
/* the screen mirror (fm1.toml mirror = true): a frame the link dropped is drawn again whole */
void app_redraw(void)
{
    ui.dirty = D_ALL;
    ctl.redraws++;
}

#ifdef PLAT_HOST
static void run_ms(uint32_t ms);
/* blesx HEX..: a SysEx frame (F0 .. F7) from the computer over Bluetooth, cut into USB-MIDI SysEx packets as the radio
 * hands them over; the stack's own packets are not simulated */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    static uint8_t by[8300];
    size_t n = 0, i;
    const char *p;
    if (!strcmp(cmd, "midiat")) {                      /* midiat FRAME B0 B1 B2: a message placed at FRAME of the next block */
        if (a && b && hq_n < 16u) {
            uint32_t st = (uint32_t)strtoul(b, 0, 16), x = c ? (uint32_t)strtoul(c, 0, 16) : 0u, y = d ? (uint32_t)strtoul(d, 0, 16) : 0u;
            hq[hq_n].pkt = (st >> 4) | st << 8 | x << 16 | y << 24;
            hq[hq_n++].frame = (uint16_t)atoi(a);
        }
        return 1;
    }
    if (!strcmp(cmd, "overload")) {                    /* overload N: N late renders (app_overload, as the device's) */
        int k, m = a ? atoi(a) : 1;
        for (k = 0; k < m; k++) {
            app_overload();
            run_ms(6);
        }
        return 1;
    }
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
    else if (!strcmp(what, "scope_frames")) *got = (int)sy.frames;         /* Synth mode's live frames drawn */
    else if (!strcmp(what, "scope_skipped")) *got = (int)sy.skipped;       /* ... with nothing to send (idle line) */
    else if (!strcmp(what, "scope_bpf")) *got = sy.frames ? (int)(sy.bytes / sy.frames) : 0;   /* bytes a frame */
    else if (!strcmp(what, "scope_us")) *got = sy.frames ? (int)(sy.ticks / PLAT_TICKS_PER_US / sy.frames) : 0;
    else if (!strcmp(what, "scope_reset")) { *got = 0; sy.frames = sy.bytes = sy.skipped = sy.ticks = 0; }
    else if (!strcmp(what, "lock_mask")) *got = (int)syn_lk.mask;           /* step 2: the locks sounding */
    else if (!strcmp(what, "lock_lanes")) *got = (int)sq_lanes_used(sq_sel_pat());
    else if (!strcmp(what, "lock_steps")) {                                 /* steps of the selected pattern with a lock */
        uint32_t i, c = 0;
        for (i = 0; i < SQ_STEPS; i++) c += sq_step_locks(sq_sel_pat(), i) != 0u;
        *got = (int)c;
    }
    else if (!strncmp(what, "lock", 4) && what[4] >= '0' && what[4] <= '9' && strchr(what, '_'))   /* lockS_ID: step S's lock */
        *got = sq_lock_get(sq_sel_pat(), (uint32_t)(atoi(what + 4) - 1) & 63u, (uint32_t)atoi(strchr(what, '_') + 1));
    else if (!strcmp(what, "lview")) *got = sq_ed.lview;
    else if (!strcmp(what, "seq_fill")) *got = sq.fill;
    else if (!strcmp(what, "ml_count")) *got = (int)ml_count();                /* MIDI learn */
    else if (!strcmp(what, "ml_arm")) *got = ml.arm != 0;
    else if (!strcmp(what, "ml_applied")) *got = (int)ml.applied;
    else if (!strcmp(what, "ml_def")) *got = setup.ml_def;
    else if (!strncmp(what, "ml_cc", 5) && what[5]) *got = setup.ml[(atoi(what + 5) - 1) & 15].cc;
    else if (!strcmp(what, "set_local")) *got = setup.local;
    else if (!strcmp(what, "local_off")) *got = local_off;
    else if (!strcmp(what, "shed")) *got = syn_shed;
    else if (!strcmp(what, "overloads")) *got = (int)ctl.overloads;
    else if (!strcmp(what, "redraws")) *got = (int)ctl.redraws;
    else if (!strcmp(what, "user_bank")) *got = syn_bank[0].used;
    else if (!strcmp(what, "syn_bright")) *got = syn.bright;
    else if (!strcmp(what, "syn_rev")) *got = syn.rev_mix;
    else if (!strcmp(what, "syn_lfo_pmd")) *got = syn.cur[FP_LPMD];
    else if (!strcmp(what, "gen_type")) *got = sgg.gen;
    else if (!strcmp(what, "undo_valid")) *got = sgu.valid;
    else if (!strcmp(what, "clip_n")) *got = sgc.n;
    else if (!strcmp(what, "seq_sum")) {                                   /* the selected pattern's notes, summed by step */
        uint32_t i, k, c = 0;
        for (i = 0; i < SQ_STEPS; i++) for (k = 0; k < sq_n(&sq_ed_pat()->s[i]); k++) c += sq_ed_pat()->s[i].note[k] * (i + 1u);
        *got = (int)c;
    }
    else if (!strncmp(what, "seq_cond", 8) && what[8]) *got = (int)sq_cond(&sq_ed_pat()->s[(atoi(what + 8) - 1) & 63]);
    else if (!strcmp(what, "seq_played")) *got = (int)sq.played;
    else if (!strcmp(what, "held_n")) *got = (int)sq_ed_held_n();
    else if (!strcmp(what, "eff_alg")) *got = (int)syn_eff_alg + 1;
    else if (!strcmp(what, "arp_held")) *got = syn_sq.n_held;
    else if (!strcmp(what, "key_notes")) {                                  /* notes the held keys sound */
        uint32_t i, c = 0;
        for (i = 0; i < NKEYS; i++) c += ctl.chord_n[i];
        *got = (int)c;
    }
    else if (!strncmp(what, "keynote", 7)) *got = ctl.chord_n[atoi(what + 7) % NKEYS] ? ctl.chord[atoi(what + 7) % NKEYS][0] : -1;
    else if (!strcmp(what, "newest_note")) *got = sy_newest() ? sy_newest()->note : -1;
    else if (!strcmp(what, "newest_ofs")) *got = sy_newest() ? sy_newest()->ofs : -1;     /* its frame in its sub-block */
    else if (!strcmp(what, "patch_mark") || !strcmp(what, "patch_changed")) {   /* the sounding voice: mark it, compare */
        static uint32_t mark;
        uint32_t i, c = 0;
        for (i = 0; i < FP_SIZE; i++) c += syn.cur[i] * (i + 1u);
        if (what[6] == 'm') { mark = c; *got = 0; } else *got = c != mark;
    }
    else if (!strcmp(what, "mut_amt")) *got = syn.mut_amt;
    else if (!strcmp(what, "mut_valid")) *got = smu.valid;
    else if (!strcmp(what, "mut_undone")) *got = smu.undone;
    else if (!strcmp(what, "patch_sum")) { uint32_t i, c = 0; for (i = 0; i < FP_SIZE; i++) c += syn.cur[i] * (i + 1u); *got = (int)c; }
    else if (!strcmp(what, "song_on")) *got = syn.song;
    else if (!strcmp(what, "chain_len")) *got = sq_ch.len;
    else if (!strcmp(what, "chain_cur")) *got = sqi.chain_cur + 1;
    else if (!strcmp(what, "song_ci")) *got = sq.ci + 1;
    else if (!strcmp(what, "in_song")) *got = sq.in_song;
    else if (!strcmp(what, "syn_scale")) *got = syn.scale;
    else if (!strcmp(what, "hv_on")) *got = hv.on;                          /* 1.3: the host voice */
    else if (!strcmp(what, "hv_chan")) *got = hv.chan + 1;
    else if (!strcmp(what, "hv_voice")) *got = hv.voice;
    else if (!strcmp(what, "hv_alg")) *got = (hv_set()->cur[FP_ALG] & 31) + 1;
    else if (!strcmp(what, "hv_level")) *got = hv_set()->level;
    else if (!strcmp(what, "hv_bright")) *got = hv_set()->bright;
    else if (!strcmp(what, "hv_bpm10")) *got = hv_set()->bpm10;
    else if (!strcmp(what, "hv_mo_cap")) *got = hv_set()->mo_cap;
    else if (!strcmp(what, "hv_mo_mode")) *got = hv_set()->mo_mode;
    else if (!strcmp(what, "mo_cap")) *got = syn.mo_cap;
    else if (!strcmp(what, "mo_mode")) *got = syn.mo_mode;
    else if (!strcmp(what, "mo_live")) *got = (int)((syn_mo.pos * 100 + 32768) >> 16);   /* where the morph is, % */
    else if (!strcmp(what, "mo_on")) *got = syn_mo.mask != 0;
    else if (!strcmp(what, "syn_level")) *got = (mode == MODE_SYNTH ? &syn : &syn_alt)->level;   /* Synth mode's own */
    else if (!strcmp(what, "render_permille")) *got = ctl.rend_frames ? (int)((uint64_t)ctl.rend_ticks / PLAT_TICKS_PER_US * 44100u / 1000u / ctl.rend_frames) : 0;
    else if (!strcmp(what, "render_reset")) { *got = 0; ctl.rend_ticks = ctl.rend_frames = 0; }
    else if (!strcmp(what, "tape_on")) *got = tv.on;
    else if (!strcmp(what, "tape_layout")) *got = tv_layout();
    else if (!strcmp(what, "tape_frames")) *got = (int)tv.frames;
    else if (!strcmp(what, "tape_bpf")) *got = tv.frames ? (int)(tv.bytes / tv.frames) : 0;
    else if (!strcmp(what, "tape_us")) *got = tv.frames ? (int)(tv.ticks / PLAT_TICKS_PER_US / tv.frames) : 0;
    else if (!strcmp(what, "tape_reset")) { *got = 0; tv.frames = tv.bytes = tv.ticks = 0; }
    else if (!strcmp(what, "tape_x")) *got = tv.drawn_x;
    else if (!strcmp(what, "bk_state")) *got = bk.st;
    else if (!strcmp(what, "bk_part")) *got = bk.k;
    else if (!strcmp(what, "setup_sub")) *got = ctl.setup_sub;
    else if (!strcmp(what, "user_voices")) *got = (int)bk_user_count();
    else if (!strcmp(what, "big")) *got = ui.big1;                         /* 1.2: the slot shown big + 1, 0 = none */
    else if (!strcmp(what, "touch")) *got = ui.touch1;
    else if (!strcmp(what, "cursor")) *got = ui.page.cursor;
    else if (!strcmp(what, "item_idx")) *got = ui.page.item_idx;
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
