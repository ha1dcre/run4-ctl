/* SPDX-License-Identifier: GPL-3.0-only
 * DUB-1 by Santiago Begueria (https://github.com/sbegueria/fm1-dub, firmware/src/dsp/amb.h at v1.2.0), a fork of AMB-1
 * by Charles Vestal; GPL-3.0-only. Modified for RUN4 CTL (DUB mode) by ha1dcre, 2026: its names renamed (du_, DU_, DP_,
 * DM_, DSEC_), du_enter, du_clock_isr, du_shed; MIDI out 0..2; the buffers in RUN4 CTL's mode arena and the plate in
 * src/plate.c; no scope or spectrum buffers (see dub.c). */
/* The instrument: a generative dub techno machine. Four layers draw on one pool of notes (a scale: the
 * root plus up to eleven more pitch classes) and one chord that moves now and then through it:
 *
 *   CHORD   the stab: four voices (two detuned saws each, or an FM keys) struck together on a Euclidean
 *           pattern off the beat, through a resonant low-pass that opens with each stab and sweeps with the
 *           arrangement; most of it goes to the echo. One voicing (an open minor 7, or a 9 without its fifth)
 *           shifted whole to each new root, as a chord memory. The chord moves every DRIFT bars, or holds.
 *   PINGS   short FM blips played by four loops of unequal length (13, 17, 23, 29 eighths), each holding a
 *           few notes of the chord that shift against the others; heavily echoed.
 *   BASS    a sub on the chord's root (now and then its fifth), on one of five grooves.
 *   DRUMS   a four-on-the-floor kick (it ducks the rest: DUCK), a shaker on the sixteenths (HITS sets how
 *           many) and an open hat off the beat.
 *
 * Over it all, the ARRANGEMENT: eight-bar phrases, each Full, a Break (the kick and the bass out, the filter
 * building), a Groove (fewer stabs) or Thin (no bass, no shaker); MOTION sets how often it leaves Full. Before
 * the kick comes back, a fill. EVOLVE turns the patterns and throws a stab into the echo now and then.
 * Then a tape echo (to ~5.6 s, two heads, a band-pass in its feedback that the arrangement sweeps, a
 * saturating feedback that may pass 100%), a short Echoplex, a granular CLOUD (after JIANT's) blooming into
 * Dattorro's plate, and a bed of vinyl crackle.
 * FREEZE (dub hold): the echo and the cloud stop listening and play what they hold for ever; the chord holds.
 *
 * Threads: du_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state below marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define DU_SR 44100.0f
#define DU_NLOOP 4                     /* ping loops */
#define DU_LOOP_MAX 29
#define DU_NDRUM 5                     /* kick, shaker, open hat, rim, clap */
#define DU_NPAD 4                      /* chord voices */

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { DM_IONIAN, DM_DORIAN, DM_PHRYGIAN, DM_LYDIAN, DM_MIXOLYDIAN, DM_AEOLIAN, DM_PENT_MAJ,
       DM_PENT_MIN, DU_NMODES };
extern const char *const DU_MODE_NAME[DU_NMODES];
extern const uint16_t DU_MODE_POOL[DU_NMODES];
extern const char *const DU_NOTE_NAME[12];
extern const uint8_t DU_LOOP_LEN[DU_NLOOP];
extern const uint8_t DU_DRUM_N[DU_NDRUM];

/* parameters: value ranges in du_param_info() */
enum {
    DP_CHORD, DP_PINGS, DP_BASS, DP_DRUMS,                /* layer levels */
    DP_DENSITY, DP_EVOLVE, DP_DRIFT, DP_HITS,             /* motion: the pings' notes, change, the chord's bars, the shaker */
    DP_ECHO, DP_ETIME, DP_REVERB, DP_FEEDBACK,            /* space */
    DP_CUTOFF, DP_DECAY, DP_PTONE, DP_MOTION,             /* the stab's filter and length, the pings' tone, the arrangement */
    DP_TUNE, DP_MIDI, DP_TEMPO, DP_ROOT, DP_MODE, DP_POCT,  /* setup, panel encoders, the pings' octave */
    DP_CVOICE,                                         /* the chord's voice (DU_CV_NAME) */
    DP_BASSOCT, DP_GROOVE, DP_FIFTH,                     /* bass: its octave, its pattern, how often the fifth */
    DP_SWING, DP_HATS,                                  /* drums: swing, the hats' colour */
    DP_STABS, DP_CRACKLE, DP_DUCK, DP_SIZE,               /* stabs a bar, the vinyl, the kick's ducking, the plate */
    DP_CLOUD,                                          /* the granular reverb */
    DP_PVOICE, DP_BVOICE, DP_KIT,                        /* the pings' voice, the bass's, the drums' kit */
    /* the sound pages: each relative to the voice as it is (0), -50 .. +50 (the kick's tune: semitones) */
    DP_CATT, DP_CCOL, DP_CRES, DP_CENV,                   /* the chord: attack, colour, resonance, the filter's envelope */
    DP_PTON, DP_PRAT, DP_PLEN, DP_PBEND,                  /* the pings: tone, ratio, length, bend */
    DP_BBITE, DP_BSHAPE, DP_BLEN, DP_BSLIDE,              /* the bass: bite, shape, length, slide */
    DP_KTUNE, DP_KLEN, DP_KPUNCH, DP_HTONE,               /* the drums: the kick's tune, length, punch; the hats' tone */
    /* the FM pages (an FM voice): the algorithm (0: the voice's; 1..9), the upper operators' ratio, op 4's
     * feedback, the modulators' decay: relative, -50 .. +50 */
    DP_CALG, DP_CFRAT, DP_CFB, DP_CFDEC,
    DP_PALG, DP_PFRAT, DP_PFB, DP_PFDEC,
    DP_BALG, DP_BFRAT, DP_BFB, DP_BFDEC,
    /* the chords (EDIT's page): eight slots, each a root (0..11, absolute), a kind (DU_CQ_*: 0 empty) and a type
     * (DU_CX_*); Play: chosen at random (0) or the slots in turn (1). An empty slot holds the chord before it */
    DP_CR0, DP_CR1, DP_CR2, DP_CR3, DP_CR4, DP_CR5, DP_CR6, DP_CR7,
    DP_CQ0, DP_CQ1, DP_CQ2, DP_CQ3, DP_CQ4, DP_CQ5, DP_CQ6, DP_CQ7,
    DP_CX0, DP_CX1, DP_CX2, DP_CX3, DP_CX4, DP_CX5, DP_CX6, DP_CX7,
    DP_PMODE,
    /* the drums' pattern page: each lane's sixteen steps (bit i: step i, as int16) and its accents, its chance (%);
     * the accents' strength; Play: Auto (generated, as ever) or Pattern (these) */
    DP_DPAT0, DP_DPAT1, DP_DPAT2, DP_DPAT3, DP_DPAT4,
    DP_DACC0, DP_DACC1, DP_DACC2, DP_DACC3, DP_DACC4,
    DP_DCH0, DP_DCH1, DP_DCH2, DP_DCH3, DP_DCH4,
    DP_DACCENT, DP_DMODE,
    DP_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} du_param_t;
const du_param_t *du_param_info(int i);
void du_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* commands (main loop -> render) */
void du_init(void);
void du_set(int param, int value);
void du_pool(uint32_t pool);                   /* the scale, relative to the root (bit 0 always set) */
void du_play(int on);                          /* it plays / rests (the tails ring on) */
void du_next(void);                            /* the next chord: on the next bar (now while resting) */
void du_freeze(int on);
void du_seed(uint32_t s);                      /* stir some entropy into the generators */
void du_panic(void);                           /* everything quiet, now */
void du_regrow(void);                          /* a new piece: fresh loops and patterns, the home chord */
/* the ping loops, packed a byte a step (DU_PINGS_N: every loop's steps, in turn): the note offset in the low
 * nibble (15: a rest), the velocity in the high one (50 + 3 x v). A piece keeps them; du_pings gives them back
 * (0: none kept, the loops grown afresh), taken at once and again after each du_regrow */
#define DU_PINGS_N 82
void du_pings_get(uint8_t *b);
void du_pings(const uint8_t *b);
/* performance (SEL's mode): layers muted (DU_M_*, a mask: set = muted), and the moves held now (DU_PF_*) */
enum { DU_M_CHORD = 1, DU_M_PINGS = 2, DU_M_BASS = 4, DU_M_KICK = 8, DU_M_HATS = 16 };
enum { DU_PF_THROW = 1, DU_PF_OPEN = 2, DU_PF_CLOSE = 4, DU_PF_KICKDROP = 8, DU_PF_ECHO = 16 };   /* (KICKDROP: Drop, the kick and the bass) */
void du_mute(uint32_t mask);
void du_perf(uint32_t flags);
/* the master actions (HOME's and PERFORM's knobs), -50..50 each, 0: the piece as it is; for playing, never saved.
 * Filter: down a low-pass on the music (Shut's), up a resonant high-pass (Open's), the kick left out; Dub: the
 * echo's send and feedback; Energy: Density, Hits and Stabs; Space: the reverb, the cloud and the plate's size */
enum { DU_MAC_FILTER, DU_MAC_DUB, DU_MAC_ENERGY, DU_MAC_SPACE, DU_NMAC };
void du_macro(int i, int v);
/* the effects grid (REC held): the effects held now, a bit each (DU_FX_*), in the grid's order */
enum { DU_FX_THROW, DU_FX_TIME, DU_FX_TAPESTOP, DU_FX_SWELL, DU_FX_LOWPASS, DU_FX_HIGHPASS, DU_FX_WAH, DU_FX_PHONE,
       DU_FX_VERB, DU_FX_CRASH, DU_FX_FREEZE, DU_FX_WASH, DU_FX_STUTTER, DU_FX_GATE, DU_FX_SIREN, DU_FX_RISER, DU_NFX };
extern const char *const DU_FX_NAME[DU_NFX];
void du_fx(uint32_t mask);
/* MIDI clock in (as FoMni): ticks drive the 1/16 grid (6 a step) while they keep coming */
enum { DU_CLK_TICK, DU_CLK_START, DU_CLK_CONTINUE, DU_CLK_STOP };
void du_clock(int msg);
void du_clock_isr(int msg);                    /* (RUN4 CTL) the same, from the audio context, before du_render */
void du_enter(void);                           /* (RUN4 CTL) the mode comes in: the arena's buffers set up, the plate
                                                * silent; the render held meanwhile */
extern volatile uint8_t du_shed;               /* (RUN4 CTL) the overload guard's step, 0..4 */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void du_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void du_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* the voices: each layer's sounds (ALGORITHM on the layer's page) */
#define DU_NCV 7
#define DU_NPV 5
#define DU_NBV 5
#define DU_NKIT 5
extern const char *const DU_CV_NAME[DU_NCV], *const DU_PV_NAME[DU_NPV], *const DU_BV_NAME[DU_NBV],
    *const DU_KIT_NAME[DU_NKIT];

/* the arrangement's sections */
enum { DSEC_FULL, DSEC_BREAK, DSEC_GROOVE, DSEC_THIN, DU_NSEC };
extern const char *const DU_SEC_NAME[DU_NSEC];

/* state for the screen (UI) */
extern volatile uint8_t du_playing, du_frozen, du_ext;
extern volatile uint8_t du_chord[DU_NPAD];     /* the chord's notes (MIDI), as the voices play them */
extern volatile uint8_t du_chord_root;         /* the chord's root, MIDI pitch class 0..11 */
extern volatile uint8_t du_chord_n;            /* its distinct tones: 3 or 4 */
extern volatile float du_drift;                /* 0..1: how far to the next chord (0 while it holds) */
extern volatile float du_pad_level, du_bass_level;   /* the stab's and the bass's envelopes, 0..1 */
extern volatile uint8_t du_loop_pos[DU_NLOOP];
extern volatile int8_t du_loop_note[DU_NLOOP][DU_LOOP_MAX];   /* -1 rest, else the slot's offset */
extern volatile float du_loop_glow[DU_NLOOP];  /* 0..1: a ping just sounded from this loop */
extern volatile uint8_t du_drum_pos[DU_NDRUM];
extern volatile uint32_t du_drum_pat[DU_NDRUM];/* bit i: a hit on step i (all 16 steps) */
extern volatile float du_drum_glow[DU_NDRUM];
extern volatile uint32_t du_bars;              /* sixteenths played since start (for tests) */
extern volatile float du_arc;                  /* the filter's sweep, -1 dark .. +1 open (times Motion) */
extern volatile uint8_t du_pulse_rest;         /* the kick is out (a Break) */
extern volatile uint8_t du_pad_note[DU_NPAD];  /* the note each voice plays (MIDI) */
extern volatile float du_pad_amp[DU_NPAD];     /* and how loud */
extern volatile uint8_t du_section;            /* DSEC_*: the phrase playing */
extern volatile uint8_t du_phrase_bar;         /* 0..7: its bar */
extern volatile uint32_t du_stab_pat;          /* the stabs' pattern this bar (bit i: step i) */
extern volatile uint32_t du_throws;            /* stabs thrown into the echo so far (for tests) */
/* for the screen's ripples and lanes */
extern volatile uint32_t du_stabs;             /* stabs struck so far */
extern volatile float du_stab_vel;             /* the last one's strength, 0..1 */
extern volatile uint8_t du_stab_thrown;        /* the last one was thrown */
extern volatile float du_echo_ms, du_echo_g;   /* the echo's time; the level a repeat keeps of the last (1: held) */
extern volatile float du_echo_in;              /* how much of the stab goes into the echo, 0..1 */
extern volatile float du_duck;                 /* the ducking now, 0 .. 1 (full depth) */
extern volatile uint16_t du_lane[6];           /* this bar as it plays: the stabs, the kick, the hats, the rim, the clap, the bass */

/* the pure parts, for the tests and the screen */
uint32_t du_euclid(int k, int n, int rot);     /* Bjorklund's pattern, as a bitmask (bit 0 = step 0) */
int du_scale(uint32_t pool, int *steps);       /* the pool's pitch classes (relative), ascending; count */
/* the chord on scale degree deg: its tones (MIDI pitch classes, absolute), 3 or 4 of them; count */
int du_chord_tones(uint32_t pool, int root, int deg, int *pcs);
/* the stab's voicing on degree deg: four intervals above the chord's root, ascending (shape 0: an open
 * 7th chord, root 5th 7th 10th; shape 1: root 7th 10th 14th, a 9th without its fifth) */
void du_voicing(uint32_t pool, int deg, int shape, int *iv);
const char *du_chord_name(const int *pcs, int n, char *buf);   /* "Em7", "Dsus4", "C" ... */
uint32_t du_groove(int g);                     /* the bass's pattern g: bit i, a note on step i */
int du_drift_bars(int v);                      /* Drift's value in bars; 0: the chord holds */
int du_kick_note(int root);                    /* the kick's note (MIDI) for the key's root: F1..B1 */
/* a layer's envelope as it sounds now (0 chord, 1 pings, 2 bass, 3 kick), for a sound page's drawing: n points
 * of its level (0..1) over *secs seconds, and of a second curve (the chord's filter envelope, the kick's
 * pitch; else 0) */
void du_env_shape(int layer, float *lvl, float *aux, int n, float *secs);
/* the FM pages: is the layer's voice FM now (0 chord, 1 pings, 2 bass), and its algorithm as it plays (0..8) */
int du_voice_fm(int layer);
#define DU_NPROG 8
enum { DU_CQ_EMPTY, DU_CQ_MAJOR, DU_CQ_MINOR, DU_CQ_SUS2, DU_CQ_SUS4, DU_CQ_DIM, DU_CQ_AUG, DU_NCQ };
enum { DU_CX_TRIAD, DU_CX_6, DU_CX_7, DU_CX_MAJ7, DU_CX_9, DU_CX_11, DU_CX_ADD9, DU_NCX };
extern const char *const DU_CQ_NAME[DU_NCQ], *const DU_CX_NAME[DU_NCX];
extern volatile int8_t du_prog_pos;            /* the slot the progression is on (0..7), -1: chosen at random */
extern volatile int8_t du_chord_slot;          /* the slot whose chord sounds, -1: a chord of the scale */
int du_deg_of(uint32_t pool, int rel);         /* the scale degree for a note above the root (nearest in the pool) */
void du_chord_iv(int kind, int type, int *iv); /* a slot's voicing: four intervals above its root, ascending */
const char *du_chord_label(int root, int kind, int type, char *buf);   /* "Fm7", "Bbsus4", "Eb6" ("-": empty) */
int du_fm_alg(int layer);
#define DU_FM_NALG 9

/* the pieces: recipes for every parameter (Tune and MIDI out included, but they are the player's) */
#define DU_NPIECES 8
const char *du_piece_name(int i);
void du_piece(int i, int16_t *par, uint16_t *pool);
