/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL: help cards. Hold one button alone for a second (no other button, key or knob since it went down) and a
 * card says what it does here (this mode, this page); letting go closes it. The card only explains: the press did what
 * it always does, and the release does too (app.c card_*: the rules of precedence). Part of the unity build, included
 * by app.c after its state. */

#define HELP_MS 1000u
#define HELP_LINES 6
static const char *const BTN_NAME[14] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY",
                                         "REC", "OCT-", "OCT+"};

#if R4_DUB
static int db_perf_on(void);                  /* dub_ui.c: PERFORM is on */
#endif
/* the card's lines for button b; *tag: the mode or page it speaks for. A line that starts with "SEL+" or "HOLD" is a
 * combination: drawn grey */
static int help_lines(uint32_t b, const char *l[HELP_LINES], const char **tag)
{
    int n = 0;
#define L(t) (l[n++] = (t))
    if (hc_setup && b != B_SEL) {             /* pressed on the SETUP page */
        *tag = "SETUP";
        if (b == B_SAVE) { L("DOES WHAT IS ARMED HERE:"); L("SEND, WRITE, CLEAR"); }
        else if (b == B_PLAY || b == B_REC || b == B_OCTDN || b == B_OCTUP) { L("NOTHING ON THIS PAGE"); L("ANY PAGE BUTTON LEAVES IT"); }
        else { L("LEAVES THE SETUP PAGE"); L("SELECT: ITS FOUR SUB-PAGES"); }
        return n;
    }
#if R4_AMB
    if (mode == MODE_AMB) {
        *tag = "AMB";
        switch (b) {
        case B_HOME: L("GARDEN: TEMPO, EVOLVE,"); L("MOTION, DRIFT"); L("HOLD + SELECT: THE TEMPO"); L(R4_DUB ? "SEL+HOME: NEXT MODE (DUB)" : "SEL+HOME: NEXT MODE (RUN4)"); break;
        case B_ENV: L("PAD: LEVEL, SWELL,"); L("BRIGHT, COLOUR"); break;
        case B_ARP: L("BELLS: LEVEL, DENSITY,"); L("TONE, OCTAVE"); break;
        case B_LFO: L("BASS: LEVEL, OCTAVE,"); L("GLIDE, FIFTH"); break;
        case B_SEQ: L("PULSE: LEVEL, HITS,"); L("SWING, HATS"); break;
        case B_FX: L("SPACE: ECHO, ECHO TIME,"); L("REVERB, SIZE"); break;
        case B_GLO: L("KEY: ROOT, TUNE,"); L("MIDI OUT, SCALE"); L("SEL+GLO: SETUP"); break;
        case B_EDIT: L("THE NEXT CHORD,"); L("ON THE NEXT BAR"); break;
        case B_PLAY: L("THE GARDEN GROWS / RESTS"); L("SEL+PLAY: RUN4'S PLAY"); break;
        case B_REC: L("FREEZE: THE CHORD HOLDS,"); L("THE REVERB RINGS ON"); L("AGAIN: THAW"); L("SEL+REC: RUN4'S REC"); break;
        case B_SAVE: L("SAVES ALL EIGHT PIECES"); L("KEPT DOWN 2 S: THIS PIECE"); L("BACK TO ITS RECIPE"); break;
        case B_OCTDN: L("THE BELLS AN OCTAVE DOWN"); break;
        case B_OCTUP: L("THE BELLS AN OCTAVE UP"); break;
        default: L("SHIFT, HOLD IT WITH"); L(R4_DUB ? "HOME: NEXT MODE (DUB)" : "HOME: NEXT MODE (RUN4)"); L("GLO: SETUP"); L("REC, PLAY: RUN4'S"); break;
        }
        return n;
    }
#endif
#if R4_DUB
    if (mode == MODE_DUB) {                   /* DUB mode: DUB-1's cards, as its own buttons do here */
        *tag = "DUB";
        if (db_perf_on() && b != B_SEL) {         /* (PERFORM on: the keys are its) */
            if (b == B_HOME || b == B_ENV || b == B_ARP || b == B_LFO || b == B_SEQ || b == B_FX || b == B_GLO) {
                *tag = "PERFORM";
                L("ITS PAGE ON THE KNOBS;"); L("THE KEYS STAY PERFORM'S"); L("SEL TAPPED: PERFORM OFF");
                return n;
            }
        }
        switch (b) {
        case B_HOME: L("DUB: FILTER, EVOLVE,"); L("MOTION, DRIFT"); L("AGAIN: TEMPO, ROOT, MODE,"); L("SWING; HOLD + SELECT: TEMPO"); L("SEL+HOME: NEXT MODE (RUN4)"); break;
        case B_ENV: L("CHORD: LEVEL, STABS,"); L("DECAY, CUTOFF"); L("ALGORITHM: ITS VOICE"); L("AGAIN: SOUND; AGAIN: FM"); break;
        case B_ARP: L("TEXTURE: PINGS, DENSITY,"); L("CLOUD, CRACKLE"); L("ALGORITHM: THE PINGS' VOICE"); L("AGAIN: SOUND; AGAIN: FM"); break;
        case B_LFO: L("BASS: LEVEL, OCTAVE,"); L("GROOVE, FIFTH"); L("ALGORITHM: ITS VOICE"); L("AGAIN: SOUND; AGAIN: FM"); break;
        case B_SEQ: L("DRUMS: LEVEL, HITS,"); L("SWING, DUCK"); L("ALGORITHM: THE KIT"); L("AGAIN: SOUND; AGAIN:"); L("THE PATTERN (WHITE KEYS)"); break;
        case B_FX: L("SPACE: ECHO, ECHO TIME,"); L("FEEDBACK, REVERB"); break;
        case B_GLO: L("GLOBAL (EVERY PIECE):"); L("TUNE, MIDI OUT"); L("SEL+GLO: SETUP"); break;
        case B_EDIT: L("TAP: THE CHORDS, 8 SLOTS"); L("(THE KEYS: THE SLOT'S ROOT)"); L("HOLD: THE NEXT CHORD"); break;
        case B_SEL: L("TAP: PERFORM. KNOBS FILTER,"); L("DUB, ENERGY, SPACE; WHITE 6"); L("HELD: THE MIX. WHITE 1-5 MUTE,"); L("BLACK 1-5 SOLO; WHITE 7-11"); L("THROW OPEN SHUT DROP ECHO"); L("SEL+HOME NEXT MODE, +GLO SETUP"); break;
        case B_PLAY: L("PLAYS / RESTS"); L("SEL+PLAY: RUN4'S PLAY"); break;
        case B_SAVE: L("SAVES ALL EIGHT PIECES"); L("KEPT DOWN 2 S: THIS PIECE"); L("BACK TO ITS RECIPE"); break;
        case B_OCTDN: L("THE PINGS AN OCTAVE DOWN"); break;
        case B_OCTUP: L("THE PINGS AN OCTAVE UP"); break;
        default: L("TAP: DUB HOLD"); L("HOLD: THE EFFECTS GRID"); break;
        }
        return n;
    }
#endif
    if (mode == MODE_SYNTH) {
        *tag = "SYNTH";
        if (synth_step_rec() && (b == B_EDIT || b == B_GLO)) {
            *tag = "STEP REC";
            L(b == B_EDIT ? "WRITES A REST" : "WRITES A TIE");
            L("AND MOVES ON");
            return n;
        }
        switch (b) {
        case B_HOME: L("SYNTH: BRIGHT, FEEDBACK,"); L("ENV SPEED, LEVEL"); L("AGAIN: THE MORPH (A / B)"); L(R4_AMB ? "SEL+HOME: NEXT MODE (AMB)" : R4_DUB ? "SEL+HOME: NEXT MODE (DUB)" : "SEL+HOME: NEXT MODE (RUN4)"); break;
        case B_EDIT: L("OPERATOR: RATIO, FINE,"); L("LEVEL, DETUNE; AGAIN: MORE"); L("SELECT, BLACK KEYS: OP 1-6"); L("SEL+EDIT: MUTATE"); break;
        case B_ENV: L("ENVELOPE: RATES 1-4"); L("AGAIN: LEVELS 1-4"); L("SELECT, BLACK KEYS: OP 1-6"); break;
        case B_LFO: L("LFO: SPEED, DELAY,"); L("PITCH MOD, AMP MOD"); L("AGAIN: WAVE, SENS, SYNC"); break;
        case B_FX: L("EFFECTS: DRIVE, CHORUS,"); L("DELAY, REVERB"); L("AGAIN: TIMES, TYPE, SIZE"); break;
        case B_GLO: L("GLOBAL: TRANSPOSE, TUNE,"); L("MIDI CH, BEND"); L("AGAIN: VELOCITY, MUTATE;"); L("THEN THE SCALE KEYS"); L("SEL+GLO: SETUP"); break;
        case B_ARP: L("ARP: ON, MODE, RATE,"); L("OCTAVES"); L("AGAIN: GATE, CHORD,"); L("INVERSION, SPREAD"); break;
        case B_SEQ: L("SEQUENCER: SIX SUB-PAGES,"); L("THE WHITE KEYS ARE STEPS"); L("HOLD A STEP: LOCKS, NOTES"); L("SEL+SEQ HELD: FILL"); break;
        case B_PLAY: L("STARTS / STOPS THE"); L("SEQUENCER"); L("SEL+PLAY: RUN4'S PLAY"); break;
        case B_REC: L("RECORDS INTO THE SEQUENCER"); L("(STOPPED, ON SEQ: STEPS)"); L("SEL+REC: RUN4'S REC"); break;
        case B_SAVE:
            if (sui.page == SP_SEQ) { L("DOES WHAT IS ARMED:"); L("COPY TO, CLEAR, GENERATE,"); L("REVERSE, MIRROR, SONG"); L("SEL+SAVE: UNDO"); }
            else { L("STORES THE VOICE IN ITS"); L("USER SLOT (OR A FREE ONE)"); L("SEL+SAVE: UNDO MUTATE"); }
            break;
        case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); break;
        case B_OCTUP: L("THE KEYS AN OCTAVE UP"); break;
        default: L("SHIFT, HOLD IT WITH"); L("HOME NEXT MODE, GLO SETUP"); L("EDIT MUTATE, SAVE UNDO"); L("K1-K4 MIDI LEARN, KEYS"); L("TRANSPOSE, SEQ FILL"); L("REC, PLAY: RUN4'S"); break;
        }
        return n;
    }
    *tag = "RUN4";                            /* RUN4 mode: the fixed layer (RUN4 decides what the pages show) */
    switch (b) {
    case B_HOME: L("RUN4'S HOME PAGE"); L("TWICE QUICKLY: ITS MODES"); L("SEL+HOME: NEXT MODE (SYNTH)"); break;
    case B_PLAY: L("RUN4'S PLAY"); break;
    case B_REC: L("RUN4'S REC"); break;
    case B_SAVE: L("RUN4'S UNDO"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("(RUN4 IS TOLD TOO)"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("(RUN4 IS TOLD TOO)"); break;
    case B_SEL: L("SHIFT: RUN4'S SECOND"); L("FUNCTIONS"); L("SEL+HOME: NEXT MODE (SYNTH)"); L("SEL+GLO: SETUP"); break;
    default: L("OPENS RUN4'S PAGE OF"); L("THIS NAME; RUN4 DECIDES"); L("WHAT IT SHOWS"); break;
    }
    return n;
#undef L
}

/* the card, over the band and the tiles (y 34..199), in two strips (the canvas holds 240 x 144) */
static void help_draw(void)
{
    const char *l[HELP_LINES], *tag = "";
    uint32_t b = (uint32_t)(ui.card - 1u) % 14u;
    int n = help_lines(b, l, &tag), k, s;
    for (s = 0; s < 2; s++) {
        int32_t y0 = s * 83;
        cv_begin(240, 83, R4_BG);
        cv_oy = -y0;
        cv_y0 = 0;
        cv_y1 = 83;
        cv_rect(6, 2, 228, 162, sy_accent(100));
        cv_rect(8, 4, 224, 158, R4_BG);
        ptext(18, 12, &FONT_B, BTN_NAME[b], sy_accent(100));
        ptext(222 - pwidth(&FONT_XS, tag), 16, &FONT_XS, tag, R4_LABEL);
        cv_rect(18, 36, 204, 1, R4_EDGE);
        for (k = 0; k < n; k++) {
            int combo = !strncmp(l[k], "SEL+", 4) || !strncmp(l[k], "HOLD", 4) || !strncmp(l[k], "AGAIN", 5);
            ptext(18, 46 + k * 18, &FONT_XS, l[k], combo ? R4_LABEL : R4_TEXT);
        }
        cv_oy = 0;
        cv_y1 = (int32_t)cv_h;
        cv_blit(0, (uint32_t)(34 + y0));
    }
}
