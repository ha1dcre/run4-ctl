# RUN4 CTL

RUN4 CTL is a firmware for the M-VAVE FM-1 with two modes:

* **RUN4 mode** turns the FM-1 into a hardware controller for the iOS app RUN4: the 27 keys, 14 buttons, 7 encoders
  and the MASTER knob are sent to the app over USB-MIDI or Bluetooth MIDI, and the app draws its pages and lights on
  the FM-1's 240 x 240 screen and LEDs. The FM-1 makes no sound in this mode.
* **Synth mode** turns the FM-1 into a standalone six-operator FM synthesizer with effects, an arpeggiator and a
  step sequencer (16 patterns of up to 64 steps, chords, ties, live and step recording). It also plays from MIDI in, so it works as a sound module for RUN4, TOLF or any MIDI source.

Switch between them with **SEL + HOME**. The FM-1 remembers the mode it was left in.

RUN4 CTL is an independent project. It is not made or supported by M-VAVE.

## RUN4 mode

* **Keys** play notes (MIDI channel 1 by default) with velocity, whether or not the app is there. F3 is the lowest key;
  OCT- and OCT+ shift the keyboard by octaves (-3 to +3).
* **Buttons, encoders and MASTER** are sent to RUN4, which decides what they do on the current page.
* **Screen**: a header in the colour of the current RUN4 track, four tiles (name, value, bar), and a footer with the
  page dots, tempo, bar and beat, transport and octave.
* **LEDs** show what RUN4 tells them to. Without the app, a held key lights itself.
* Keys still play without the app. If the app is not there, the screen says NOT CONNECTED and the notes go on.

## Synth mode

SEL + HOME enters it; the screen then shows the Synth pages in the same style as RUN4's: a header in the page colour
(orange for sound, teal for effects, blue for arpeggiator and sequencer), four tiles with a value and a bar, and a
footer with the page dots, tempo, bar and beat, transport and octave.

**Sound.** Eight voices (the oldest, released one is stolen first), Dexed's FM engine (Felucca's port) with the 32 DX7
algorithms. 8 factory voices (F1 to F8) and 32 user voices (U01 to U32) in two banks. A chain of drive, chorus, tempo
delay and reverb (room, spring, hall).

**Controls.**

| Control | Use |
| --- | --- |
| Keys | play (OCT- / OCT+ shift them); velocity as set on the SETUP page |
| ALGORITHM encoder | the algorithm of the voice (1 to 32) |
| PRESETS encoder | the voice (F1 to F8, then the user voices that exist) |
| SELECT encoder | the operator (OP1 to OP6) on EDIT and ENV (the six lowest black keys pick it too); the step page on SEQ |
| K1 to K4 | the four values on screen |
| MASTER | volume |
| HOME, EDIT, ENV, LFO, FX, GLO, ARP, SEQ | the pages; press the same button again for its second sub-page |
| PLAY, REC | start / stop the sequencer; record into it (see Sequencer) |
| SEL + a key | transpose the sequence: it plays from that key (C4 = no transpose) |
| SEL + REC, SEL + PLAY | with RUN4 connected: RUN4's REC and PLAY (see RUN4 and Synth mode together) |
| SAVE | store the voice in its user slot, or in the first free one (on SEQ: an armed COPY TO / CLEAR) |

**Pages.**

| Page | Sub-page 1 | Sub-page 2 |
| --- | --- | --- |
| HOME (SYNTH) | BRIGHT, FEEDBACK, ENV SPEED, LEVEL | |
| EDIT (OPERATOR n) | RATIO, FINE, LEVEL, DETUNE | MODE (ratio / fixed), VEL SENS, RATE SCALE, AMP MOD |
| ENV (ENVELOPE n) | RATE 1 to 4 | LEVEL 1 to 4 |
| LFO | SPEED, DELAY, PITCH MOD, AMP MOD | WAVE, PITCH SENS, LFO SYNC, OSC SYNC |
| FX | DRIVE, CHORUS, DELAY, REVERB (amounts) | DELAY TIME (1/8 to dotted 1/2), DELAY FEEDBACK, REVERB TYPE, REVERB SIZE |
| GLO (GLOBAL) | TRANSPOSE, TUNE, MIDI CH (OMNI or 1 to 16), BEND RANGE | VELOCITY amount |
| ARP | ARP on/off, MODE (up, down, up-down, random, played), RATE, OCTAVES | GATE |
| SEQ (SEQUENCER) | three sub-pages and two more views: see Sequencer | |

The delay is cut at 371 ms: at slow tempi the long divisions are shortened to it.

**Arpeggiator.** It turns held keys into an arpeggio and shares the sequencer's tempo (and a MIDI clock coming in).

### Sequencer

16 patterns (P01 to P16) of 1 to 64 steps. A step holds up to four notes (a chord) or is a **TIE** (what the step
before plays goes on through it), and has its own velocity, gate and chance. Each pattern has its own length, rate
(1/8, 1/16, 1/16T, 1/32), swing, gate and velocity. PLAY starts and stops; the footer counts bars and beats.

**The grid** (SEQ page, REC off). The 16 white keys are the steps of the step page shown; the four lowest black keys
(or the SELECT encoder) show step page 1 to 4 (steps 1-16, 17-32, 33-48, 49-64).

| Do | What happens |
| --- | --- |
| tap a white key | the step on / off (a step switched on with nothing in it gets the last note entered, C4 at first; off keeps its notes) |
| hold a white key + play keys | the step gets those notes: the first replaces what it had, more keys while it is held make a chord (up to 4) |
| hold a white key + K1 / K2 / K3 / K4 | its NOTE (the whole chord moves), VELOCITY, GATE, CHANCE; PAT = the pattern's value |
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

A pattern picked while another plays starts when that one comes to its end. **Copy:** turn COPY TO to the target
pattern and press SAVE. **Clear:** turn CLEAR right and press SAVE (either is forgotten after 8 seconds or on another
page). **Transpose** while playing: hold SEL and press a key (C4 = +0, F4 = +5).

**Clock.** AUTO (the default): a MIDI clock coming in (USB, Bluetooth or the jack) drives the sequencer and the
arpeggiator, its START and STOP start and stop the sequencer, and when the clock stops coming for half a second the
FM-1's own tempo takes over again. OUT: the own tempo, and MIDI clock, START and STOP go out over USB and Bluetooth.
INT: the own tempo, a clock coming in is ignored.

**Kept.** The patterns are stored with the voices: a few seconds after the last edit, only while the sequencer is
stopped (writing the flash stops the sound for a moment), and when you leave Synth mode or press SAVE.

**MIDI in.** In Synth mode USB, Bluetooth and the TRS jack play the synth: notes, sustain pedal, pitch bend, all notes
off, on the channel set in GLOBAL (OMNI by default). The notes of the keys are not sent anywhere in this mode.

**DX7 patches.** Send a DX7 **32-voice bank** (SysEx, 4104 bytes) or a **single voice** (163 bytes) to the FM-1 over USB or
Bluetooth while it is in Synth mode, from a librarian or a DAW. A bank replaces the 32 user voices; a single voice goes
into the current user slot or the first free one. The screen says IMPORTED 32 VOICES (or 1 VOICE). A bad checksum or a
cut dump imports nothing. The voices are kept when the FM-1 is switched off.

**Not like a DX7.** The engine reads the patch parameters but has no DX7 operator switches, MIDI controllers other than
those above, microtuning or portamento. The factory voices are Felucca's own, not a DX7 ROM.

## RUN4 and Synth mode together

Switching modes keeps the link: after SEL + HOME the FM-1 says hello to RUN4 at once, so RUN4 knows which mode it is in
and takes over the screen again as soon as you are back in RUN4 mode. While RUN4 is connected in Synth mode:

* **SEL + REC** and **SEL + PLAY** are RUN4's REC and PLAY (the screen says RUN4 REC / RUN4 PLAY); REC and PLAY alone
  stay the synth's. So you can arm a RUN4 track and record the synth without touching the app.
* The FM-1's sound reaches RUN4 over the USB cable: the firmware is a USB audio device (its output, recordable) and
  plays the computer's audio, as fm1-base's default. Choose the FM-1 as the input in RUN4.
* If RUN4 sends its MIDI clock to the FM-1, the sequencer and the arpeggiator follow it (clock AUTO).

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
afterwards.

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
app). K1 to K4 change the four tiles, any page button leaves it. Nothing you do on it is sent to RUN4 and the settings
are kept when you power the FM-1 off.

| Tile | Values |
| --- | --- |
| K1 VELOCITY | SOFT, NORMAL, HARD, FIXED 100 |
| K2 AFTERTOUCH | OFF (default), POLY, CHANNEL |
| K3 LED LEVEL | OFF, LOW, HIGH (the dim LEDs) |
| K4 KEY CHANNEL | MIDI channel 1 to 16 (default 1) |

Aftertouch is measured with the keys' own sensors. The velocity and pressure curves were set up without a device; if a
setting feels too soft or too hard, pick another curve.

## Help

* The keys do not play: check the USB cable, and that the host shows an input called RUN4 CTL FM-1.
* The screen says NOT CONNECTED: RUN4 has not answered. Open RUN4 and enable the FM-1.
* Everything about the wire protocol is in `docs/PROTOCOL.md`.

## Licence

GPL-3.0-only. Parts from fm1-base and felucca-bsp stay under their own licences (MPL-2.0); see `NOTICE`.
