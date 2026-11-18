# RUN4 CTL

RUN4 CTL is a firmware for the M-VAVE FM-1 with four modes:

* **RUN4 mode** turns the FM-1 into a hardware controller for the iOS app RUN4: the 27 keys, 14 buttons, 7 encoders
  and the MASTER knob are sent to the app over USB-MIDI or Bluetooth MIDI, and the app draws its pages and lights on
  the FM-1's 240 x 240 screen and LEDs. The FM-1 makes no sound in this mode, unless RUN4 plays it as an instrument
  (the **FM-1 engine** of a RUN4 track, see "The FM-1 as RUN4's instrument").
* **Synth mode** turns the FM-1 into a standalone six-operator FM synthesizer with effects, an arpeggiator and a
  step sequencer (16 patterns of up to 64 steps, chords, ties, live and step recording). It also plays from MIDI in, so it works as a sound module for RUN4, TOLF or any MIDI source.
* **AMB mode** turns the FM-1 into a generative ambient machine (AMB-1's garden, after Brian Eno's *Music for
  Airports*): breathing pads, bells on tape loops of unequal length, a gliding bass, quiet Euclidean percussion, a
  tape echo and a plate reverb, in eight pieces. You tend it rather than play it.

* **DUB mode** turns the FM-1 into a generative dub techno machine (DUB-1's, after Basic Channel's chords dissolving
  in a tape echo): chord stabs through a resonant filter into a tape echo, FM pings on loops of unequal length, a sub
  bass on five grooves, a kick that ducks the rest, hats, a granular cloud, a plate, vinyl crackle, in eight pieces that
  arrange themselves in eight-bar phrases. You steer it: the master actions Filter, Dub, Energy and Space, mutes, solos
  and dub moves, dub hold and an effects grid.

Switch with **SEL + HOME**: each press goes to the next mode, round: RUN4 -> SYNTH -> AMB -> DUB -> RUN4. The FM-1
remembers the mode it was left in. (Before firmware 0.7 SEL + HOME went back and forth between RUN4 and Synth; since
0.9 the round has four stops: from Synth mode back to RUN4 it is three presses, through AMB and DUB mode, which stay
quiet if you pass through them in under 0.7 s.)

**Hold any button for a second** (alone, touching nothing else) and a **help card** says what it does in this mode and
on this page; let go and the card closes. See "Help cards".

RUN4 CTL is an independent project. It is not made or supported by M-VAVE.

## RUN4 mode

* **Keys** play notes (MIDI channel 1 by default) with velocity, whether or not the app is there. F3 is the lowest key;
  OCT- and OCT+ shift the keyboard by octaves (-3 to +3).
* **Buttons, encoders and MASTER** are sent to RUN4, which decides what they do on the current page.
* **Screen**: a header in the colour of the current RUN4 track, four tiles (name, value, bar), and a footer with the
  page dots, tempo, bar and beat, transport and octave. With a RUN4 that speaks protocol 1.2 the header shows where you
  are (e.g. TAPE > WEAR), the parameter in focus has a frame and its position (5/12) is in the footer; turning K1 to K4
  shows that parameter large for a moment (also when you change it in the app), then the four tiles come back.
* **Tape view.** On RUN4's Tape pages (with a RUN4 that speaks protocol 1.3) the screen shows the focused loop: its
  waveform with the loop region lit, the playhead (red while recording) and five level meters (T1 to T4 and the master,
  the focused track's in amber, a red cap when one clipped); the page's four values sit in a row under it, as in Synth
  mode. Turning a knob still shows its value large for a moment.
* **LEDs** show what RUN4 tells them to. Without the app, a held key lights itself.
* Keys still play without the app. If the app is not there, the screen says NOT CONNECTED and the notes go on.

### The FM-1 as RUN4's instrument

A RUN4 track can use the **FM-1 engine**: the track's notes (its sequencer, chords, arpeggiator, what you play) are
played by the FM-1's own FM synth while the FM-1 is in RUN4 mode, and come out of the FM-1's headphone / line output
and over the USB cable into RUN4. RUN4 chooses the voice (one of the FM-1's 8 factory or 32 user voices, or one of its
own) and a few live settings (algorithm, feedback, brightness, envelope speed, volume, vibrato, effects). Key pressure
makes the sound brighter. MASTER is the FM-1's volume for it, as for the iPad's audio playing through the FM-1.

The FM-1's keys keep going to RUN4 as before; RUN4 sends the track's notes back, so what you hear is what the track
plays. Switching to Synth mode (SEL + HOME) silences RUN4's FM-1 track and Synth mode plays its own sound; back in
RUN4 mode the track's sound returns unchanged. The settings live in RUN4: after switching the FM-1 off and on, or
replugging it, RUN4 sends them again.

## Synth mode

SEL + HOME enters it. The screen is black, grey and white with one warm accent for where you are: the header (the page,
the voice, a strip of the eight pages with this one lit), a band that shows the sound, the four knobs' tiles in a row
(the knob you turn lights for two seconds) and the footer (sub-page dots, tempo, bar and beat, transport, octave).

The band: on HOME, FX, GLO, ARP and SETUP the **scope** of the output (it settles to a dotted idle line when nothing
sounds); on EDIT the **six operators** (the patch's level as a line, what the newest note plays as a moving bar,
carriers underlined, the one you edit in the accent, its ratio under it); on ENV the **envelopes** (the one you edit
bright, the stage the newest note is in lit); on LFO its **wave** with a dot where it is; on SEQ the step grid.

**Sound.** Up to eight voices (the oldest, released one is stolen first, and fades out in 1.5 ms instead of stopping
dead). Since firmware 0.9.2 the FM-1 counts what a voice of the sound costs (its operators, the effects that are on)
and starts a voice only while the whole stays well inside the time it has: a heavy electric piano with every effect on
plays five or six voices at once, with the reverb alone six or seven, dry eight. Dexed's FM engine (Felucca's port)
with the 32 DX7 algorithms. 8 factory voices (F1 to F8) and 32 user voices (U01 to U32) in two banks. A chain of drive, chorus, tempo
delay and reverb: since firmware 0.7 a **plate** (Jon Dattorro's, as AMB-1 has it) with three characters, ROOM (short,
dark), PLATE (bright; a sound stored with the old SPRING gets it) and HALL (long and dark, 20 ms before it starts).
REVERB SIZE still goes from short to long (HALL about 0.6 to 2.7 s, ROOM 0.4 to 1.5 s) and REVERB keeps its level; the
shortest sizes ring a little longer than the old reverb's did.

**Controls.**

| Control | Use |
| --- | --- |
| Keys | play (OCT- / OCT+ shift them); velocity as set on the SETUP page; in a scale and as chords if set (see Scale and chord keys) |
| ALGORITHM encoder | the algorithm of the voice (1 to 32) |
| PRESETS encoder | the voice (F1 to F8, then the user voices that exist) |
| SELECT encoder | the operator (OP1 to OP6) on EDIT and ENV (the six lowest black keys pick it too); the step page on SEQ |
| K1 to K4 | the four values on screen |
| MASTER | volume |
| HOME, EDIT, ENV, LFO, FX, GLO, ARP, SEQ | the pages; press the same button again for its second sub-page |
| PLAY, REC | start / stop the sequencer; record into it (see Sequencer) |
| SEL + a key | transpose the sequence: it plays from that key (C4 = no transpose) |
| SEL + REC, SEL + PLAY | with RUN4 connected: RUN4's REC and PLAY (see RUN4 and Synth mode together) |
| SAVE | store the voice in its user slot, or in the first free one (on SEQ: an armed COPY TO / CLEAR, GENERATE, an armed REVERSE / MIRROR; a held step: clear its locks; SONG: insert / delete an entry) |
| SEL + EDIT | MUTATE: a random variation of the voice (see Mutate) |
| SEL + SAVE | undo the last MUTATE (again: redo it); on the SEQ page: undo the last pattern change (GENERATE, PHRASE, PASTE) |
| SEL + K1..K4 | MIDI learn: the next controller (CC) you move sets that tile (see MIDI controllers) |
| SEL + SEQ, held | FILL: steps with the condition FILL play, NOT FILL ones rest, while SEQ is held |

**Pages.**

| Page | Sub-page 1 | Sub-page 2 |
| --- | --- | --- |
| HOME (SYNTH) | BRIGHT, FEEDBACK, ENV SPEED, LEVEL | MORPH: MORPH, MODE, SET A, SET B (see Morph) |
| EDIT (OPERATOR n) | RATIO, FINE, LEVEL, DETUNE | MODE (ratio / fixed), VEL SENS, RATE SCALE, AMP MOD |
| ENV (ENVELOPE n) | RATE 1 to 4 | LEVEL 1 to 4 |
| LFO | SPEED, DELAY, PITCH MOD, AMP MOD | WAVE, PITCH SENS, LFO SYNC, OSC SYNC |
| FX | DRIVE, CHORUS, DELAY, REVERB (amounts) | DELAY TIME (1/8 to dotted 1/2), DELAY FEEDBACK, REVERB TYPE, REVERB SIZE |
| GLO (GLOBAL) | TRANSPOSE, TUNE, MIDI CH (OMNI or 1 to 16), BEND RANGE | VELOCITY amount, MUTATE amount (SMALL, MEDIUM, WILD); third sub-page KEYS: SCALE, ROOT, KEY MODE, MIDI IN |
| ARP | ARP on/off, MODE (up, down, up-down, random, played), RATE, OCTAVES | GATE, CHORD, INVERSION, SPREAD |
| SEQ (SEQUENCER) | six sub-pages (the fourth is SONG, then GENERATE and PHRASE) and two more views: see Sequencer | |

The delay is cut at 371 ms: at slow tempi the long divisions are shortened to it.

**Arpeggiator.** It turns held keys into an arpeggio and shares the sequencer's tempo (and a MIDI clock coming in).

### Morph

HOME's second sub-page (press HOME again) morphs between two snapshots of the sound. **SET A** and **SET B** (turn
K3 / K4 either way) take ten values as they are now: BRIGHT, FEEDBACK, ENV SPEED, LEVEL (HOME) and DRIVE, CHORUS,
DELAY, REVERB, DELAY FDBK, REVERB SIZE (FX). **MODE**: OFF (the sound's own values), KNOB (K1 **MORPH** sets where
between A, 0 %, and B, 100 %), or a slow drift there and back that takes 10 S, 20 S, 30 S, 45 S, 1 MIN, 90 S, 2 MIN,
3 MIN, 4 MIN or 5 MIN; MORPH then shows where it is, and turning it moves the drift on from there. While MODE is not
OFF and both snapshots are set, those ten values come from the morph (the HOME and FX tiles show the voice's own
values; turning one says MORPH IS ON). A step's parameter lock still wins over the morph. The snapshots and the mode
are kept with the settings. RUN4 can use the same on its FM-1 track (protocol: HV_PARAM 18..20).

### Scale and chord keys

**Scale** (GLO, third sub-page KEYS). SCALE: OFF (every key as it is), MAJOR, MINOR, DORIAN, PHRYGIAN, LYDIAN,
MIXOLYDIAN, HARMONIC MINOR, MAJOR and MINOR PENTATONIC, BLUES; ROOT: C to B. KEY MODE:

* **SNAP** (the default, and the recommended one): every key plays the nearest note of the scale (between two, the
  lower one). The keyboard keeps its layout, so chords and shapes you know still work; a key outside the scale doubles
  its neighbour. The keys of the scale light dim.
* **IN KEY**: the 16 white keys are the scale in a row, from the root in the third octave up (OCT- / OCT+ shift it); the
  black keys are silent. Runs and pentatonics fall under the fingers; the roots light dim.

MIDI IN: AS IS (the default: notes from USB, Bluetooth or the jack play as they come) or SCALE (snapped to the scale
too; chords are not added to MIDI in).

**Chords** (ARP, second sub-page). CHORD: OFF, MAJ, MIN, 7, MAJ7, MIN7, SUS2, SUS4, POWER, SCALE 3 and SCALE 4 (the
triad / seventh of the scale on that key, so one finger plays in key; with the scale OFF they are MAJ and 7).
INVERSION: ROOT, 1ST, 2ND, 3RD (the lowest notes go up an octave). SPREAD: CLOSE, OPEN (the second note up an
octave), WIDE (the lowest note down an octave). One key plays the chord (up to four notes); the arpeggiator gets all
of them, and live and step recording write all of them into the step.

### Mutate

**SEL + EDIT** makes a random variation of the sounding voice, by the MUTATE amount on GLO's second sub-page: SMALL
(levels, envelopes and detune move a little), MEDIUM (some ratios too, now and then the algorithm), WILD (anything,
the LFO too). It stays musical: ratios stay on whole and half numbers, the operators that you hear (carriers) stay
loud, attack and release stay reasonable, so the result always sounds. The screen says what changed, e.g. MUTATE ALG
OP235 LFO. **SEL + SAVE** goes back to the voice before it, and again forward (one step). Choosing another voice
forgets it. Nothing is written into a voice slot until you press SAVE (like any edit, the sounding voice is kept when
the FM-1 is switched off).

### Sequencer

16 patterns (P01 to P16) of 1 to 64 steps. A step holds up to four notes (a chord) or is a **TIE** (what the step
before plays goes on through it), and has its own velocity, gate and chance. Each pattern has its own length, rate
(1/8, 1/16, 1/16T, 1/32), swing, gate and velocity. PLAY starts and stops; the footer counts bars and beats.

**The grid** (SEQ page, REC off). The 16 white keys are the steps of the step page shown; the four lowest black keys
(or the SELECT encoder) show step page 1 to 4 (steps 1-16, 17-32, 33-48, 49-64).

| Do | What happens |
| --- | --- |
| tap a white key | the step on / off (a step switched on with nothing in it gets the last note entered, C4 at first; off keeps its notes) |
| hold a white key + a page button | K1 to K4 lock that page's sound parameters on the step (see Parameter locks) |
| hold a white key + play keys | the step gets those notes: the first replaces what it had, more keys while it is held make a chord (up to 4) |
| hold a white key + K1 / K2 / K3 / K4 | its NOTE (the whole chord moves), VELOCITY, GATE, CHANCE; PAT = the pattern's value |
| hold a white key + SEQ again | its CONDITION (K1; see Trig conditions); SEQ once more: back to NOTE .. CHANCE |
| hold a white key + EDIT | COPY the step (with SEL + another step held: every step from the first to the last of them) |
| hold a white key + GLO | PASTE what was copied from this step on (the pattern's length cuts it; locks come along) |
| a black key (1 to 4) | that step page; while playing the page follows the playing step until you pick one |

LEDs: a lit white key is a step that plays, a dim one is switched off with notes in it, the playing step is the other
way round. The black keys show the step pages (the one shown lit). While a step is held its notes light on the keys.
The screen shows the same: a cell per step with a tick for each note by pitch, a bar under the playing step (white)
and the cursor (red), the four step pages below, and the pattern's length, rate and swing.

**Step recording** (REC on, stopped, on the SEQ page). The keys play notes: the keys pressed together go into the
cursor step, and when all are let go the cursor moves on. **EDIT** writes a rest, **GLO** a TIE (each moves on). K1
moves the cursor, K2 moves the cursor step's notes, K3 is the length, K4 the tempo.

**Live recording** (REC on while playing, any page). What you play goes into the nearest step (the playing one, or the
next one when you are past its middle), with its velocity. Notes add up (overdub: up to four a step); a note held over
the following steps writes TIEs there. With the arpeggiator on, what it plays is recorded.

**The SEQ pages** (SEQ again for the next one):

| Sub-page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| 1 | PATTERN | LENGTH (1-64) | SWING (50-75 %) | TEMPO |
| 2 | RATE | GATE | VELOCITY | TRANSPOSE (-24 to +24) |
| 3 | STEP (cursor) | COPY TO | CLEAR | CLOCK (AUTO, OUT, INT) |
| 4 SONG | ENTRY | PATTERN | REPEATS (1-16) | SONG (OFF, ON) |
| 5 GENERATE | TYPE (EUCLID, SPARSE, PULSE, ARP, DRIFT) | its three settings (below) | | |
| 6 PHRASE | ROTATE | TRANSPOSE | REVERSE (SAVE) | MIRROR (SAVE) |

A pattern picked while another plays starts when that one comes to its end. **Copy:** turn COPY TO to the target
pattern and press SAVE. **Clear:** turn CLEAR right and press SAVE (either is forgotten after 8 seconds or on another
page). **Transpose** while playing: hold SEL and press a key (C4 = +0, F4 = +5).

**Parameter locks.** Hold a step on the grid and press a page button: its four tiles show what you can lock on that
step: **HOME** BRIGHT, FEEDBACK, ENV SPEED, LEVEL; **LFO** SPEED, DELAY, PITCH MOD, AMP MOD; **FX** DRIVE, CHORUS,
DELAY, REVERB, and FX again DELAY TIME, DELAY FDBK, REVERB SIZE; **SEQ** the step's own NOTE, VELOCITY, GATE, CHANCE
again. The ALGORITHM encoder locks the algorithm. Turning K1 to K4 locks the value on the step: the tile turns amber,
a white tick on its bar shows the voice's own value, and the step gets an amber mark on the grid. Turn it back to the
voice's own value and the lock goes; **SAVE** with the step held removes all its locks. Hold a step, then hold **SEL**
and press more steps to lock them all at once. When a step plays, its locks sound with it and stay until the next step
that plays (a step without locks plays the voice as it is); a TIE step's locks change the held note's sound; a step
that does not play (off, chance) changes nothing; STOP ends them. Locks are part of the pattern: COPY TO copies them,
CLEAR clears them. Up to six different parameters can be locked in one pattern (on any of its steps); a seventh says
LOCK LANES FULL. The locks change the sound only while the sequencer plays: saving the voice saves the voice's own
values. Notes you play on the keys while a locked step sounds have its sound too.

**Song.** The fourth SEQ sub-page chains patterns: up to 32 entries, each a pattern and how often it plays (1 to 16).
K1 moves through the entries (after the last one: NEW, where turning PATTERN adds one); K2 the entry's pattern; K3 its
repeats; K4 SONG on or off. SAVE inserts a copy of the entry; turning PATTERN below P01 arms DELETE and SAVE removes the
entry. The band shows the chain, the entry playing lit. With SONG on, PLAY (or a MIDI START with clock AUTO) plays the
chain from its first entry, each pattern to its end as a queued pattern change does, and starts over after the last;
with any clock (AUTO, OUT, INT). The grid shows the pattern playing (turn PATTERN to edit another one meanwhile) and
says SONG 2/5 x1/2 (entry, loop). With SONG off, patterns play as before. The chain is kept with the patterns.

**Trig conditions.** Hold a step and press SEQ again: K1 sets its CONDITION. ALWAYS (the default), **1:2** (the
first of every two loops of the pattern), **2:2** (the second), **1:4** (the first of every four), **FIRST** (only the
first loop after PLAY), **FILL** (only while FILL is held), **NOT FILL** (only while it is not). The loops count from
PLAY and again whenever another pattern (or song entry) takes over. A step with a condition has a notch at the bottom
of its cell. **FILL**: hold SEL and press SEQ; FILL is on while SEQ stays held (the grid says FILL). A TIE goes on
whatever its condition. Conditions are kept with the patterns (older patterns: ALWAYS).

**GENERATE** (fifth sub-page) fills the pattern when you press SAVE, in the scale of GLO's KEYS page (with SCALE OFF:
the minor pentatonic) from the last note entered on the grid (C4 at first):

| TYPE | K2 | K3 | K4 | What it does |
| --- | --- | --- | --- | --- |
| EUCLID | HITS | STEPS (LEN = the pattern's length) | ROTATE | HITS spread as evenly as can be over STEPS, repeated over the pattern; a hit keeps the notes its step had (an empty one gets the note), the others are switched off (their notes kept): a rhythm over what is there |
| SPARSE | DENSITY | RANGE (octaves) | SEED | notes of the scale here and there, the downbeats likelier |
| PULSE | EVERY | OFFSET | LENGTH | the note every n steps from OFFSET + 1, held over LENGTH - 1 TIEs |
| ARP | SHAPE (UP, DOWN, UP-DOWN, RANDOM) | RANGE | EVERY | the CHORD of the ARP page (OFF: the scale's seventh chord) arpeggiated, a note every n steps |
| DRIFT | DENSITY | MOVE (scale steps) | SEED | a random walk in the scale |

The same SEED gives the same pattern; turn it for another.

**PHRASE** (sixth sub-page): **ROTATE** moves every step (and its locks) a step later or earlier per detent;
**TRANSPOSE** moves every note a step of the scale (with SCALE OFF a semitone) per detent, refused if a note would
leave the keyboard; turn **REVERSE** or **MIRROR** and press SAVE: the pattern back to front (a note and its TIEs stay
together), or its pitches upside down (around the middle of their range, into the scale).

**Undo.** SEL + SAVE on the SEQ page undoes the last GENERATE, PHRASE or PASTE (a run of ROTATE or TRANSPOSE turns is
one change); again: redo. One level. Elsewhere SEL + SAVE stays MUTATE's undo.

**Clock.** AUTO (the default): a MIDI clock coming in (USB, Bluetooth or the jack) drives the sequencer and the
arpeggiator, its START and STOP start and stop the sequencer, and when the clock stops coming for half a second the
FM-1's own tempo takes over again. OUT: the own tempo, and MIDI clock, START and STOP go out over USB and Bluetooth.
INT: the own tempo, a clock coming in is ignored.

**Kept.** The patterns are stored with the voices: a few seconds after the last edit, only while the sequencer is
stopped (writing the flash stops the sound for a moment), and when you leave Synth mode or press SAVE.

**MIDI in.** In Synth mode USB, Bluetooth and the TRS jack play the synth: notes, sustain pedal, pitch bend, all notes
off, controllers (see MIDI controllers), on the channel set in GLOBAL (OMNI by default); snapped to the scale when
MIDI IN is SCALE. The notes of the keys are not sent anywhere in this mode. USB and the jack are **sample-accurate**:
each message plays at the point of the audio block where it arrived, one block (5.8 ms) later, instead of at the next
block's start (up to 5.8 ms of jitter before); a note starts at its exact sample, a note-off, a controller or a MIDI
clock within 0.73 ms. The same holds for RUN4's notes to the FM-1 engine in RUN4 mode. Bluetooth plays at once, as
before.

### MIDI controllers

**Default CCs** (on unless SETUP > MIDI LEARN turns them off), on Synth mode's MIDI channel:

| CC | Sets | CC | Sets |
| --- | --- | --- | --- |
| 1 (mod wheel) | LFO PITCH MOD | 76 | LFO SPEED |
| 7 (volume) | LEVEL | 78 | LFO DELAY |
| 12 | DRIVE | 91 | REVERB |
| 16 | MORPH | 92 | LFO AMP MOD |
| 71 | FEEDBACK | 93 | CHORUS |
| 73 | ENV SPEED | 94 | DELAY |
| 74 | BRIGHT | 64, 120, 123 | sustain, all notes off (as before) |

**MIDI learn.** Hold SEL and turn the knob of the tile you want (any Synth page but SEQ; on EDIT and ENV the operator
shown), then move a controller within 10 seconds: the screen says e.g. CC 21 > LEVEL. A learned CC answers on the
channel it was learned on and wins over the default map. SAVE while learning removes the tile's CC; any other button
ends learning. Up to 16; CCs 6, 32-63, 64 and 96-101 (data entry, the fine halves, the pedal, (N)RPN) and 120 and up
cannot be learned. Every controller **picks up**: it changes a value only once it has reached it (or crossed it), so
a knob at another position never makes the sound jump. The list is on SETUP > MIDI LEARN.

**DX7 patches.** Send a DX7 **32-voice bank** (SysEx, 4104 bytes) or a **single voice** (163 bytes) to the FM-1 over USB or
Bluetooth while it is in Synth mode, from a librarian or a DAW. A bank replaces the 32 user voices; a single voice goes
into the current user slot or the first free one. The screen says IMPORTED 32 VOICES (or 1 VOICE). A bad checksum or a
cut dump imports nothing. The voices are kept when the FM-1 is switched off.

**Not like a DX7.** The engine reads the patch parameters but has no DX7 operator switches, MIDI controllers other than
those above, microtuning or portamento. The factory voices are Felucca's own, not a DX7 ROM.

## AMB mode

AMB mode is AMB-1, Charles Vestal's generative ambient machine for the FM-1 (after Brian Eno's *Music for
Airports* and the echo chain of *Discreet Music*), in RUN4 CTL's look. It starts growing by itself 0.7 s after you
enter it. Four layers draw on one scale and one slowly drifting chord: the **pad** (four voices breathing on unequal
cycles, a Rhodes or sung voices), the **bells** (four tape loops of 13, 17, 23 and 29 beats, each with a few notes),
the **bass** (the chord's root, gliding, now and then the fifth) and the **pulse** (a soft thump and two hi-hats on
Euclidean patterns of 16, 12 and 7 sixteenths). A slow tide swings it between sparse and full; with EVOLVE the layers
take rests. Then a short echo, a long tape loop (5.6 s, two heads) and the plate.

| Control | Use |
| --- | --- |
| Keys | the scale: a lit key's note is in it. Press a key to take its note out, again to put it back (every octave of a note is one key). The root stays, and three notes at least |
| PLAY | the garden grows / rests (resting, it rings out slowly) |
| REC | FREEZE: the chord holds and the reverb rings on, the bells and the pulse go on above it; again: thaw |
| EDIT | the next chord, on the next bar (at once while resting) |
| HOME, ENV, ARP, LFO, SEQ, FX, GLO | the pages GARDEN, PAD, BELLS, BASS, PULSE, SPACE, KEY |
| SELECT | the previous / next page; with HOME held: the tempo |
| ALGORITHM | the scale: IONIAN, DORIAN, PHRYGIAN, LYDIAN, MIXOLYDIAN, AEOLIAN, the pentatonics (CUSTOM once you changed it with the keys) |
| PRESETS | the piece |
| K1 to K4 | the page's four values |
| OCT-, OCT+ | the bells an octave down / up (-2 to +2; the footer's OCT shows it) |
| SAVE | saves all eight pieces; **held 2 s**: the piece back to its recipe (saved with the next save) |
| SEL + HOME, SEL + GLO, SEL + REC / PLAY | the next mode, the SETUP page, RUN4's REC / PLAY (as in Synth mode) |

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| GARDEN (HOME) | TEMPO (30-120) | EVOLVE | MOTION | DRIFT (20 S to 5:00 a chord) |
| PAD (ENV) | LEVEL | SWELL | BRIGHT | COLOUR (RHODES, VOICES) |
| BELLS (ARP) | LEVEL | DENSITY (0 to 5 notes a loop) | TONE | OCTAVE |
| BASS (LFO) | LEVEL | OCTAVE | GLIDE | FIFTH |
| PULSE (SEQ) | LEVEL | HITS | SWING | HATS |
| SPACE (FX) | ECHO | ECHO TIME (1/4. to 4 BARS) | REVERB | SIZE |
| KEY (GLO) | ROOT | TUNE | MIDI OUT (OFF, CLOCK, NOTES) | SCALE |

**The pieces** (PRESETS): GARDEN (D Dorian, 72), DEPARTURES (C# Ionian, 60), VOICES (F Aeolian, 56), AFTERNOON
(G Lydian, 84), NIGHT BUS (A Aeolian, 96), SHORELINE (E pentatonic major, 64), GLASS (Eb pentatonic minor, 76), TIDES
(Bb Mixolydian, 70). Each remembers how you left it; TUNE and MIDI OUT are shared.

**The screen.** The header: the page, the piece, the seven pages as a strip, a dot while something is not saved. The
band: the twelve notes as a ring (C on top, the scale grey, the chord in the accent, the root framed, the chord's name
and the state inside: GROWING, RESTING, FROZEN, EXT CLOCK, WAKING); the four bell loops as rows of beats (a block is a
note, the playhead lit, in the accent when it rings); the three pulse rows; the four layers' levels (pad, bells, bass,
pulse); a line filling up on the way to the next chord. The footer: the tempo, bar and beat, the play triangle while
it grows, the red disc while frozen, the bells' octave.

**Kept.** All eight pieces, the piece playing, TUNE and MIDI OUT are kept in the FM-1: a few seconds after a change
while it rests (writing the flash interrupts the sound for a moment, so never while it grows), with SAVE, and when you
leave AMB mode. If you switch off in AMB mode, it starts in AMB mode.

**MIDI.** In: a MIDI clock (USB, Bluetooth, the jack) drives the garden's beat (EXT CLOCK), START grows it from the
top, STOP rests it; half a second without clock and its own tempo takes over. A note on channel 1 takes its note out
of the scale or puts it back, as a key does (not RUN4's notes for its FM-1 track: those are suspended here). Out
(KEY > MIDI OUT, to USB and Bluetooth): OFF (the default), CLOCK (clock, start and stop while it runs on its own
tempo), NOTES (also the pad on channel 1, the bells on 2, the bass on 3, the pulse on 10).

**With RUN4.** RUN4 sees a local mode, as with Synth mode: the link stays, SEL + REC / SEL + PLAY are RUN4's REC and
PLAY, and the garden goes to RUN4 over the USB cable (choose the FM-1 as the input; LOCAL OUT works too). RUN4 needs no
update for it.

## DUB mode

DUB mode is DUB-1, Santiago Begueria's generative dub techno for the FM-1 (a fork of AMB-1, release 1.2), in RUN4
CTL's look. It starts playing by itself 0.7 s after you enter it. Four layers draw on one scale and one chord that
moves now and then (or through the eight **chord slots** you write): the **chord** (a stab of four voices, Saws, Keys,
Organ, Pad, Bell, Tonewheel or Noise, struck off the beat through a resonant low-pass, most of it into the echo), the
**pings** (short FM blips on four loops of 13, 17, 23 and 29 eighths), the **bass** (a sub on the chord's root, on
one of five grooves) and the **drums** (a four-on-the-floor kick that ducks the rest, a shaker, an open hat, a rim, a
clap; one of five kits). Over it, the **arrangement**: eight-bar phrases, Full, Break (the kick and the bass out, the
filter building), Groove or Thin, with a fill before the kick comes back; MOTION sets how often it leaves Full. Then a
tape echo (to 5.6 s, two heads, a band-pass in its feedback, the feedback past 100 % held by a soft clip), a short
Echoplex, a granular cloud blooming into the plate, and vinyl crackle.

| Control | Use |
| --- | --- |
| Keys | the scale: a lit key's note is in it. Press a key to take its note out, again to put it back. The root stays, and three notes at least |
| PLAY | plays / rests (resting, the echo rings out) |
| REC | **tapped: DUB HOLD** (the echo and the cloud play what they hold, the chord holds; again: off). **Held: the effects grid** (below) |
| EDIT | **tapped: the CHORDS page** (again: back); **held** (half a second): the next chord, on the next bar |
| SEL | **tapped alone: PERFORM** on / off (below). Held with another button: SHIFT, as everywhere |
| HOME, ENV, ARP, LFO, SEQ, FX, GLO | the pages DUB, CHORD, TEXTURE, BASS, DRUMS, SPACE, GLOBAL. Pressed again: HOME the KEY page; ENV, ARP and LFO their layer's SOUND page, then its FM page, then back; SEQ the DRUM SOUND page, then the PATTERN page, then back. A sub-page's button blinks |
| SELECT | the previous / next page; with HOME held: the tempo |
| ALGORITHM | on a layer's pages: its voice (the drums: the kit); elsewhere the scale (the modes, CUSTOM once you changed it with the keys) |
| PRESETS | the piece |
| K1 to K4 | the page's four values |
| OCT-, OCT+ | the pings an octave down / up (-2 to +2; the footer's OCT shows it) |
| SAVE | saves all eight pieces; **held 2 s**: the piece back to its recipe (saved with the next save) |
| SEL + HOME, SEL + GLO, SEL + REC / PLAY | the next mode, the SETUP page, RUN4's REC / PLAY (as in Synth mode) |

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| DUB (HOME) | FILTER (the master filter: left SHUT, right OPEN, the middle off; the kick stays out of it) | EVOLVE | MOTION | DRIFT (2 to 64 BARS a chord, or HOLD) |
| KEY (HOME again) | TEMPO (90-140) | ROOT | MODE | SWING |
| CHORD (ENV) | LEVEL | STABS (1 to 6 a bar) | DECAY | CUTOFF |
| CHORD SOUND (ENV again) | ATTACK | COLOUR | RESO | ENV |
| CHORD FM (ENV a third time) | ALGO (VOICE: the voice's own, 1-8, 2-OP) | RATIO | FEEDBACK | MOD DEC (Keys and Bell only: the title says NOT FM otherwise) |
| TEXTURE (ARP) | PINGS (their level) | DENSITY | CLOUD | CRACKLE |
| PING SOUND / PING FM (ARP again) | TONE / ALGO | RATIO / RATIO | LENGTH / FEEDBACK | BEND / MOD DEC |
| BASS (LFO) | LEVEL | OCTAVE | GROOVE (DRONE, OFFBEAT, DOTTED, ROLLER, SYNC) | FIFTH |
| BASS SOUND / BASS FM (LFO again) | BITE / ALGO | SHAPE / RATIO | LENGTH / FEEDBACK | SLIDE / MOD DEC (FM: Punch, Rubber, Pluck, Growl) |
| DRUMS (SEQ) | LEVEL | HITS | SWING | DUCK |
| DRUM SOUND (SEQ again) | KICK TUNE | KICK LEN | PUNCH | HATS |
| PATTERN (SEQ a third time) | LANE (KICK, SHAKER, OPEN, RIM, CLAP) | ACCENT | CHANCE | PLAY (AUTO, PATTERN) |
| SPACE (FX) | ECHO | ECHO TIME (1/8 to 1 BAR) | FEEDBACK (45 % to 112 %) | REVERB |
| GLOBAL (GLO) | TUNE | MIDI OUT (OFF, CLOCK, NOTES) | | |
| CHORDS (EDIT tapped) | SLOT (1-8) | CHORD (EMPTY, MAJOR, MINOR, SUS2, SUS4, DIM, AUG) | TYPE (TRIAD, 6, 7, MAJ7, 9, 11, ADD9) | PLAY (RANDOM, PROG) |
| PERFORM (SEL tapped) | FILTER | DUB | ENERGY | SPACE |
| MIX (PERFORM, white 6 held) | CHORD | PINGS | BASS | DRUMS |

**The chords** (EDIT tapped). Eight slots: K1 picks one, a key sets its root (an empty slot becomes a minor chord on
it, and the slots start to play), K2 and K3 its chord and type, K4 PLAY: RANDOM (the scale's own chords, a weighted
walk that keeps coming home) or PROG (the slots in turn, a chord every DRIFT bars; an empty slot between two holds the
one before). The band shows the eight slots, the one playing lit.

**The pattern** (SEQ three times). The white keys are the lane's sixteen steps; a black key held with a white one
accents that step. The first edit takes over what AUTO was playing, and PLAY turns to PATTERN; K1 the lane, K2 how
much quieter the steps without an accent are, K3 the lane's chance, K4 back to AUTO.

**PERFORM** (SEL tapped alone; SEL lit). The keys from F3 up, in three sets with a dark key between them: **white 1-5**
the layers, a press each (CHORD, PINGS, BASS: their level to 0 and back to 100; KICK, HATS: muted and back), **black
1-5** the same layers soloed (more than one: they play together, the rest muted; a solo blinks), **white 6 held** the
MIX on the knobs, **white 7-11 held** the dub moves: THROW (every stab into the echo), OPEN (the filter pushed open),
SHUT (pushed shut), DROP (the kick and the bass out), ECHO (everything pushed into the echo). The knobs are the **master
actions**, each from -100 to +100 with the piece as it is in the middle: **FILTER** (as on the DUB page), **DUB** (more
into the echo, longer, darker), **ENERGY** (the notes, hits and stabs thin out or fill in; far down the next phrases
are Thin, far up Full with a fill), **SPACE** (the reverb, the cloud and the plate's size). They are for playing: never
saved, kept when you change the piece, back in the middle at power-on. The page buttons still work in PERFORM (the keys
stay PERFORM's); SEL tapped again leaves it (the moves let go, the mutes stay).

**The effects grid** (REC held; the band shows the sixteen names in four groups). The white keys from F3 up: THROW,
TIME X2, TAPE STOP, SWELL (the echo); LOW-PASS, HIGH-PASS, WAH, PHONE (a filter on everything); VERB, CRASH, FREEZE,
WASH (the space); STUTTER, GATE, SIREN, RISER (rhythm and noise). Each plays while its key is held, even after you let
go of REC. A REC press that played an effect, or was held past 0.3 s, is not a dub hold.

**The pieces** (PRESETS): BASIC (F Dorian, 122), CHAMBER (A Aeolian, 118), STEPPERS (D Dorian, 126), FOG (G Aeolian,
112), CONCRETE (C pentatonic minor, 130), NIGHT DRIVE (Bb Dorian, 124), PRESSURE (E Phrygian, 128), HAZE (Eb Ionian,
116). Each remembers how you left it, its ping loops too; TUNE and MIDI OUT are shared.

**The screen.** The header: the page, the piece, the seven pages as a strip, a dot while something is not saved. The
band: the twelve notes as a ring (the scale grey, the chord in the accent, the root framed, the chord's name and the
section inside: FULL, BREAK, GROOVE, THIN, or RESTING, DUB HOLD, EXT CLOCK, WAKING); the phrase's eight bars; the bar
as it plays, a row each for the stabs, the kick, the hats, the rim, the clap and the bass (dim when muted in PERFORM);
the four layers' levels and the kick's ducking; PERFORM and the move held; a line filling up on the way to the next
chord. On the PATTERN page the five lanes, on the CHORDS page the eight slots, with REC held the effects grid. The
footer: the tempo, bar and beat, the play triangle, the red disc in dub hold, the pings' octave. The keys light the
scale (the root bright); in PERFORM the layers heard, the solos, the moves and white 6; on the PATTERN page the lane's
steps; on the CHORDS page the slot's root; with REC held the grid.

**Kept.** All eight pieces (with their ping loops), the piece playing, TUNE and MIDI OUT: a few seconds after a change
while it rests, with SAVE, and when you leave DUB mode. If you switch off in DUB mode, it starts in DUB mode.

**MIDI.** As AMB mode: a MIDI clock drives it (EXT CLOCK; START plays from the top, STOP rests), a note on channel 1
takes its note out of the scale or puts it back. Out (GLOBAL > MIDI OUT): OFF (the default), CLOCK, NOTES (the chord
on channel 1, the pings on 2, the bass on 3, the drums on 10).

**With RUN4.** As AMB mode: a local mode for RUN4, SEL + REC / SEL + PLAY are RUN4's, the sound goes to RUN4 over the
USB cable, and RUN4 can pick DUB, show its pages and set its values (protocol 1.4 / 1.5, mode 3), the drum pattern's
steps and accents too: an edit in RUN4 takes over what AUTO was playing, as the first key does.

## Help cards

Hold a button alone for a second: a card over the screen says what it does, in this mode and on this page (on the
SETUP page: there). Let go and it closes. The card only explains; the press did what it always does (a page button
opened its page, PLAY started, REC armed or froze) and so does the release. It never comes, or goes at once, when
another button, a key or a knob is touched; that touch does what it always did. So nothing about the gestures that hold
a button changed: SEL with another button (SHIFT), a step held on the grid with a page button, SEL + SEQ held (FILL),
MIDI learn (SEL + a knob), RUN4's HOME double tap, DUB's EDIT held (the next chord), and AMB's and DUB's SAVE held 2 s (the card shows at 1 s and closes when the
piece goes back to its recipe). In DUB mode REC has no card (held, it shows the effects grid, which
names them), and SEL tapped alone turns PERFORM on or off; held for its card, its release does nothing more.

## RUN4 and Synth mode together

Switching modes keeps the link: after SEL + HOME the FM-1 says hello to RUN4 at once, so RUN4 knows which mode it is in
and takes over the screen again as soon as you are back in RUN4 mode. While RUN4 is connected in Synth mode (and in
AMB and DUB mode, see there):

* **SEL + REC** and **SEL + PLAY** are RUN4's REC and PLAY (the screen says RUN4 REC / RUN4 PLAY); REC and PLAY alone
  stay the synth's. So you can arm a RUN4 track and record the synth without touching the app.
* The FM-1's sound reaches RUN4 over the USB cable: the firmware is a USB audio input (its output, recordable). Choose
  the FM-1 as the input in RUN4. It does not play the computer's audio (firmware 0.9.1 on): the iPad keeps its own
  speaker or headphones for RUN4, and RUN4 can choose its output as usual.
* If RUN4 sends its MIDI clock to the FM-1, the sequencer and the arpeggiator follow it (clock AUTO).

### RUN4 picks the mode and plays the FM-1's controls (firmware 0.8)

From firmware 0.8 (protocol 1.4) RUN4 can choose the FM-1's mode itself and show the controls of Synth mode, AMB
mode and (firmware 0.9) DUB mode in the app: the FM-1 tells RUN4 its modes, and for the mode playing its pages and every parameter (name, range,
the text the FM-1 shows), so RUN4 needs no update for a new mode. When RUN4 switches the mode it is exactly as if you
pressed SEL + HOME, and the other way round: switch on the FM-1 and RUN4 follows. The link stays up through a switch
(no NOT CONNECTED screen, no new hello). Whatever you or RUN4 change shows on both: the FM-1 redraws its page and shows
its usual toast (GROWING, NEXT CHORD, SAVED ...), RUN4 gets the new value and the FM-1's status line (AMB: the piece,
the chord, the scale and whether it grows, rests or is frozen). The values RUN4 sets are saved as your own edits are.

**AMB into a RUN4 Tape.** Connect the FM-1 to the iPad with USB-C, pick AMB in RUN4, set the Tape track's input to the
FM-1 and arm it. Press PLAY / REC in RUN4: RUN4 sends its MIDI clock, AMB grows from the top in time with the take
(START) and rests at STOP, at RUN4's tempo. While RUN4's clock runs, its transport leads: RUN4 cannot grow or rest AMB
by itself then (it shows that AMB follows the transport), but the FM-1's PLAY button still works. Keep AMB's MIDI OUT
(KEY page) OFF while RUN4 sends its clock, and set SETUP > AUDIO > LOCAL OUT to USB ONLY if you do not want to hear
it twice. (The app's side of this needs RUN4's own update; the firmware's side is in 0.8.)

## Bluetooth

Bluetooth MIDI starts when the FM-1 is switched on. Connect from your computer or tablet (it shows up as a
Bluetooth MIDI device; the name comes from the stack and has not been checked on a device); no pairing is needed. In RUN4 mode the app talks to the FM-1 over
Bluetooth exactly as over USB; in Synth mode the MIDI in and DX7 dumps work over it. A USB-only package (without
Bluetooth) exists for those who want no radio. There is no switch for Bluetooth in the menus.

## Install

You need a Chrome or Edge browser (Web MIDI) and a USB cable.

1. **Back up first.** Installing replaces M-VAVE's firmware, including your presets. Save the presets you want to keep
   with M-VAVE's own tools before you start.
2. Connect the FM-1 and open the installer page of this firmware (`install/` in the site folder, published by the
   project owner). Follow its steps. The command-line installer in fm1-base (`tools/fm1_install.py`) does the same.
3. After the install the FM-1 restarts and shows RUN4 CTL.

**Going back to stock.** Install M-VAVE's own firmware package (V15) with the same installer: it accepts M-VAVE's
package. RUN4 CTL's own settings (SETUP page) and the user voices of Synth mode are not part of it and are simply gone
afterwards: take a USB backup first (see USB backup) to have them back later.

## Connect to RUN4

Plug the FM-1 into the iPad or iPhone (or connect over Bluetooth, or use a Mac session), open RUN4 and enable the FM-1 in its settings. The FM-1
sends HELLO every second until RUN4 answers; then the screen switches to the RUN4 page. Unplug and replug: it starts
over.

## Button map

The FM-1's controls keep the same meaning on every page. The **fixed layer**:

| Control | Use |
| --- | --- |
| ALGORITHM encoder (track) | choose the track |
| PRESETS encoder | choose the sub-page |
| SELECT encoder | choose the bank |
| K1 to K4 | the four values on screen |
| MASTER | RUN4's master level |
| PLAY, REC | transport |
| SAVE | Undo |
| OCT-, OCT+ | shift the keys by an octave (local); also sent to RUN4 |
| SEL | SHIFT: hold it for the second function of the other buttons |

The **page buttons** open the page of the same name: HOME, ENV, LFO, FX, EDIT, GLO, ARP, SEQ. What each page shows is decided
by RUN4 and by the track you are on. Press HOME twice quickly for RUN4's mode list (Tape, Poly, Disc, Perform, TOLF): SELECT
or PRESETS move, HOME or PLAY pick, any other page button or SAVE cancels.

## SETUP page

This page lives in the firmware, not in RUN4. Hold **SEL** and press **GLO** to open it (it works with or without the
app). K1 to K4 change the four tiles, any page button leaves it, SELECT (or PRESETS) turns through its four sub-pages:
the settings, VOICE BACKUP, AUDIO and MIDI LEARN. Nothing you do on it is sent to RUN4 and the settings are kept when
you power the FM-1 off.

| Tile | Values |
| --- | --- |
| K1 VELOCITY | SOFT, NORMAL, HARD, FIXED 100 |
| K2 AFTERTOUCH | OFF (default), POLY, CHANNEL |
| K3 LED LEVEL | OFF, LOW, HIGH (the dim LEDs) |
| K4 KEY CHANNEL | MIDI channel 1 to 16 (default 1) |

Turn **SELECT** (or PRESETS) for the second sub-page, **VOICE BACKUP**: RUN4 keeps a copy of the 32 user voices of
Synth mode (U01 to U32). It needs a RUN4 that speaks protocol 1.3, connected; it works in both modes.

| Tile | What it does |
| --- | --- |
| K1 BACKUP | turn it, then press **SAVE**: the 32 user voices go to RUN4, which keeps the last backup. The screen says BACKUP SENT |
| K2 RESTORE | turn it: the FM-1 loads RUN4's last backup and checks it; then **SAVE** writes it over the 32 user voices (RESTORED n VOICES). Leaving the page first changes nothing |
| K3 USER VOICES | how many user voices are stored |

Messages: **NO HOST** (RUN4 is not connected, too old, or did not answer), **NO BACKUP** (RUN4 has none yet), **BAD
CHECKSUM** (the backup arrived damaged: nothing is written). The backup holds the voices only, not the settings or the
sequencer's patterns: for everything, use the USB backup.

## USB backup

Everything the FM-1 keeps (the SETUP page with MIDI learn, Synth mode's settings with the morph, the 32 user voices,
the 16 patterns with their locks and the song, AMB mode's and DUB mode's pieces, the Bluetooth record) goes into one file over USB,
and back. Only the
knobs' calibration stays with the unit. Use fm1-base's backup page or its tool on a computer:

    python3 base/tools/fm1_backup.py backup run4ctl.bak --magic R4CB
    python3 base/tools/fm1_backup.py restore run4ctl.bak

A backup can be taken while the FM-1 plays (what is not saved yet is left out: stop first to have it). A restore needs
the sequencer stopped and, in AMB or DUB mode, the music resting (STOP FIRST), checks the whole file before anything
changes, and asks on the screen: RESTORE FROM USB? SEL yes, HOME no. Then everything is as in the file; the mode you are
in stays until the next power-on. Another firmware's backup does not restore. The VOICE BACKUP to RUN4 (above) stays
as it was.

**A backup of firmware 0.8 or older** (before DUB mode; 0.6 and older: before AMB mode too) is restored with RUN4 CTL's
own tool, which adds the parts it lacks as absent (those modes at their recipes; everything else as in the file):

    python3 tools/r4ctl_backup.py restore old.bak
    python3 tools/r4ctl_backup.py upgrade old.bak new.bak     (for fm1-base's backup page: then restore new.bak there)

The FM-1's own restore takes only backups of its own format (version 3 since firmware 0.9). The package without AMB
and DUB mode (see the README) makes and takes the same format, so a backup goes back and forth between the two.

## Screen mirror

The FM-1's screen and panel can be shown and played on a computer over USB (fm1-base's mirror page `web/mirror/` or
`base/tools/fm1_mirror.py`). It costs nothing until a viewer asks; the music always goes first, and a frame that did
not fit is drawn again.

## USB stems

The **RUN4 CTL STEMS** package (built from `app-stems/`) is the same firmware with six USB audio channels instead of
two: 1-2 the mix (as ever), 3-4 the dry voices (before chorus, delay and reverb), 5-6 the effects' return, all at the
same level (the mix is the dry plus the effects, before the output's soft ceiling). The normal package stays a stereo
USB device, which is what RUN4 and most apps expect; install STEMS only for a recorder that takes all six. 44.1 kHz.

Turn on for the third sub-page, **AUDIO**:

| Tile | What it does |
| --- | --- |
| K1 LOCAL OUT | ON (the default), or USB ONLY: while a computer (an iPad, a Mac) is connected over USB, the FM-1's sound goes to USB at full level (MASTER does not change the recording) and its headphone / line output and speaker stay silent, so RUN4 records it without hearing it twice. Listen on the iPad (the FM-1 does not play the computer's audio). Without a computer the FM-1 plays as usual |
| K2 USB CH | the USB audio channels of this package: 2 STEREO, or 6 STEMS (the RUN4 CTL STEMS package, see USB stems) |
| K3 LOAD SHED | NONE, or the overload guard's step (see below) |
| K4 2ND CORE | what DUB and AMB mode's reverb runs on: TEST (the FM-1's second core being tested, the first 1.5 s after power-on), ON (it passed: the reverb runs on it), FAILED (it read memory wrong in the test or later: everything on the first core until power-off), OFF (it did not start) |

The fourth sub-page, **MIDI LEARN**: K1 the binding (1 to 16), K2 shows its CC and channel and what it sets, K3
DEFAULT CCS on / off, K4 CLEAR (turn, then SAVE: that binding goes).

**Overload guard.** If the sound ever takes too long to compute (many voices, every effect, the FM-1 busy), the FM-1
makes it lighter a step at a time instead of crackling. Synth mode (and RUN4's FM-1 track): 1 the reverb without its
input diffusion, 2 six voices, 3 no chorus and no delay, 4 four voices. AMB mode: 1 the same for its reverb, 2 three
pad voices, 3 no hi-hats and no octave doublings of the bells, 4 two pad voices and four bells. DUB mode: 1 the same
for its reverb, 2 no cloud, 3 four pings instead of six and no crackle, 4 one saw a chord voice, 5 the chord on three
voices (the fourth fades out), 6 with one core only: no reverb (it fades out, and comes back from silence). A step is
taken quietly when the sound gets close to its time (85 %), more than one at once when it comes in all at once; only
when it actually ran late does it say CPU HIGH: SOUND LIGHTER, once. After 3 seconds of easy going a step comes back.
On a normal day it never shows.

**Second core.** Since firmware 0.9.2 DUB and AMB mode run their plate reverb on the FM-1's second core, beside the rest
of the sound, which gives them about a quarter more room. Some FM-1s read memory wrong now and then on that core, so it
is tested at every power-on, with the sound playing, before it gets the reverb, and checked again with every piece of
work it does; one wrong answer and the FM-1 runs on one core until it is switched off (SETUP > AUDIO, 2ND CORE, says
which). The sound is the same either way; the reverb comes 3 to 6 ms later than before.

Aftertouch is measured with the keys' own sensors. The velocity and pressure curves were set up without a device; if a
setting feels too soft or too hard, pick another curve.

## Help

* The keys do not play: check the USB cable, and that the host shows an input called RUN4 CTL FM-1.
* The screen says NOT CONNECTED: RUN4 has not answered. Open RUN4 and enable the FM-1.
* Everything about the wire protocol is in `docs/PROTOCOL.md`.

## Licence

GPL-3.0-only. Parts from fm1-base and felucca-bsp stay under their own licences (MPL-2.0); see `NOTICE`. The protocol
(`docs/PROTOCOL.md`) and its codec (`src/r4p.h`, `src/r4p.c`) are MIT (`LICENSE-MIT`), so other firmwares and devices
can speak RUN4 CTL.
