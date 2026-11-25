<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Eugen Schott -->

# RUN4 CTL protocol v1 (minor 7)

This document and the reference codec (`src/r4p.h`, `src/r4p.c`) are under the MIT licence (`LICENSE-MIT`), so any
firmware or device can implement RUN4 CTL; the rest of RUN4 CTL stays GPL-3.0. See "Implementing RUN4 CTL in your own
firmware or device" at the end.

The FM-1 running RUN4 CTL is a terminal: it reports what the player touches and draws what the iOS app RUN4 tells it
to. This file is the contract between the two. The reference implementation is one codec, `src/r4p.h` / `src/r4p.c`
(plain C99, used by the firmware and by the native tests); `tools/fake_run4.py` is an independent encoder used to
cross-check it.

## Transport

* USB-MIDI. Keys are plain MIDI; everything else is SysEx.
* Frame: `F0 7D 52 34 <cmd> <payload...> F7`. `7D` is the non-commercial manufacturer ID, `52 34` is "R4".
* All bytes between F0 and F7 are 7-bit (0x00..0x7F). A frame with a byte >= 0x80 inside is dropped whole.
* 14-bit values are two bytes, MSB first, 7 bits each: `v = (b0 << 7) | b1`, `0..16383`.
* Strings: one length byte (number of **characters**, 0..24), then the characters. ASCII `0x20..0x7E` is sent as is. A
  Latin-1 character `c >= 0x80` is sent as `0x7F, c - 0x80` (two bytes, one character), so "TÓLF" is
  `04 54 7F 53 4C 46`. Anything else (controls, U+0100 and up) is sent as `?`. A longer string is cut at 24 characters.
  The device draws the glyph if its font has it, otherwise the base letter (the phase-0 fonts are ASCII only, so Ó
  is drawn as O). The device never formats values: all numbers shown arrive as text from the host.
* Maximum frame size: PAGE with four slots and every string at 24 characters, all escaped: **515 bytes** including F0
  and F7 (513 between them); with the protocol 1.2 trailer (a 24-character escaped path) **569 bytes** (567 between
  them), below the 640 bytes fm1-base's USB-MIDI SysEx buffer takes (`HOST_SX_MAX`, the same as
  the device's). The codec test asserts it (`tests/r4p_test.c`, "max PAGE frame"). Frames **from the device** are at most
  136 bytes (protocol 1.3: VOICE_DATA, BK_PART; 1.4 and 1.5: every frame, the encoder refuses a longer one), so one always fits
  whole in the Bluetooth MIDI out queue (64 packets of 3 bytes = 192) and in the USB SysEx ring (128 packets = 384); the
  device sends a frame only when it fits whole.
* Coexistence: fm1-base reserves SysEx under `7D 4C ..` (sub-IDs `43` remote control, `47` crash log, `48` crash history,
  `53` debug stats, `57` WiFi, `41` Bluetooth audio, `44` Bluetooth devices) and `7D 53 52 ..` (fm1-reader's screen
  reader); its updater uses `F0 22 24 35 ..` and a `F0 pack7(00 59 ..)` framing (the screen mirror reuses the latter). `7D 52 34` collides
  with none of them (checked in `base/device/*.c` and felucca-bsp's `chip/ota.c`). A parser that sees another header
  must return "not mine" without touching state: `r4p_parse_body` returns 0.

## Device to host

| cmd | name | payload |
| --- | --- | --- |
| 0x01 | HELLO | `proto_ver, fw_major, fw_minor, mode, [proto_minor]` (mode 0 = RUN4, 1 = a local mode: Synth or, since firmware 0.7, AMB - which one: 1.4's MODE; `proto_minor` since 1.1, absent = 0) |
| 0x02 | BTN | `id (0..13), state (1 down / 0 up)` |
| 0x03 | ENC | `id (0..6), delta` |
| 0x04 | POT | `value14` (MASTER) |
| 0x05 | SYNC_REQ | none: "send me everything" |
| 0x06 | LIST_PICK | `index, action` (action 0 = OK, 1 = CANCEL, 2 = MOVE) - protocol 1.1 |
| 0x07 | HV_STATE | `state, chan, voice, name(str)` - protocol 1.3, "Host voice" |
| 0x08 | VOICE_NAMES | `first, total, n` then `n x (used, name(str))` - 1.3 |
| 0x09 | VOICE_DATA | `idx, used` then 128 bytes when used - 1.3 |
| 0x0A | BK_PART | `k, used, 128 bytes` - 1.3, "Patch backup" |
| 0x0B | BK_END | `count, crc (5 bytes)` - 1.3 |
| 0x0C | BK_GET | `k` (0..31, 127 = the summary) - 1.3 |
| 0x0D | MODES | `cur, n, n x (id, flags, name(str))` - 1.4, "Local modes" |
| 0x0E | MODE | `mode, flags, hash (5 bytes)` - 1.4 |
| 0x0F | DESC_HEAD | `mode, hash (5), npages, nparams14, nstate, title(str), nstate x label(str)` - 1.4 |
| 0x10 | DESC_PAGE | `page, title(str)` - 1.4 |
| 0x11 | DESC_PARAM | `id14, page, kind, flags, step, lo, hi, def (s14 each), nlabels, name(str), unit(str), value (s14), text(str)` - 1.4 |
| 0x12 | DESC_LABELS | `id14, first, total, n, n x label(str)` - 1.4 |
| 0x13 | DESC_END | `mode, hash (5)` - 1.4 |
| 0x14 | PVAL | `mode, n, n x (id14, value s14, text(str))` - 1.4 |
| 0x15 | LSTATE | `mode, flags, n, n x text(str)` - 1.4 |
| 0x16 | LM_ERR | `mode, code, id14` - 1.4 |
| 0x17 | LVIEW | `mode, page, n, n x id14` (n 0..4) - 1.5, "The local mode's page" |
| 0x1B | SCENE_PICK | `scene` (0..127) - 1.6, "Protocol 1.6: the scene page" |

Button ids follow fm1-base `plat.h`: 0 FX, 1 SEL, 2 ENV, 3 LFO, 4 EDIT, 5 GLO, 6 HOME, 7 SAVE, 8 ARP, 9 SEQ, 10 PLAY,
11 REC, 12 OCT-, 13 OCT+. Encoder ids: 0 SELECT, 1 ALGO, 2 PRESETS, 3..6 K1..K4.

ENC delta: byte `0x40 + d`, so `0x40` = 0, `0x41` = +1, `0x3F` = -1, clamped to +-63 (`0x01..0x7F`; `0x00` is
invalid). Detents are summed per ~60 Hz frame, one ENC frame per encoder per frame; if a frame has more than 63, the
rest goes out in the next frame (`r4p_enc_add` / `r4p_enc_take`).

POT: sent when MASTER moves by 6 counts of 14 bits or more (at most every 20 ms), when it reaches 0 or 16383, and once
after HELLO_ACK.

Sending never blocks the firmware: a frame goes out only when the SysEx ring has room for it whole. BTN events wait in a
queue of 32 (oldest first; nothing is dropped, a change beyond 32 stays in the button state and is taken as soon as there
is room), ENC and POT go out only behind them (ENC deltas keep adding up meanwhile), HELLO and SYNC_REQ wait for room. If
the host stops reading, the keys still play and the screen still draws; when it reads again the frames arrive in order.

Keys: plain Note On / Note Off, by default channel 1, `note = 53 + key + 12 * octave` (key 0 = F3). The velocity comes
from fm1-base's touch sensing (`APP_TOUCH`, `lib/touch.c`: the 20 keys on the analog rows are measured, the 7 top keys
by a probe), shaped by the SETUP page's curve; the aftertouch, when switched on there, goes out as Poly Aftertouch
(`An note pressure`) or Channel Pressure (`Dn pressure`, the strongest held key). A note-off uses the note the key started with, so an octave change
while a key is held leaves no stuck note.

## Host to device

| cmd | name | payload |
| --- | --- | --- |
| 0x41 | HELLO_ACK | `proto_ver, [proto_minor]` (the minor since 1.1; absent = a 1.0 host) |
| 0x42 | PAGE | `title(str) track(str) r g b page_idx page_count bank_idx bank_count` then 4 x `name(str) kind value14 text(str)` |
| 0x43 | VALUE | `slot (0..3) value14 text(str)` |
| 0x44 | LEDS | 41 states, 2 bits each, packed 3 per byte LSB first, 14 bytes |
| 0x45 | STATUS | `bpm10 (14 bit), bar (14 bit), beat (0..15), flags` |
| 0x46 | TOAST | `text(str), duration (100 ms units)` |
| 0x47 | LIST | `title(str) r g b total first sel n` then `n x item(str)` - protocol 1.1 |
| 0x48 | FOCUS | `cursor item_idx14 item_count14` - protocol 1.2 |
| 0x49 | HV_ON | `on, chan` - protocol 1.3, "Host voice" |
| 0x4A | HV_VOICE | `kind` then `idx` / 128 bytes / 155 bytes - 1.3 |
| 0x4B | HV_PARAM | `1..21 x (id, value)` - 1.3 (ids 18..20: firmware 0.6) |
| 0x4C | NAMES_REQ | none - 1.3 |
| 0x4D | VOICE_GET | `idx` (0..39, 127 = the host voice's) - 1.3 |
| 0x4E | TAPE_WAVE | `track, label(str), n, n x peak` - 1.3, "Tape view" |
| 0x4F | TAPE_POS | `play14, in14, out14, flags` - 1.3 |
| 0x50 | METERS | `5 x level, clip` - 1.3 |
| 0x51 | BK_ACK | `status` - 1.3, "Patch backup" |
| 0x52 | BK_INFO | `count, crc (5 bytes)` - 1.3 |
| 0x53 | BK_DATA | `k, used, 128 bytes` - 1.3 |
| 0x54 | MODES_REQ | none - 1.4 |
| 0x55 | MODE_SET | `mode` - 1.4 |
| 0x56 | DESC_REQ | `mode` - 1.4 |
| 0x57 | VALS_REQ | `mode` - 1.4 |
| 0x58 | PSET | `mode, n x (id14, value s14)`, n 1..16 - 1.4 |
| 0x59 | LVIEW_SET | `mode, page` - 1.5 |
| 0x5C | SCENES | `total14, first, n, n x name(str)` (n 0..16, names of at most 8 characters) - 1.6 |
| 0x5D | SCENE_STATE | `active14, queued14` (a scene 0..127, or 16383 = none) - 1.6 |
| 0x5E | ROLL | `flags, lo, rows, steps, first14, total14, cur_col, cur_row, play, beat, label(str), n, n x (col, len, row, vel, nflags)` (rows 0: the roll ends) - 1.7, "Protocol 1.7: the piano roll" |

Protocol 1.2 adds optional trailers (see "Protocol 1.2: where you are"): PAGE may end with
`cursor item_idx14 item_count14 path(str)`, VALUE with `flags`.

* PAGE colour: r, g, b each 0..127; the device draws them x2. `kind`: 0 empty, 1 unipolar, 2 bipolar, 3 enum, 4 toggle.
  `value14` by kind: unipolar 0..16383 from the left; bipolar 8192 = centre; **enum: `(segment_count << 7) | index`**
  (the segment count is needed to draw the bar; 2..16 are drawn); toggle: 0 off, non-zero on. `text` is the finished
  value including its unit.
* VALUE replaces one slot's value and text; the device redraws that tile only.
* LEDS: entries 0..13 are the buttons (BTN order), 14..40 the keys (key 0..26). 0 off, 1 dim, 2 on, 3 blink (2 Hz).
  Entry 41, the padding in the last byte, is ignored.
* STATUS flags: bit 0 playing, bit 1 recording, bit 2 overdub. `beat` is 0-based; the device shows `bar.(beat+1)`.
* TOAST duration 0 means 1.5 s.

## Protocol 1.1: LIST

Version 1.1 is backwards compatible: `proto_ver` stays 1; the minor is one optional trailing byte of HELLO (what the
firmware speaks) and of HELLO_ACK (what the host speaks). A firmware only needs it to tell hosts apart (a host that
sent no minor is 1.0 and must not be sent a LIST); a 1.0 host that sees the extra HELLO byte ignores it if it reads
by the layout, and the 1.0 frames of the host are still parsed here (`tests/r4p_test.c`, "t_v11").

LIST shows a scrollable single-choice list on the whole screen (used for the HOME double-tap mode list: Tape, Poly,
Disc, Perform, TOLF; later kit and preset browsing). `title` is drawn in the header in the colour `r g b` (0..127, x2).
`total` is the number of items (0..127; **0 closes the list**), `sel` the selected index, and the frame carries the
window: the items `first .. first+n-1` (`n` at most 7, the rows the screen has). A frame is refused when `n > 7`,
`first + n > total` or `sel >= total` (total > 0); the longest is 405 bytes.

While a list is open the firmware keeps the buttons and encoders local (nothing is sent as BTN / ENC):

* SELECT or PRESETS turn: moves the selection (clamped, no wrap) and draws it at once. If the new index lies outside
  the window the host sent, the firmware sends `LIST_PICK index, MOVE` and expects a LIST frame for that window (until
  it arrives the selection is shown only when it is in the window).
* HOME or PLAY: `LIST_PICK sel, OK`, the list closes on the device.
* SAVE, ENV, LFO, FX, EDIT, GLO, ARP or SEQ: `LIST_PICK sel, CANCEL`, the list closes.
* The host may send a LIST with total 0 at any time to close it. A new HELLO_ACK or a cable replug closes it.
* Keys, MASTER and the SETUP page (SEL + GLO) keep working; the LIST_PICK frames go through the same ordered queue as
  BTN frames.

## Protocol 1.2: where you are

Version 1.2 lets the host show the player where a parameter lives (e.g. Tape > Wear > Failure), which one is in
focus, and the value being set, large. It is backwards compatible: `proto_ver` stays 1, the firmware's HELLO says minor
2, and a host that speaks 1.2 answers **HELLO_ACK `01 02`**. A host that answered 1.0 or 1.1 is never sent anything new
and sees no change in behaviour (the firmware's big value view below is off for it too). The firmware parses the 1.2
frames from any host.

**PAGE trailer.** After the four slots a PAGE may carry, all together or not at all:

| field | bytes | meaning |
| --- | --- | --- |
| `cursor` | 1 | the slot in focus, 0..3 (its tile gets a frame in the page colour); 127 = none |
| `item_idx14` | 2 | the focused parameter's position in the host's list of the page's parameters, 0-based |
| `item_count14` | 2 | that list's length; 0 = no position shown (then `item_idx14` must be 0) |
| `path` | str | where you are, e.g. `TAPE > WEAR` (up to 24 characters); empty = the title is shown |

Without the trailer (1.1) the page has no cursor, no position and no path. A PAGE is refused when the trailer is
partial, `cursor` is neither 0..3 nor 127, or `item_idx14 >= item_count14` with a count above 0. The device shows the
path in the header in place of the title (cut to fit the header beside the track pill) and the position as
`5/12` (`item_idx14 + 1` / `item_count14`) at the right of the footer's first row.

**FOCUS (0x48)** `cursor item_idx14 item_count14`: moves the focus without resending the page (same ranges as the PAGE
trailer). The device redraws the old and new focused tiles and the position only. The path stays as the last PAGE set
it.

**VALUE trailer.** A VALUE may end with one `flags` byte (absent = 0): bit 0 = **show big** (the player touched this
parameter in the app: the device shows the big value view for this slot). Other bits are 0.

**Big value view** (firmware-local, only with a 1.2 host, RUN4 mode, no LIST or SETUP open). When K1..K4 is turned,
the firmware frames that slot's tile at once (and sends the ENC as before). When a VALUE for that slot arrives within
1.5 s of the turn, or any VALUE with flags bit 0 arrives, the tile area shows the slot enlarged: the path (or title),
the slot's name, its value text in the large font and a wide bar of its kind. Each further VALUE for that slot
redraws it; 1.2 s after the last one the four tiles come back. A PAGE ends the view at once. Another slot's VALUE
only updates that slot (seen when the tiles come back), unless it qualifies itself (turned, or flags bit 0): then the
view switches to it. Everything is redrawn by region (no full-screen clear).

## Protocol 1.3

Version 1.3 adds four things: the **host voice** (the FM-1's own FM engine as an instrument of a RUN4 track, also in
RUN4 mode), the **voice list** (the FM-1's 40 voice names and their data), the **tape view** (a waveform, playhead and
meters drawn on the FM-1) and the **patch backup** (the 32 user voices kept by the host). It is backwards compatible:
`proto_ver` stays 1, the firmware's HELLO says minor 3 (4 since 1.4, 5 since 1.5), and a host that speaks 1.3 answers **HELLO_ACK `01 03`**.

* The device **sends** 1.3 frames (HV_STATE, VOICE_NAMES, VOICE_DATA, BK_PART, BK_END, BK_GET) only to a host whose
  last HELLO_ACK said minor 3 or more. A 1.0 / 1.1 / 1.2 host sees no change at all (its layout is never the tape
  layout, because only a host sends TAPE_WAVE; its SETUP > VOICE BACKUP says NO HOST).
* The device **takes** the 1.3 host frames from any host after its HELLO_ACK (as it does with 1.2's). A frame that
  breaks a rule below is dropped whole (counted as malformed), as before.
* Every 1.3 frame fits the limits above: from the host at most 177 bytes (TAPE_WAVE), from the device at most 136.
* The device's answers and its backup frames go out behind button events (BTN, LIST_PICK keep their order and come
  first), one 1.3 frame per pass of its main loop, each only when it fits whole. Among
  them: HV_STATE first, then VOICE_DATA, then VOICE_NAMES, then the backup.
* A new link (USB cable out and in, a new Bluetooth connection) clears everything 1.3 on the device: the host voice is
  off, owed answers are forgotten, a backup or restore under way ends silently, the tape view is gone. A new HELLO_ACK
  ends the tape view (the host sends it again after SYNC_REQ, with the page) and does **not** change the host voice.

### Host voice

RUN4's Poly engine "FM-1": the notes of a RUN4 track (its sequencer, keys, arpeggiator, chords) are played by the
FM-1's six-operator FM engine (the one Synth mode plays, 8 voices, the oldest released one stolen first, drive /
chorus / delay / reverb after it) **while the FM-1 is in RUN4 mode**, where it is otherwise silent. The host voice has
its own settings, separate from Synth mode's (Synth mode's sound and stored settings are never touched by it). They
live in RAM only: after a power-on, and after a new link, the host voice is off and back at its defaults (below).
**After every SYNC_REQ the host sends HV_ON, HV_VOICE and HV_PARAM again** (all three are idempotent).

**HV_ON (0x49)** `on, chan` - host to device.

| field | values |
| --- | --- |
| `on` | 1 = on, 0 = off |
| `chan` | the MIDI channel the host voice plays, 0..15 (= channel 1..16). Recommended: 15 (channel 16), away from the keys' channel (SETUP > KEY CHANNEL, default 1) |

On: from now on, in RUN4 mode, the notes on `chan` sound. Off: its sounding notes are released (their release and the
effects ring out) and further notes are ignored. An HV_ON with another channel releases the notes of the old one. The
device answers each HV_ON with HV_STATE. While off, the FM-1 in RUN4 mode makes no sound, as in 1.2.

**Notes: plain MIDI, not frames.** The host voice is played with ordinary MIDI channel messages on channel `chan`, over
the same link the protocol runs on (USB-MIDI, cable 0; or Bluetooth MIDI). Taken: Note On (velocity 1..127; velocity 0
is a Note Off), Note Off, Polyphonic Key Pressure (`An`), Channel Pressure (`Dn`), Pitch Bend (`En`, +- BEND RANGE
semitones), Control Change 64 (sustain, >= 64 down), 120 and 123 (all sound / all notes off: released). Everything else
on that channel (other CCs, Program Change) is ignored; voices change with HV_VOICE. Notes from the TRS MIDI jack never
play it (only the host's links do). Why MIDI and not frames: a note is one 4-byte USB-MIDI packet instead of a ~10-byte
SysEx frame; it does not go through the device's 648-byte SysEx input queue (and over Bluetooth its one-frame-at-a-time
assembler), so a chord or a fast sequence never drops; RUN4 can play it with its existing MIDI-out path, timestamps
and scheduling (CoreMIDI `MIDITimeStamp`, BLE-MIDI timestamps); and any MIDI app can play the FM-1 in RUN4 mode too.
A note is played at the next audio block (256 frames, 5.8 ms).

**No echo.** The device never forwards MIDI it receives: the host's notes are not sent back, do not light keys, and
are not counted as key presses. The FM-1's own keys keep sending their notes on the key channel to the host, as in 1.0,
and never play the host voice directly: RUN4 decides, by routing them to the track, and sends the track's notes back on
`chan`. (So a host must not thru the FM-1's key notes back on `chan` unless it means them to sound.)

**Pressure.** The larger of the key's own pressure (poly) and the channel pressure opens the modulators: it adds up to
AT DEPTH steps of BRIGHT at pressure 127 (a brighter, harder sound). AT DEPTH 0 switches it off.

**Tempo and volume.** The delay's time is in beats: the device takes the tempo from the host's STATUS (`bpm10` 400..2400;
others ignored). The host voice's level is VOLUME times the FM-1's MASTER (MASTER is still sent as POT, as in 1.0): the
same as the iPad's own audio playing through the FM-1.

**HV_VOICE (0x4A)** `kind` then the voice - host to device.

| kind | then | the voice |
| --- | --- | --- |
| 0 | `idx` (1 byte, 0..39) | one of the FM-1's stored voices: 0..7 the factory voices F1..F8, 8..39 the user voices U01..U32 (VOICE_NAMES tells which are used). An empty user slot: nothing changes |
| 1 | 128 bytes | a packed voice: the FM-1's store format, which is the 128-byte voice record of a DX7 32-voice bank (VMEM) |
| 2 | 155 bytes | a DX7 single voice (VCED): the 155 data bytes of a DX7 single-voice dump (`F0 43 0n 00 01 1B <155> cs F7`) |

Every byte is 7-bit (the frame rule); out-of-range fields are clamped to the DX7's ranges, the name's characters
outside 32..126 become spaces. The sounding notes are released, then the voice replaces the host voice's patch
(operators, envelopes, algorithm, feedback, LFO, name). The host voice's other settings (BRIGHT, VOLUME, the effects,
the rest of HV_PARAM's that are not "patch") stay. The device answers with HV_STATE (also when an empty slot changed
nothing). At most 10 a second.

**HV_PARAM (0x4B)** `id, value` pairs, 1..21 of them - host to device. No answer; applied at the next audio block
without retriggering notes. A frame with any unknown `id` or a `value` above its maximum is dropped whole. Send changes
only, coalesced to the latest value, at most 100 frames a second in all (put several pairs in one frame).

| id | name | wire value | means | default | kind |
| --- | --- | --- | --- | --- | --- |
| 0 | ALGORITHM | 0..31 | algorithm 1..32 | the voice's | patch |
| 1 | FEEDBACK | 0..7 | | the voice's | patch |
| 2 | BRIGHT | 0..127 | -64..+63 (64 = 0): the modulators' level, the brightness macro | 64 | engine |
| 3 | ENV SPEED | 0..127 | -64..+63 (64 = 0): all envelope rates faster / slower | 64 | engine |
| 4 | VOLUME | 0..100 | % | 80 | engine |
| 5 | VIBRATO | 0..99 | LFO pitch modulation depth (the voice's PITCH SENS scales it; 0 there = none) | the voice's | patch |
| 6 | TREMOLO | 0..99 | LFO amplitude modulation depth (the operators' AMP MOD sens scale it) | the voice's | patch |
| 7 | LFO SPEED | 0..99 | | the voice's | patch |
| 8 | AT DEPTH | 0..63 | pressure into BRIGHT, steps at full pressure | 32 | engine |
| 9 | BEND RANGE | 0..12 | semitones | 2 | engine |
| 10 | VEL SENS | 0..100 | % velocity to level | 100 | engine |
| 11 | TRANSPOSE | 0..48 | -24..+24 semitones (24 = 0) | 24 | engine |
| 12 | TUNE | 0..100 | -50..+50 cents (50 = 0) | 50 | engine |
| 13 | CHORUS | 0..100 | % | 20 | engine |
| 14 | DELAY | 0..100 | % (a ping-pong delay, cut at 371 ms) | 0 | engine |
| 15 | REVERB | 0..100 | % (room) | 20 | engine |
| 16 | DRIVE | 0..100 | % | 0 | engine |
| 17 | DELAY TIME | 0..5 | 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2, dotted 1/2 | 2 | engine |
| 18 | MORPH SET | 0..2 | 0: snapshot A, 1: snapshot B of BRIGHT, FEEDBACK, ENV SPEED, VOLUME, DRIVE, CHORUS, DELAY, REVERB (and the delay's feedback and the reverb's size) as they are now; 2: both cleared | none | engine |
| 19 | MORPH MODE | 0..11 | 0 off, 1 knob (MORPH sets it), 2..11 a slow drift A -> B -> A of 10, 20, 30, 45, 60, 90, 120, 180, 240, 300 s | 0 | engine |
| 20 | MORPH | 0..100 | where between A (0) and B (100); a drift goes on from there | 0 | engine |

"Patch" parameters edit the loaded voice (VOICE_GET 127 returns them; the next HV_VOICE replaces them); "engine"
parameters belong to the host voice and stay across HV_VOICE. The defaults hold after a power-on: the voice is F1.

**The morph (ids 18..20, a backwards-compatible addition; the protocol stays 1.3).** RUN4 CTL firmware 0.6 and later
(HELLO's `fw_major.fw_minor`) knows them; an older firmware drops a frame with any of them whole (the rule above), so
a host sends them only to 0.6+, or in a frame of their own. The host voice's morph is the same as Synth mode's (MANUAL,
"Morph"): while both snapshots are taken and MORPH MODE is not 0, the ten values come from between A and B, at control
rate (a block, 5.8 ms), and a drift moves between them by itself; HV_PARAM's own values for those ids are then the
base the snapshots were taken from, not what plays. The snapshots are the FM-1's RAM, like the rest of the host voice:
after a power-on, a replug or a SYNC_REQ the host sends, in this order, the A values and `18 = 0`, the B values and
`18 = 1`, the values it wants as base, then `19` and `20`. RUN4 needs a change to use them; without it nothing
changes (the morph stays off).

**HV_STATE (0x07)** `state, chan, voice, name(str)` - device to host (1.3 hosts only).

| field | values |
| --- | --- |
| `state` | 0 off, 1 sounding (on, RUN4 mode), 2 suspended (on, but the FM-1 is in Synth or AMB mode) |
| `chan` | the channel, 0..15 (as last set; 15 before any HV_ON) |
| `voice` | where the voice came from: 0..39 a stored voice, 127 the host's (HV_VOICE kind 1 / 2) |
| `name` | the voice's name, up to 10 characters, spaces at the end cut |

Sent after each HV_ON and HV_VOICE, when the FM-1 switches to Synth mode while the host voice is on (state 2, before
the HELLO with mode 1), and after each HELLO_ACK while it is on (state 1 in RUN4 mode, 2 in Synth mode).

**AMB mode (firmware 0.7).** The FM-1 has a third mode, AMB (a generative ambient machine, MANUAL "AMB mode"). Its
HELLO says mode 1, as Synth mode's (a 1.4 host learns which from MODE and can describe and control both, "Protocol
1.4"): for the host both are a local mode that sounds on its own, nothing to draw or sync, the host voice suspended (HV_STATE 2), SEL + REC / SEL + PLAY sent as BTN 11 / 10, the sound on the USB audio
input as ever. Nothing new on the wire; a host needs no change. In AMB mode a MIDI clock from the host drives the
garden (START grows it, STOP rests it), and a note on channel 1 changes its scale (unless it is the host voice's
channel: those notes are ignored there); its MIDI OUT is off unless the player sets it (clock, or notes on channels 1,
2, 3 and 10). SEL + HOME goes RUN4 -> SYNTH -> AMB -> DUB -> RUN4 (firmware 0.9): from Synth mode back to RUN4 a
1.0 .. 1.3 host sees three HELLOs (mode 1, 1, then 0).

**DUB mode (firmware 0.9).** A fourth mode, DUB (DUB-1's generative dub techno, MANUAL "DUB mode"), is a local mode as
AMB is, in every way above: HELLO mode 1, a MIDI clock drives it (START plays it from the top, STOP rests it), a note on
channel 1 changes its scale, MIDI OUT off unless the player sets it (clock, or notes on channels 1, 2, 3 and 10), SEL +
REC / SEL + PLAY sent as BTN 11 / 10. Nothing new on the wire: a 1.4 host learns it from MODES (id 3) and its
description.

**Synth mode.** When the player switches to Synth mode (SEL + HOME) Synth mode takes over: the host voice's notes are
cut, HV_STATE says 2, and Synth mode plays with its own voice and settings. What the host sends meanwhile (HV_ON,
HV_VOICE, HV_PARAM) is kept for the host voice and applied on return; the host's notes play Synth mode's own sound if
its MIDI CH (GLOBAL page) is OMNI or the same channel, as any MIDI in does there. Back in RUN4 mode the host voice sounds
again, unchanged, and after the HELLO_ACK the device says HV_STATE 1.

**Audio.** The host voice comes out of the FM-1's headphone / line output. The FM-1 is a USB audio input device in
both modes (fm1-base's `usb_audio_in`, `app/fm1.toml`): its output goes to the iPad over the cable (RUN4 can record or
monitor the track from the FM-1's USB input). **Since firmware 0.9.1 the FM-1 has no USB audio output**
(`usb_audio_out = false`): with one, iOS sent all the iPad's sound to the FM-1 and RUN4 could not choose another output;
without it the iPad keeps its own speaker or headphones for RUN4, and the FM-1 only records. RUN4 receives the host
voice alone (the FM-1's own render).

**CPU.** The host voice is Synth mode's engine at the same load (8 voices and the effects); RUN4 mode's screen costs less
than Synth mode's live band (the tape view below sends ~5.6 KB a frame, the scope ~18.5 KB). Measured in the host
simulator (`tests/scenarios/15_host_voice.txt`, `16_tape_view.txt`); not measured on a device.

### Voice list

**NAMES_REQ (0x4C)** - host to device, no payload. The device answers with five **VOICE_NAMES (0x08)** frames, `first`
0, 8, 16, 24, 32 in this order: `first, total (40), n (8)` then `n x (used, name(str))`, `used` 1 = a voice is there
(the factory voices always), 0 = an empty user slot (its name is empty). Names: up to 10 ASCII characters (32..126),
spaces at the end cut. Index = the stored voice's number for HV_VOICE kind 0. A NAMES_REQ while frames are owed starts
over. Rules for a parser: `1 <= n <= 8`, `first + n <= total`, `used` 0 or 1, names at most 10 characters.

**VOICE_GET (0x4D)** `idx` (0..39, or 127 = the host voice's sounding voice, its HV_PARAM patch edits in it) - host to
device. The device answers **VOICE_DATA (0x09)** `idx, used` followed by the 128-byte packed voice only when `used` is
1 (an empty slot: `idx, 0`, nothing after). 127 is always used. One VOICE_GET is answered at a time; a newer one
replaces an unanswered one.

### Tape view

Host to device, drawn in RUN4 mode (ignored in Synth mode), from any host after its HELLO_ACK. While the host has sent a
waveform, the host's page is drawn in Synth mode's layout: the header and footer as on every page (path, track, page
dots, position, tempo, transport), a band (x 4..235, y 34..115) with the waveform, the loop region, the playhead and
five meters, and the page's four tiles in one row under it (y 116..199, the tile in focus - the knob just turned, else
the PAGE cursor - in the accent). Black, greys and white with one amber accent; a recording playhead is red, a clipped
meter has a red cap. Without TAPE_WAVE the 1.2 layout is unchanged.

**TAPE_WAVE (0x4E)** `track, label(str), n, n x peak`

| field | values |
| --- | --- |
| `track` | the focused track, 0..3 (T1..T4): its meter is drawn in the accent |
| `label` | up to 24 characters over the waveform, e.g. `T1  LOOP 2  4 BARS` (empty: none) |
| `n` | 0..120 columns; **0 ends the tape view** (the four tiles come back) |
| `peak` | per column the absolute peak of that stretch of the loop, linear, 0..127 (127 = full scale; the host may normalise). The columns are spread over 184 pixels, mirrored around a centre line |

Send it when the view starts, when the focused track or loop changes, and while recording when the content changes
(at most 4 a second: each one redraws the whole band, ~38 KB to the panel).

**TAPE_POS (0x4F)** `play14, in14, out14, flags` - positions across the waveform, 0..16383 (0 = the start of the first
column, 16383 = the end of the last). `flags`: bit 0 playing, bit 1 recording (the playhead is red), bit 2 no loop
region (then `in` and `out` are ignored; send 0). Without bit 2 `in <= out`. Send at most 15 a second while playing,
and once at each change while stopped. A change of `in`, `out`, bit 1 or bit 2 redraws the band; a moving playhead
redraws only the few columns it leaves and enters.

**METERS (0x50)** `5 x level, clip` - T1, T2, T3, T4, master. `level` = 127 + 2 x dBFS, clamped to 0..127 (0 dBFS =
127, -12 dBFS = 103, -63.5 dBFS and below = 0); peak or RMS and the ballistics are the host's choice. `clip` bit i
(0..4) = that meter clipped; the host holds it as long as it wants the cap shown. At most 15 a second, changes only.

The device draws at most 25 frames a second; TAPE_POS and METERS only move the playhead's and the meters' columns
(measured in the simulator: ~5.6 KB a frame to the panel at 15 frames a second, ~11 us of drawing on the host). The
big value view (1.2), a toast, a LIST and the SETUP page cover the view and it comes back. It ends with TAPE_WAVE `n = 0`,
a new HELLO_ACK, a link change and a switch to Synth mode; a PAGE does not end it (the Tape pages keep it).

### Patch backup

The player keeps the FM-1's 32 user voices (U01..U32, Synth mode's two banks) in the host: SETUP (SEL + GLO), SELECT or
PRESETS to its second sub-page **VOICE BACKUP**, then K1 BACKUP (SAVE sends) or K2 RESTORE (fetches; SAVE writes). The
host stores the backup silently (no app UI) and keeps the last one that arrived whole and correct. Works in both modes.

**Parts.** A backup is 32 parts, `k` = 0..31 for U01..U32: `k, used, data` with `used` 1 = a voice is stored there and
`data` its 128-byte packed voice (the FM-1's store format = a DX7 bank's voice record, all bytes 7-bit), `used` 0 = the
slot is empty and `data` 128 zero bytes. The same layout is BK_PART (device to host) and BK_DATA (host to device).

**Checksum.** CRC-32 as zlib's `crc32` (reflected, polynomial 0xEDB88320, initial and final XOR 0xFFFFFFFF; the check
value of "123456789" is 0xCBF43926) over, for k = 0..31 in order, the 130 bytes `k, used, data[0..127]` of each part
(4160 bytes). On the wire (BK_END, BK_INFO) it is five bytes of 7 bits, the lowest first: `c & 7F, c >> 7 & 7F,
c >> 14 & 7F, c >> 21 & 7F, c >> 28` (the fifth is 0..15; a larger one makes the frame malformed).

**Backup (device to host).** After K1 and SAVE the device sends BK_PART k = 0..31 in order, then **BK_END (0x0B)**
`count (32), crc`. The host checks that it got parts 0..31 each once (a BK_PART 0 starts a new backup and drops an
unfinished one) and the CRC, and answers within 2 seconds **BK_ACK (0x51)** `status`: 0 = stored (it is now the host's
last backup), 1 = bad checksum or incomplete (the previous backup stays), 2 = could not be stored. The device shows
BACKUP SENT, BAD CHECKSUM or BACKUP NOT STORED; no BK_ACK within 2 s, or no 1.3 host at all: NO HOST.

**Restore (host to device, paced by the device).** After K2 the device sends **BK_GET (0x0C)** `127` and the host answers
**BK_INFO (0x52)** `count, crc` of its last backup (`count` 0 and crc 0 = it has none: the device shows NO BACKUP; a
count other than 32: BAD BACKUP). Then for k = 0..31 the device sends BK_GET `k` and the host answers **BK_DATA (0x53)**
with that part, exactly as it was stored. A BK_DATA for another part than the one asked for is ignored. Each request
waits 2 s for its answer and is sent three times in all; then NO HOST. When all 32 are in, the device computes the CRC
over them: if it differs from BK_INFO's, BAD CHECKSUM and nothing is written. Otherwise it shows SAVE: WRITE n (n = the
used parts) and waits: **SAVE** replaces both banks in the store (RESTORED n VOICES); leaving SETUP or K1 instead
forgets it. The host must not change the backup between its BK_INFO and the last BK_DATA. The device keeps the parts
in RAM until SAVE; a voice that sounds keeps sounding (the restore changes the stored voices, not the sounding one).

| frame | dir | layout | bytes |
| --- | --- | --- | --- |
| BK_PART 0x0A | device to host | `k (0..31), used (0/1), 128 bytes` | 136 |
| BK_END 0x0B | device to host | `count (0..32; a backup sends 32), crc (5)` | 12 |
| BK_GET 0x0C | device to host | `k` (0..31, or 127 = BK_INFO please) | 7 |
| BK_ACK 0x51 | host to device | `status` (0..2) | 7 |
| BK_INFO 0x52 | host to device | `count (0..32), crc (5)` | 12 |
| BK_DATA 0x53 | host to device | `k (0..31), used (0/1), 128 bytes` | 136 |

A host that does not take part (1.2) is never sent BK frames: the device says NO HOST at once. A backup is 33 frames
from the device, a restore 33 request / answer round trips.

## Protocol 1.4: local modes

Version 1.4 lets the host pick the FM-1's mode and remote-control its **local modes** (Synth, AMB, DUB, and any later one)
without knowing them in advance: the device lists its modes, switches on request, describes the playing local mode
(pages, parameters, their kinds, ranges, labels, values and the text it shows for them) and reports value changes and
a short status line. A host built on this document needs no update for a new mode or a new parameter; it caches a
description by its hash. First use: RUN4 picks AMB, and records it into a Tape over the USB audio input.

It is backwards compatible: `proto_ver` stays 1, the firmware's HELLO says minor 4 (5 since 1.5), and a host that
speaks 1.4 answers **HELLO_ACK `01 04`**. The device sends the 1.4 frames (MODES, MODE, DESC_*, PVAL, LSTATE, LM_ERR) only to a host whose
last HELLO_ACK said minor 4 or more; a 1.0 .. 1.3 host sees exactly what it saw before. The device takes the 1.4 host
frames from any host after its HELLO_ACK (a MODE_SET from a 1.3 host switches the mode, and the HELLO handshake follows
as for SEL + HOME); before the HELLO_ACK they are ignored.

### Values on the wire

* **s14**: a signed value, `v + 8192` as a 14-bit value (two 7-bit bytes, MSB first): -8192 .. 8191. `00 00` is -8192,
  `40 00` is 0, `7F 7F` is 8191.
* **id14**: a parameter's id, plain 14-bit: 0..16382 (16383 is never a parameter's id: protocol 1.5's NONE).
* **hash (5)**: 32 bits as five 7-bit groups, lowest first (as BK_END's CRC; the fifth group 0..15).
* Strings as everywhere (length byte, Latin-1 escape), with these maxima: mode names 12 characters, parameter names,
  page titles, labels, value texts, the description's title and the state texts 16, units 8. The reader refuses a
  longer string (the frame is malformed); the encoder cuts.
* Every 1.4 frame from the device is at most **136 bytes** (F0 and F7 included). Where a frame carries a variable
  number of entries (DESC_LABELS, PVAL), the device puts in as many as fit, up to the maximum below.

### Modes

| id | name | flags | what it is |
| --- | --- | --- | --- |
| 0 | RUN4 | AVAIL, CTRL | the controller: the host draws its pages (1.0 .. 1.3). No description |
| 1 | SYNTH | AVAIL, DESC | Synth mode, described (below) |
| 2 | AMB | AVAIL, DESC; in the package without AMB (`app-noamb`): **0** (listed, not available) | AMB mode, described (below) |
| 3 | DUB | AVAIL, DESC (firmware 0.9); in `app-noamb`: **0** (listed, not available) | DUB mode, described (below) |

Mode ids are stable; a later mode takes the next free id (3, 4, ...), up to 126. **Flags** (MODES, MODE): bit 0
**AVAIL** = this firmware has the mode, bit 1 **DESC** = it is described (DESC_REQ works), bit 2 **CTRL** = the
controller mode (the host draws: PAGE, LEDS, STATUS ...). Other bits 0. HELLO's `mode` byte stays as in 1.0: 0 in the
CTRL mode, 1 in any local mode.

**MODES_REQ (0x54)** - host to device, no payload. Answer: **MODES (0x0D)** `cur, n, n x (id, flags, name(str))`: the
mode playing and every mode the firmware knows, available or not, in id order (`1 <= n <= 8`).

**MODE_SET (0x55)** `mode` - host to device: switch to that mode, exactly as SEL + HOME to it would (held keys
released, the old mode stopped and its settings saved as at a switch on the device, AMB grows 0.7 s after it comes in,
the SETUP page, a LIST or a help card are closed). It is carried out at the next pass of the device's main loop.
Answers: MODE (also when the mode is already playing); `LM_ERR NOMODE` for an id that is not AVAIL here (the mode does
not change).

**MODE (0x0E)** `mode, flags, hash` - device to host: the mode now playing, its flags and its description's hash
(0 = no description: RUN4 mode). Sent to a 1.4 host:

* after each HELLO_ACK with minor >= 4, in every mode (in RUN4 mode after the SYNC_REQ and the POT);
* after every mode change, the player's (SEL + HOME) or the host's (MODE_SET);
* in answer to a MODE_SET, and after an `LM_ERR WRONGMODE`.

**The link stays across a mode change (1.4 host only).** With a host whose last HELLO_ACK said minor 4 or more the
device does not drop to "not connected" and sends no HELLO at a mode switch: it sends MODE. Into RUN4 mode it also
does what a HELLO_ACK does: SYNC_REQ and a POT (the host sends PAGE, LEDS, STATUS, the tape view and the host voice's
HV_ON / HV_VOICE / HV_PARAM again, as after any SYNC_REQ), HV_STATE 1 when the host voice is on. Out of RUN4 mode:
HV_STATE 2 (host voice on) and MODE; the host stops drawing. With a 1.0 .. 1.3 host a switch is the HELLO handshake as
before (section "Two modes").

### The description

**DESC_REQ (0x56)** `mode` - host to device: describe the mode playing. `mode` must be the one playing (else
`LM_ERR WRONGMODE` and a MODE), and it must have a description (else `LM_ERR NODESC`). The device answers with the
description, in this order, nothing else of 1.4 in between except LM_ERR, MODE and MODES:

1. **DESC_HEAD (0x0F)** `mode, hash (5), npages, nparams14, nstate, title(str), nstate x label(str)` - npages 0..32,
   nparams 0..1024, nstate 0..4: the labels of LSTATE's texts (AMB: PIECE, CHORD, SCALE, STATE).
2. **DESC_PAGE (0x10)** `page, title(str)` for page = 0 .. npages-1 in order.
3. For each parameter in the device's order: **DESC_PARAM (0x11)**, then, when it has labels, its **DESC_LABELS
   (0x12)** frames, `first` = 0, 8, 16 ... in order.
4. **DESC_END (0x13)** `mode, hash (5)`, then an LSTATE.

A DESC_REQ while a description goes out starts it over. A mode change ends it (MODE follows): a host drops what it has
of an unfinished description when a MODE or a new DESC_HEAD arrives. The host checks at DESC_END: npages pages,
nparams parameters, every parameter's `nlabels` labels, unique ids, the hash it worked out = DESC_HEAD's = DESC_END's
= MODE's; else it asks again.

**DESC_PARAM (0x11)**: `id14, page, kind, flags, step, lo s14, hi s14, def s14, nlabels, name(str), unit(str),
value s14, text(str)`.

| field | meaning |
| --- | --- |
| `id` | 0..16382, stable for the mode: the host may keep it (a host's mapping, a stored setting); never reused for another meaning. 16383 is refused (malformed): LVIEW's NONE |
| `page` | the page it sits on, 0..npages-1 |
| `kind` | 0 NUMBER, 1 ENUM, 2 TOGGLE, 3 ACTION, 4 MASK (below) |
| `flags` | bit 0 READONLY (shown, not set), bit 1 AUTO (it changes by itself: a clock, a drift, the generative engine), bit 2 BIPOLAR (draw it from the middle, 0 at the centre) |
| `step` | 1..127: a value the host sets is `lo + k x step` |
| `lo`, `hi`, `def` | the range and the default (`lo <= def <= hi`) |
| `nlabels` | how many labels follow in DESC_LABELS (0..127) |
| `name`, `unit` | e.g. `LEVEL`, `ct`. The unit is a hint for the host's own editor; what the device shows is `text` |
| `value`, `text` | the value now and the text the device shows for it (`39S`, `1 BAR`, `+10`, `GROWING`): the host shows `text`, it never formats a value of a mode it does not know |

| kind | values | labels | set by the host |
| --- | --- | --- | --- |
| NUMBER | `lo..hi` | 0 | any `lo + k x step` |
| ENUM | `lo..hi`, `hi - lo + 1 = nlabels` | label k = value `lo + k` | any value |
| TOGGLE | `lo` 0, `hi` 1 | 0, or 2 (label 0 = off, 1 = on) | 0 or 1 |
| ACTION | `lo` 0, `hi` 1, `def` 0, the value always 0 | 0 | **1 fires it** (0 is RANGE) |
| MASK | `lo` 0, `hi` = 2^nlabels - 1, nlabels 1..13 | label k = bit k | any bit pattern (the mode may refuse some: REFUSED) |

A frame that breaks these rules is malformed (dropped by the reader). The reader takes ENUM, MASK and TOGGLE's labels
as text for the host's UI (e.g. a list of pieces); ACTIONs are buttons; a READONLY parameter is shown only.

**The hash** (FNV-1a 32: start 0x811C9DC5, per byte `h = (h XOR b) x 0x01000193 mod 2^32`) runs over the description's
frames in the order they are sent, over these bytes of each (as encoded on the wire): DESC_HEAD from its cmd byte to
the end of the payload **without** its five hash bytes (cmd, mode, then npages ..); DESC_PAGE and DESC_LABELS from cmd
to the end of the payload; DESC_PARAM from cmd up to the end of `unit` (**without** `value` and `text`). DESC_END and
F0 / F7 are not in it. A result of 0 is sent as 1 (0 means "no description"). So the hash changes with anything static
in the description (a renamed parameter, a new label, another range) and not with the values; a host that has a
description with that hash (from MODE) may skip DESC_REQ and send VALS_REQ. The codec has it as `r4p_desc_hash_add`.

### Values and the status line

**VALS_REQ (0x57)** `mode` - host to device: the values of every parameter again (a host with the description cached).
Same checks as DESC_REQ. The device answers with PVAL frames for every parameter (but the ACTIONs) and an LSTATE.

A DESC_REQ or VALS_REQ **subscribes** the host: from then on, until the next mode change, HELLO_ACK or new link, the
device sends what changes. A host that never asks gets no PVAL or LSTATE.

**PVAL (0x14)** `mode, n, n x (id14, value s14, text(str))` - device to host, n 1..6: values that changed, whatever
changed them (the host's PSET, the device's knobs and keys, a piece change, the clock, a drift). The device looks for
changes at most every **40 ms (25 a second)**, also while a knob turns (the values in between go out as it turns, not
only the last), and sends only the parameters whose value differs from the one it last sent (or never sent), so each
parameter is told at most 25 times a second, the last value always, and a host's own PSET comes back once (its
**echo**, with the device's text). (Firmware with protocol 1.4 only: every 67 ms, 15 a second; the pace is the same
for every host.) A value the host sets to what it already was is not echoed. ACTIONs are never in a
PVAL. A pass that finds more than fits goes on in the next frames at once.

**LSTATE (0x15)** `mode, flags, n, n x text(str)` - device to host, n 0..4: the status line, its texts labelled by
DESC_HEAD's state labels. Sent after DESC_END and VALS_REQ, then when anything in it changes, at most every 40 ms.
`flags`: bit 0 PLAYING (growing / the sequencer runs), bit 1 FROZEN, bit 2 EXT CLOCK (a MIDI clock comes in and leads),
bit 3 WAKING (it will grow in a moment: AMB just came in), bit 4 RECORDING (Synth mode's sequencer records). Other bits 0.

**PSET (0x58)** `mode, n x (id14, value s14)` - host to device, 1..16 pairs: set parameters, fire actions, applied in
order. The whole frame is checked first: `mode` must be the one playing (else `LM_ERR WRONGMODE`, id 0, and a MODE),
the mode described (else NODESC); for each pair the id known (else UNKNOWN), not READONLY (else READONLY), the value in
`lo..hi` and on a step, an ACTION's value 1 (else RANGE). **One bad pair and no pair of the frame is applied** (the
LM_ERR names the first). Then each pair is applied; one the mode refuses in its state (REFUSED, CLOCK) is skipped with
its LM_ERR, the others are applied. No other answer: the changed values come back as PVAL. Send changes only,
coalesced to the latest value, at most 30 PSET frames a second in all (put several pairs in one frame); over Bluetooth
not in bursts (one frame at a time, see "Bluetooth MIDI").

**LM_ERR (0x16)** `mode, code, id14` - device to host: `mode` = the mode playing, `id` = the parameter concerned
(0 when none). At most four wait; more in a burst are dropped.

| code | name | when |
| --- | --- | --- |
| 1 | NOMODE | MODE_SET: no such mode, or not in this firmware (AMB and DUB in `app-noamb`) |
| 2 | WRONGMODE | DESC_REQ / VALS_REQ / PSET name another mode than the one playing (a MODE follows) |
| 3 | NODESC | DESC_REQ / VALS_REQ / PSET in a mode without a description (RUN4) |
| 4 | UNKNOWN | PSET: no parameter with that id |
| 5 | RANGE | PSET: outside `lo..hi`, off the step, an ACTION with a value other than 1 |
| 6 | READONLY | PSET: a READONLY parameter |
| 7 | REFUSED | PSET: in range but not now (AMB, DUB: a note mask without the root or with fewer than three notes; Synth: an empty user voice) |
| 8 | CLOCK | PSET: the transport (AMB's GROW, DUB's PLAY, Synth's PLAY) while a MIDI clock leads it (below) |
| 9 | BUSY | reserved |

### Sequences

* **Connect (1.4 host).** Device HELLO (minor 4; 5 since 1.5) -> host HELLO_ACK `01 04` -> device (RUN4 mode: SYNC_REQ, POT, then)
  MODE. In a local mode the host then sends DESC_REQ (or VALS_REQ if it has MODE's hash cached) -> description ->
  LSTATE -> PVAL / LSTATE as things change.
* **The host picks AMB.** MODES_REQ -> MODES; MODE_SET 2 -> MODE `02 03 hash`; DESC_REQ 2 (or VALS_REQ 2) -> ... AMB
  grows 0.7 s after it came in (LSTATE WAKING, then PLAYING; PVAL GROW 1). DUB the same way (MODE_SET 3; PVAL PLAY 1).
* **The player switches on the device** (SEL + HOME): MODE with the new mode; the subscription ends. Into RUN4 mode:
  SYNC_REQ, POT, MODE; the host resyncs as after any SYNC_REQ. Into a local mode: the host asks DESC_REQ / VALS_REQ.
* **Resync after SYNC_REQ.** A SYNC_REQ comes only in RUN4 mode (after a HELLO_ACK or a switch into it): the host
  sends the page, LEDS, STATUS and the host voice as in 1.3; nothing of 1.4 is owed then. In a local mode a host that
  lost track (a replug, its own restart) sends HELLO_ACK `01 04` -> MODE, then DESC_REQ / VALS_REQ.
* **A new link** (cable, Bluetooth): HELLO again; nothing is subscribed, a MODE_SET not yet carried out is forgotten.

Sizes: AMB's description is 57 frames, 1,707 bytes (the longest frame 78 bytes); DUB's 199 frames, 6,155 bytes (the
longest 76; `tests/scenarios/34_dub_modes.txt`); Synth mode's 187 frames, 5,751 bytes
(measured in the simulator, `tests/scenarios/31_modes.txt`). The device sends up to four 1.4 frames per pass of its
main loop as the link has room, behind the button events and the 1.3 answers (HV_STATE, then MODE / MODES / LM_ERR,
then VOICE_DATA, VOICE_NAMES, the backup, then the description, PVAL, LSTATE, and to a 1.5 host LVIEW). Over USB a description takes a few
frames of the main loop; over Bluetooth it is paced by the 64-packet queue (not measured on a device).

### AMB's description (mode 2)

8 pages: 0 PLAY, 1 GARDEN, 2 PAD, 3 BELLS, 4 BASS, 5 PULSE, 6 SPACE, 7 KEY (pages 1..7 are the device's pages, their
four knobs in knob order). 35 parameters; ids 0..27 are AMB's engine parameters (`src/amb/amb.h` P_*), 32..38 the
PLAY page's. State texts: PIECE (`GARDEN`), CHORD (`Em7`), SCALE (`D DORIAN`, `D CUSTOM`), STATE (`GROWING`,
`RESTING`, `FROZEN`, `EXT CLOCK`, `WAKING`). The pieces keep every value per piece (a piece change sends all of them).

| id | page | name | kind | range (def) | notes |
| --- | --- | --- | --- | --- | --- |
| 32 | PLAY | PIECE | ENUM | 0..7 (0) GARDEN DEPARTURES VOICES AFTERNOON NIGHT BUS SHORELINE GLASS TIDES | as PRESETS: the piece as it was left |
| 33 | PLAY | NOTES | MASK | 12 bits, bit k = pitch class k (C, C#, D, Eb ... B) (0xAB5) | the scale as the keys toggle it. REFUSED without the root or with fewer than 3 notes. Text `7 NOTES` |
| 34 | PLAY | GROW | TOGGLE, AUTO | RESTING / GROWING (1) | PLAY: grows / rests (the tails ring on). CLOCK while a MIDI clock runs (below). Text `WAKING` while it is about to grow |
| 35 | PLAY | FREEZE | TOGGLE | OFF / ON (0) | REC: the chord held, the plate rings on |
| 36 | PLAY | NEXT CHORD | ACTION | | EDIT: the next chord, on the next bar while growing |
| 37 | PLAY | SAVE | ACTION | | SAVE: the pieces into the store |
| 38 | PLAY | RESET PIECE | ACTION | | SAVE held: the piece back to its recipe (not saved until SAVE) |
| 18 | GARDEN | TEMPO | NUMBER | 30..120 (72) BPM | (a running MIDI clock's tempo leads) |
| 5 | GARDEN | EVOLVE | NUMBER | 0..100 (35) | |
| 15 | GARDEN | MOTION | NUMBER | 0..100 (50) | |
| 6 | GARDEN | DRIFT | NUMBER | 0..100 (25) | text 20S .. 5:00 |
| 0 | PAD | LEVEL | NUMBER | 0..100 (60) | |
| 13 | PAD | SWELL | NUMBER | 0..100 (60) | |
| 12 | PAD | BRIGHT | NUMBER | 0..100 (40) | |
| 22 | PAD | COLOUR | ENUM | RHODES VOICES (0) | |
| 1 | BELLS | LEVEL | NUMBER | 0..100 (65) | |
| 4 | BELLS | DENSITY | NUMBER | 0..100 (40) | |
| 14 | BELLS | TONE | NUMBER | 0..100 (50) | |
| 21 | BELLS | OCTAVE | NUMBER, BIPOLAR | -2..2 (0) oct | OCT- / OCT+ |
| 2 | BASS | LEVEL | NUMBER | 0..100 (45) | |
| 23 | BASS | OCTAVE | NUMBER, BIPOLAR | -1..1 (0) oct | |
| 24 | BASS | GLIDE | NUMBER | 0..100 (20) | |
| 25 | BASS | FIFTH | NUMBER | 0..100 (35) | |
| 3 | PULSE | LEVEL | NUMBER | 0..100 (40) | |
| 7 | PULSE | HITS | NUMBER | 0..100 (35) | |
| 26 | PULSE | SWING | NUMBER | 0..100 (0) | text 50% .. 75% |
| 27 | PULSE | HATS | NUMBER | 0..100 (50) | |
| 8 | SPACE | ECHO | NUMBER | 0..100 (45) | |
| 9 | SPACE | ECHO TIME | ENUM | 1/4. 1/2 1/2. 1 BAR 2 BARS 4 BARS (3) | |
| 10 | SPACE | REVERB | NUMBER | 0..100 (65) | |
| 11 | SPACE | SIZE | NUMBER | 0..100 (80) | |
| 19 | KEY | ROOT | ENUM | C C# D Eb E F F# G Ab A Bb B (2) | the scale moves with it |
| 16 | KEY | TUNE | NUMBER, BIPOLAR | -50..50 (0) ct | the player's, not the piece's |
| 17 | KEY | MIDI OUT | ENUM | OFF CLOCK NOTES (0) | the player's, not the piece's |
| 20 | KEY | SCALE | ENUM | IONIAN DORIAN PHRYGIAN LYDIAN MIXOLYD. AEOLIAN PENT MAJ PENT MIN CUSTOM (1) | 8 = CUSTOM: notes taken out or put in; setting 8 keeps the notes as they are |

Every value the host sets goes into the piece playing (or the player's TUNE / MIDI OUT) and is saved as the device's
own edits are (a few seconds later while resting; SAVE at once). The device redraws its page and shows the same
toast as for its own buttons (GROWING, NEXT CHORD, SAVED ...).

### DUB's description (mode 3)

20 pages: 0 PLAY, then the device's pages in the order SELECT and the page buttons reach them: 1 DUB, 2 KEY, 3 CHORD, 4
CHORD SOUND, 5 CHORD FM, 6 TEXTURE, 7 PING SOUND, 8 PING FM, 9 BASS, 10 BASS SOUND, 11 BASS FM, 12 DRUMS, 13 DRUM SOUND,
14 PATTERN, 15 SPACE, 16 GLOBAL, 17 CHORDS, 18 PERFORM, 19 MIX. 135 parameters. Ids 0..105 are DUB's engine
parameters (`src/dub/dub.h` DP_*); 128..137 the PLAY page's and PERFORM's mutes and solos; 140 SLOT, 143 LANE, 145..148
the master actions FILTER, DUB, ENERGY, SPACE (what the device keeps beside the pieces); 160..179 the drum pattern's
steps and accents; id + 256 is a second place of
the same value on another page (KEY's SWING, PERFORM's FILTER, the MIX page's four levels): a PVAL tells both when it
changes. Besides its four knobs a page may hold values the device sets otherwise: the layer's VOICE (ALGORITHM), the
pings' OCTAVE (OCT- / OCT+), the drums' KIT; the CHORDS page all eight slots (its K1 SLOT picks which two are on K2,
K3), the PATTERN page all five chances (K1 LANE picks) and every lane's steps and accents (its keys edit the lane K1
picks). Each lane's sixteen steps are two MASKs of eight, `<LANE> 1-8` and `<LANE> 9-16` (labels `1`..`8`,
`9`..`16`), its accents two more, `<LANE> ACC 1-8` and `<LANE> ACC 9-16`: id 160 + 4 lane + 0..3, the lanes KICK
SHAKER OPEN RIM CLAP. While PLAY is AUTO they tell the steps Auto plays (its shaker changes now and then: a PVAL); the
first write takes Auto's pattern over and turns PLAY to PATTERN, as the device's first key does. An accent written
leaves its step as it is (the device's black key + white key turns the step on too). Text: the eight steps, `X` set,
`-` not (`X---X---`). State texts: PIECE (`BASIC`), CHORD (`Fm7`), SCALE (`F DORIAN`, `F CUSTOM`), STATE (`FULL 3/8`, `BREAK 1/8`
.. the section and its bar while it plays, `EXT FULL 3/8` with a MIDI clock, `RESTING`, `DUB HOLD`, `WAKING`); flags
PLAYING, FROZEN (dub hold), EXT CLOCK, WAKING. The pieces keep every value per piece (a piece change sends all of
them); the master actions, SLOT, LANE, KICK, HATS and SOLO are the device's, kept across pieces, not saved.

| id | page | name | kind | notes |
| --- | --- | --- | --- | --- |
| 128 | PLAY | PIECE | ENUM | as PRESETS: the piece as it was left (its ping loops too) |
| 129 | PLAY | NOTES | MASK | the scale as the keys toggle it. REFUSED without the root or with fewer than 3 notes. Text `7 NOTES` |
| 130 | PLAY | PLAY | TOGGLE | PLAY: plays / rests. AUTO. CLOCK while a MIDI clock runs. Text `WAKING` while it is about to play |
| 131 | PLAY | DUB HOLD | TOGGLE | REC tapped: the echo and the cloud hold, the chord holds |
| 132 | PLAY | NEXT CHORD | ACTION | EDIT held: the next chord, on the next bar while playing |
| 133 | PLAY | SAVE | ACTION | SAVE: the pieces into the store |
| 134 | PLAY | RESET PIECE | ACTION | SAVE held: the piece back to its recipe (not saved until SAVE) |
| 145 | DUB | FILTER | NUMBER | the master filter: -50..-1 SHUT 100..2, 0 OFF, 1..50 OPEN 2..100. For playing: never saved |
| 5 | DUB | EVOLVE | NUMBER |  |
| 15 | DUB | MOTION | NUMBER |  |
| 6 | DUB | DRIFT | ENUM | 2 BARS .. 64 BARS, HOLD |
| 18 | KEY | TEMPO | NUMBER | (a running MIDI clock's tempo leads) |
| 19 | KEY | ROOT | ENUM |  |
| 20 | KEY | MODE | ENUM | 8 = CUSTOM: notes taken out or put in; setting 8 keeps the notes |
| 282 | KEY | SWING | NUMBER | the same value as DRUMS' SWING (26) |
| 0 | CHORD | LEVEL | NUMBER |  |
| 28 | CHORD | STABS | NUMBER |  |
| 13 | CHORD | DECAY | NUMBER |  |
| 12 | CHORD | CUTOFF | NUMBER |  |
| 22 | CHORD | VOICE | ENUM | ALGORITHM on CHORD's pages: SAWS KEYS ORGAN PAD BELL TONEWHEEL NOISE |
| 36 | CHORD SOUND | ATTACK | NUMBER |  |
| 37 | CHORD SOUND | COLOUR | NUMBER |  |
| 38 | CHORD SOUND | RESO | NUMBER |  |
| 39 | CHORD SOUND | ENV | NUMBER |  |
| 52 | CHORD FM | ALGO | ENUM | VOICE (the voice's own), 1..8, 2-OP; the FM pages act on FM voices only (the chord's KEYS, BELL; every ping; the bass's PUNCH .. GROWL) |
| 53 | CHORD FM | RATIO | NUMBER |  |
| 54 | CHORD FM | FEEDBACK | NUMBER |  |
| 55 | CHORD FM | MOD DEC | NUMBER |  |
| 1 | TEXTURE | PINGS | NUMBER |  |
| 4 | TEXTURE | DENSITY | NUMBER |  |
| 32 | TEXTURE | CLOUD | NUMBER |  |
| 29 | TEXTURE | CRACKLE | NUMBER |  |
| 33 | TEXTURE | VOICE | ENUM | ALGORITHM on TEXTURE's pages: BLIP MARIMBA GLASS WOOD DROP |
| 21 | TEXTURE | OCTAVE | NUMBER | OCT- / OCT+ |
| 40 | PING SOUND | TONE | NUMBER |  |
| 41 | PING SOUND | RATIO | NUMBER |  |
| 42 | PING SOUND | LENGTH | NUMBER |  |
| 43 | PING SOUND | BEND | NUMBER |  |
| 56 | PING FM | ALGO | ENUM |  |
| 57 | PING FM | RATIO | NUMBER |  |
| 58 | PING FM | FEEDBACK | NUMBER |  |
| 59 | PING FM | MOD DEC | NUMBER |  |
| 2 | BASS | LEVEL | NUMBER |  |
| 23 | BASS | OCTAVE | NUMBER |  |
| 24 | BASS | GROOVE | ENUM |  |
| 25 | BASS | FIFTH | NUMBER |  |
| 34 | BASS | VOICE | ENUM | ALGORITHM on BASS's pages: SUB PUNCH RUBBER PLUCK GROWL |
| 44 | BASS SOUND | BITE | NUMBER |  |
| 45 | BASS SOUND | SHAPE | NUMBER |  |
| 46 | BASS SOUND | LENGTH | NUMBER |  |
| 47 | BASS SOUND | SLIDE | NUMBER |  |
| 60 | BASS FM | ALGO | ENUM |  |
| 61 | BASS FM | RATIO | NUMBER |  |
| 62 | BASS FM | FEEDBACK | NUMBER |  |
| 63 | BASS FM | MOD DEC | NUMBER |  |
| 3 | DRUMS | LEVEL | NUMBER |  |
| 7 | DRUMS | HITS | NUMBER |  |
| 26 | DRUMS | SWING | NUMBER | text 50% .. 70% |
| 30 | DRUMS | DUCK | NUMBER |  |
| 35 | DRUMS | KIT | ENUM | ALGORITHM on DRUMS' pages: TIGHT DEEP PUNCH SOFT DUSTY |
| 48 | DRUM SOUND | KICK TUNE | NUMBER |  |
| 49 | DRUM SOUND | KICK LEN | NUMBER |  |
| 50 | DRUM SOUND | PUNCH | NUMBER |  |
| 51 | DRUM SOUND | HATS | NUMBER |  |
| 143 | PATTERN | LANE | ENUM | which lane the device's PATTERN page edits (its CHANCE on K3) |
| 104 | PATTERN | ACCENT | NUMBER |  |
| 99 | PATTERN | KICK CHANCE | NUMBER |  |
| 100 | PATTERN | SHAKER CHANCE | NUMBER |  |
| 101 | PATTERN | OPEN CHANCE | NUMBER |  |
| 102 | PATTERN | RIM CHANCE | NUMBER |  |
| 103 | PATTERN | CLAP CHANCE | NUMBER |  |
| 105 | PATTERN | PLAY | ENUM | AUTO / PATTERN (the steps: 160..179) |
| 160, 161 | PATTERN | KICK 1-8, KICK 9-16 | MASK | 8 bits: bit k = step k + 1 (+ 9); as the white keys with lane KICK. Text `X---X---` |
| 162, 163 | PATTERN | KICK ACC 1-8, KICK ACC 9-16 | MASK | 8 bits: the steps accented (a black key held + the white key) |
| 164..167 | PATTERN | SHAKER 1-8, SHAKER 9-16, SHAKER ACC 1-8, SHAKER ACC 9-16 | MASK | as the kick's |
| 168..171 | PATTERN | OPEN 1-8, OPEN 9-16, OPEN ACC 1-8, OPEN ACC 9-16 | MASK | as the kick's |
| 172..175 | PATTERN | RIM 1-8, RIM 9-16, RIM ACC 1-8, RIM ACC 9-16 | MASK | as the kick's |
| 176..179 | PATTERN | CLAP 1-8, CLAP 9-16, CLAP ACC 1-8, CLAP ACC 9-16 | MASK | as the kick's |
| 8 | SPACE | ECHO | NUMBER |  |
| 9 | SPACE | ECHO TIME | ENUM |  |
| 11 | SPACE | FEEDBACK | NUMBER | text 45% .. 112% (the loop's gain) |
| 10 | SPACE | REVERB | NUMBER |  |
| 16 | GLOBAL | TUNE | NUMBER | the player's, not the piece's |
| 17 | GLOBAL | MIDI OUT | ENUM | the player's, not the piece's: OFF CLOCK NOTES |
| 140 | CHORDS | SLOT | ENUM | which slot the device's CHORDS page edits (its CHORD and TYPE on K2, K3) |
| 88 | CHORDS | PLAY | ENUM | RANDOM / PROG (the slots in turn) |
| 401 | PERFORM | FILTER | NUMBER | the same value as DUB's FILTER (145) |
| 146 | PERFORM | DUB | NUMBER | -50..50: the echo's send and feedback. For playing |
| 147 | PERFORM | ENERGY | NUMBER | -50..50: density, hits, stabs, the phrases. For playing |
| 148 | PERFORM | SPACE | NUMBER | -50..50: reverb, cloud, the plate's size. For playing |
| 135 | PERFORM | KICK | TOGGLE | OFF / ON (1): muted (PERFORM's white 4) |
| 136 | PERFORM | HATS | TOGGLE | OFF / ON (1): muted (white 5) |
| 137 | PERFORM | SOLO | MASK | 5 bits: CHORD PINGS BASS KICK HATS soloed (black 1-5); text `NONE`, a name, `2 SOLO` |
| 256 | MIX | CHORD | NUMBER | the same value as CHORD's LEVEL (0); the MIX page: PERFORM, white 6 held |
| 257 | MIX | PINGS | NUMBER | the same as TEXTURE's PINGS (1) |
| 258 | MIX | BASS | NUMBER | the same as BASS' LEVEL (2) |
| 259 | MIX | DRUMS | NUMBER | the same as DRUMS' LEVEL (3) |
| 64..71, 72..79, 80..87 | CHORDS | ROOT 1..8, CHORD 1..8, TYPE 1..8 | ENUM | in the order ROOT 1, CHORD 1, TYPE 1, ROOT 2 ..; ROOT C .. B, CHORD EMPTY MAJOR MINOR SUS2 SUS4 DIM AUG (a chord written: PLAY becomes PROG), TYPE TRIAD 6 7 MAJ7 9 11 ADD9 |

Ranges, labels and defaults are in the description (`tools/fake_run4.py` DUB_PARAMS has the list as the device sends
it, checked in `tests/run_sim.sh`). Every value the host sets goes into the piece playing (or the player's TUNE / MIDI
OUT, or the device's own) and is saved as the device's own edits are; the device redraws its page and shows the toast
its buttons would.

### Synth mode's description (mode 1)

Built from Synth mode's own page table (`src/synth_ui.c` SP[]), so it is exactly what its pages edit. 14 pages:
0 VOICE, 1 SYNTH, 2 MORPH, 3..8 OP1..OP6, 9 LFO, 10 EFFECTS, 11 GLOBAL, 12 ARP, 13 SEQ; 143 parameters. State texts:
VOICE (`F4 BRASS SECT`), PATTERN (`PATTERN 1`), STATE (`PLAYING`, `STOPPED`, `EXT CLOCK`); flags PLAYING, RECORDING,
EXT CLOCK.

| ids | page | parameters |
| --- | --- | --- |
| 150, 151, 152 | VOICE | VOICE (1..40: F1..F8, U1..U32; text `F4 BRASS SECT`; an empty user slot is REFUSED), ALGORITHM (1..32), SAVE VOICE (ACTION: into this user slot or the first free one) |
| 0..3 | SYNTH | BRIGHT (-64..63), FEEDBACK (0..7), ENV SPEED (-64..63), LEVEL (0..100 %) |
| 4..7 | MORPH | MORPH (0..100 %, AUTO: a drift moves it), MODE (ENUM OFF KNOB 10 S .. 5 MIN), SET A, SET B (ACTIONs: the snapshot now) |
| 16 + 16 x op + j | OP1..OP6 | j 0..7 the OPERATOR page (RATIO, FINE, LEVEL, DETUNE, MODE, VEL SENS, RATE SCALE, AMP MOD), j 8..15 the ENVELOPE page (RATE 1..4, LEVEL 1..4); op 0..5 = OP1..OP6 |
| 112..119 | LFO | SPEED, DELAY, PITCH MOD, AMP MOD, WAVE, PITCH SENS, LFO SYNC, OSC SYNC |
| 120..127 | EFFECTS | DRIVE, CHORUS, DELAY, REVERB, DELAY TIME, DELAY FDBK, REVERB TYPE, REVERB SIZE |
| 128..133, 136..139 | GLOBAL | TRANSPOSE, TUNE, MIDI CH (0 = OMNI), BEND RANGE, VELOCITY, MUTATE; SCALE, ROOT, KEY MODE, MIDI IN |
| 140..147 | ARP | ARP, MODE, RATE, OCTAVES, GATE, CHORD, INVERSION, SPREAD |
| 153, 154 | SEQ | PLAY (TOGGLE STOPPED / PLAYING, AUTO; CLOCK while a MIDI clock leads), TEMPO (400..2400 = 40.0..240.0 BPM, unit `BPM/10`) |

Ranges, kinds and labels are in the description (the sequencer's steps, locks and patterns are not described).
Synth mode's `def` is the range's low end or 0: its values come with the voice and the settings, it has no default of
its own per parameter.

### Transport: RUN4's clock and the remote GROW / PLAY

AMB follows a MIDI clock from the host (USB or Bluetooth) as before: **START** grows from the top, **CONTINUE** grows
on, **STOP** rests, the ticks (F8, 24 a beat) set its tempo while they come (EXT CLOCK; half a second without one: its
own tempo again). The rule for 1.4: **while a clock runs, the clock has the transport.** "Runs" = a START or CONTINUE
came and no STOP since, and ticks still come (within ~0.6 s). Then a PSET of GROW (34) is refused with `LM_ERR CLOCK`
and changes nothing; the host shows that AMB follows its transport. Ticks without a START (a host that sends its clock
while stopped), or after a STOP: GROW from the host works, and a garden grown that way plays at the clock's tempo. The
player's PLAY button on the FM-1 always works (the player has the last word); the next START / STOP from the clock wins
again. Synth mode's PLAY (153) follows the same rule (refused while its sequencer follows a MIDI clock, clock AUTO).

**Recording AMB into a RUN4 Tape** (the intended use): RUN4 picks AMB (MODE_SET 2), sends its MIDI clock to the FM-1
over the same USB-C cable, and records the FM-1's USB audio input on the Tape track. RUN4's transport START starts
the recording and AMB grows from the top in time with it; STOP rests AMB (its tails ring out) - so a take starts on
the grid. Without a clock from RUN4 the host may grow and rest AMB with GROW. MIDI OUT on AMB's KEY page should stay
OFF while RUN4 sends a clock (MIDI OUT CLOCK sends AMB's own clock only while no clock comes in; NOTES sends its notes
on channels 1, 2, 3 and 10 to RUN4, which a host may route or ignore). SETUP > AUDIO > LOCAL OUT USB ONLY keeps the
FM-1's speaker silent while RUN4 records it.

## Protocol 1.5: the local mode's page

Version 1.5 tells the host which of the playing local mode's description pages the FM-1 shows and which parameters
its knobs K1..K4 hold there, and lets the host pick the page, so a host that shows the description's pages (as tabs,
say) and the FM-1 always show the same one. It is backwards compatible: `proto_ver` stays 1, the firmware's HELLO says
minor 5, and a host that speaks 1.5 answers **HELLO_ACK `01 05`**. Everything of 1.4 stays as it is. The device sends
LVIEW only to a host whose last HELLO_ACK said minor 5 or more; a 1.4 host sees exactly what it saw before (but for
1.4's faster pace, "Values and the status line": 40 ms, for every host). The device takes LVIEW_SET from any host
after its HELLO_ACK, as the 1.4 host frames; its LM_ERR and MODE answers go to a 1.4 host only, as 1.4's.

**LVIEW (0x17)** `mode, page, n, n x id14` - device to host: `mode` = the mode playing, `page` = the description page
(0 .. npages-1, DESC_PAGE's numbers) the device's screen shows now, then the description ids of the parameters on
K1..Kn, in knob order (K1 first), n 0..4. A knob in the middle without a described parameter is **16383 (NONE)**;
empty knobs at the end are left out (n smaller); a page whose knobs edit something not described has n = 0. 16383 is
never a parameter's id (DESC_PARAM refuses it). Every other id is a parameter of the description on that `page`. The
reader refuses (malformed) n above 4, a page above 31, an id past 14 bits, a length that does not match n.

Sent to a subscribed 1.5 host (DESC_REQ / VALS_REQ, "Values and the status line"):

* once after the description (after DESC_END and its LSTATE) and once after a VALS_REQ's answer (after its PVALs and
  its LSTATE);
* then whenever the page or its ids change, whatever changed them: the player's SELECT or page buttons, an operator
  change (Synth), the host's own LVIEW_SET. The device looks at the value pass's pace, at most every **40 ms**, never
  more than one LVIEW a look; the last page is always told. A change back and forth within 40 ms may not be told.
* again after an LVIEW_SET the device does not carry out now (below), so the host's tab goes back to the page shown.

Nothing while not subscribed; a mode change, a HELLO_ACK and a new link end it as the rest of 1.4's subscription (the
next DESC_REQ / VALS_REQ starts it again). An LVIEW goes out in 1.4's order behind LM_ERR, MODE and MODES and after
the description stream, PVAL and LSTATE ("Sizes" above).

**LVIEW_SET (0x59)** `mode, page` - host to device: show that description page, page 0..127. Checked as PSET: `mode`
not the one playing: `LM_ERR WRONGMODE` (id 0) and a MODE; a mode without a description (RUN4): NODESC; `page` >=
npages: `LM_ERR RANGE` (id 0). Otherwise the device shows the page at the next pass of its main loop, as its own page
buttons get there (the screen redrawn, a help card closed, MIDI learn left with LEARN OFF), and sends the LVIEW. A
page the device shows already, or a page that is none of the device's pages (AMB's PLAY, Synth mode's VOICE: page 0),
changes nothing and is not an error: no answer. While the player has the device's SETUP page open, or (Synth mode) holds
a sequencer step, the page does not change: the device sends the LVIEW of the page it shows. A MODE_SET carried out
in the same pass wins (an LVIEW_SET for the mode left is forgotten).

| code | when (LVIEW_SET) |
| --- | --- |
| 2 WRONGMODE | `mode` is not the mode playing (a MODE follows) |
| 3 NODESC | the mode playing has no description (RUN4) |
| 5 RANGE | `page` >= the description's npages |

**AMB's pages** (mode 2): the device's page v (GARDEN .. KEY, its page buttons HOME ENV ARP LFO SEQ FX GLO, SELECT
steps through them) is description page v + 1, its four knobs the four parameters the description puts on it, KEY's
SCALE as id 20 (P_MODE). Page 0 PLAY is never the device's page. LVIEW_SET p (1..7) shows page p - 1 as its button.

| page | K1..K4 ids |
| --- | --- |
| 1 GARDEN | 18 5 15 6 |
| 2 PAD | 0 13 12 22 |
| 3 BELLS | 1 4 14 21 |
| 4 BASS | 2 23 24 25 |
| 5 PULSE | 3 7 26 27 |
| 6 SPACE | 8 9 10 11 |
| 7 KEY | 19 16 17 20 |

**DUB's pages** (mode 3): the device's page is the description page of the same name (the order of DUB's description above), its
knobs these ids. Page 0 PLAY is never the device's page. LVIEW_SET p shows page p as the device's buttons get there:
DUB .. GLOBAL as their buttons, KEY, the SOUND, FM and PATTERN pages as their button pressed again; CHORDS as EDIT
tapped (EDIT again goes back to where it came from); PERFORM as SEL tapped (PERFORM on); MIX only while the player holds
white 6 in PERFORM: not now (the device sends the LVIEW of the page it shows, the host's tab goes back).

| page | K1..K4 ids |
| --- | --- |
| 1 DUB | 145 5 15 6 |
| 2 KEY | 18 19 20 282 |
| 3 CHORD | 0 28 13 12 |
| 4 CHORD SOUND | 36 37 38 39 |
| 5 CHORD FM | 52 53 54 55 |
| 6 TEXTURE | 1 4 32 29 |
| 7 PING SOUND | 40 41 42 43 |
| 8 PING FM | 56 57 58 59 |
| 9 BASS | 2 23 24 25 |
| 10 BASS SOUND | 44 45 46 47 |
| 11 BASS FM | 60 61 62 63 |
| 12 DRUMS | 3 7 26 30 |
| 13 DRUM SOUND | 48 49 50 51 |
| 14 PATTERN, lane l | 143 104 99+l 105 |
| 15 SPACE | 8 9 11 10 |
| 16 GLOBAL | 16 17 (n = 2) |
| 17 CHORDS, slot s | 140 72+s 80+s 88 |
| 18 PERFORM | 401 146 147 148 |
| 19 MIX | 256 257 258 259 |

**Synth mode's pages** (mode 1), from the same table as its description (`src/synth_lm.c` sl_ref). Page 0 VOICE is
never the device's page.

| the device shows | page | K1..K4 ids |
| --- | --- | --- |
| HOME, sub-page 1 (SYNTH) | 1 SYNTH | 0 1 2 3 |
| HOME, sub-page 2 (MORPH) | 2 MORPH | 4 5 6 7 |
| EDIT (OPERATOR) op o, sub-page s | 3 + o (OP1..OP6) | 16 + 16o + 4s + k |
| ENV (ENVELOPE) op o, sub-page s | 3 + o | 16 + 16o + 8 + 4s + k |
| LFO, sub-page s | 9 LFO | 112 + 4s + k |
| FX (EFFECTS), sub-page s | 10 EFFECTS | 120 + 4s + k |
| GLO (GLOBAL), sub-page 1, 2, 3 | 11 GLOBAL | 128 129 130 131; 132 133 (n = 2: K3, K4 empty); 136 137 138 139 |
| ARP, sub-page s | 12 ARP | 140 + 4s + k |
| SEQ (any of its sub-pages) | 13 SEQ | none (n = 0: its knobs edit steps, not described) |

(k = 0..3 the knob, s = 0, 1 the sub-page, o = 0..5 the operator, the one SELECT picks.) LVIEW_SET: 1 HOME's first
sub-page, 2 HOME's second; 3..8 operator p - 3 on EDIT's first sub-page, or, when the device shows EDIT or ENV already,
on that page and sub-page (only the operator changes); 9 LFO, 10 EFFECTS, 11 GLOBAL, 12 ARP, 13 SEQ, each on its first
sub-page, or on the sub-page it shows when the device is on that page already.

### Sequences (1.5)

* **Connect (1.5 host).** Device HELLO (minor 5) -> host HELLO_ACK `01 05` -> MODE; DESC_REQ (or VALS_REQ) ->
  description -> LSTATE -> LVIEW; then PVAL / LSTATE / LVIEW as things change.
* **The player turns SELECT in AMB** (GARDEN -> PAD): LVIEW `02 02 04 00 00 00 0D 00 0C 00 16`.
* **The host's tab BELLS**: LVIEW_SET `02 03` -> the device shows BELLS -> LVIEW `02 03 04 00 01 00 04 00 0E 00 15`.
  LVIEW_SET `02 03` again: nothing. LVIEW_SET `02 00`: nothing. LVIEW_SET `02 08`: LM_ERR `02 05 00 00`.
* **Synth mode, OP3 picked on the EDIT page**: LVIEW `01 05 04 00 30 00 31 00 32 00 33`.
* **DUB, EDIT tapped with slot 4 picked**: LVIEW `03 11 04 01 0C 00 4B 00 53 00 58`; the host's PERFORM tab: LVIEW_SET
  `03 12` -> PERFORM on -> LVIEW `03 12 04 03 11 01 12 01 13 01 14`.

Sizes: LVIEW is 9 to 17 bytes (F0 and F7 included), LVIEW_SET 8.

## Protocol 1.6: the scene page

RUN4's Sets (the app's scenes, up to 128) as a page of the FM-1: a grid of 16 scene numbers with short names, the white
keys 1..16 recalling the scene they stand for. Backwards compatible: `proto_ver` stays 1, the firmware's HELLO says
minor 6, and the host never sends SCENES or SCENE_STATE to a device that said a lower minor. A device that does not
know the frames ignores them (a frame of ours it cannot parse is dropped and counted); a host that does not know
SCENE_PICK ignores it. Nothing else changes: until SCENES arrives with `total` above 0 the FM-1 behaves exactly as before.

* **SCENES** (host to device): `total14` the number of scenes the host offers (0..128; 0 = none, the page is closed and
  not offered), `first` and `n` the names that follow (`n` 0..16, `first + n <= total`), each a `str` of at most 8
  characters (Latin-1, as everywhere); an empty name shows the number alone. One frame is one bank of 16, so the whole
  list is 8 frames at most. `total` is in every frame; the device keeps the names it has
  and drops those at or above `total`. A frame with `n` 0 changes the count only. Bluetooth: one frame at a time, spaced
  (see "The modes, and Bluetooth"). The names are the host's short forms; the device never edits them.
* **SCENE_STATE** (host to device): `active14` the scene that is playing and `queued14` the one the host will switch to,
  each 0..127 or 16383 for none. Sent after SCENES (a full sync, after SYNC_REQ) and at every change; a queued scene
  that the host reports as active is reported with `queued` 16383. Both are the host's truth: the device only
  anticipates (below).
* **SCENE_PICK** (device to host): `scene` 0..127, the white key pressed. The host recalls that scene by its own rules
  (RUN4 launches it as its own Set recall does; the FM-1 adds no quantisation, no timing of its own) and answers with
  SCENE_STATE. A pick of a scene that is not below `total` is never sent. The device never sends a pick in any other
  situation; nothing else is sent while the page is open that is not sent otherwise.

**The page on the device** (firmware-local, like SETUP). **SEL + SEQ** opens it in RUN4 mode, but only when the host's
HELLO_ACK said minor 6 or more and SCENES gave `total` above 0 (otherwise SEL + SEQ goes to the host as ever, as BTN).
The press of SEQ stays local; the SEL press was already sent. It opens on the bank of the active scene. The 16 **white
keys**, left to right, are scenes `bank * 16 + 1 .. bank * 16 + 16` (a key without a scene does nothing; no key plays a
note while the page is open). **OCT-** / **OCT+** (without SHIFT) change the bank, not the octave, and stay local (they
stop at the first and the last bank). Any page button (HOME ENV LFO FX EDIT GLO ARP SEQ) leaves and is not sent; SEL +
GLO (SETUP) and SEL + HOME (the next mode) leave and then do what they always do. PLAY, REC and SAVE are sent as BTN as
on any page. A host LIST, a new HELLO_ACK, a lost link or a mode switch close the page.

The display: the header "SCENES" and "BANK n/N" in the accent, 4 x 4 cells with the scene's number (large) and its
name; the active scene filled, a queued one with a blinking frame; empty cells where the bank has no scene. The key
LEDs: the active scene's key on, the queued one blinking, the other scenes of the bank dim, the rest off (the host's
key LEDs are not shown meanwhile). The device marks a scene it just sent as queued at once and clears the mark when
SCENE_STATE arrives, or after 4 s if none does.

Bytes, an example (40 scenes, the host's picks; the FM-1 opens the page and key 7 is pressed):

```
host -> FM-1  F0 7D 52 34 5C 00 28 00 10 05 "INTRO" 00 07 "VERSE A" ... F7    SCENES total 40, first 0, 16 names
host -> FM-1  F0 7D 52 34 5C 00 28 10 10 ... F7                                bank 2
host -> FM-1  F0 7D 52 34 5D 00 03 7F 7F F7                                    SCENE_STATE active 3, queued none
FM-1 -> host  F0 7D 52 34 1B 06 F7                                             SCENE_PICK 6 (scene 7)
host -> FM-1  F0 7D 52 34 5D 00 03 00 06 F7                                    queued 6 (the host is switching)
host -> FM-1  F0 7D 52 34 5D 00 06 7F 7F F7                                    active 6
```

SCENE_PICK is 7 bytes, SCENE_STATE 10, SCENES 10 to 154 (at most 282 with names of accented letters only, far below 640).

## Protocol 1.7: the piano roll

A window of the pattern the host shows (RUN4: a Poly track's pattern, TÓLF's steps) drawn as a piano roll: a key
column, pitch rows, step columns with beat and bar lines, the notes as bars, the cursor cell and the playhead.
Backwards compatible: `proto_ver` stays 1, the firmware's HELLO says minor 7, and the host never sends ROLL to a device
that said a lower minor (it shows the page's four tiles there, as before). A device that does not know ROLL ignores it.
The device **only draws**: there is no new device frame. The cursor, the window, the notes and every edit are the
host's; the player moves and edits with the knobs, keys and buttons, which reach the host as BTN / ENC / note messages
as on every page (RUN4: K1 the cursor's step, K2 its pitch, K3 the length, K4 the velocity of the note under it, a white
key sets or deletes a note on that step at the cursor's pitch; the app's FM-1 manual has the details).

**ROLL (0x5E)** `flags, lo, rows, steps, first14, total14, cur_col, cur_row, play, beat, label(str), n, n x note`,
a note being `col, len, row, vel, nflags`

| field | values |
| --- | --- |
| `flags` | bit 0 the pattern plays, bit 1 it records (the playhead and the label red); other bits 0 |
| `lo` | the pitch (MIDI note) of row 0, the lowest row; `lo + rows - 1 <= 127` |
| `rows` | 1..24 pitch rows, drawn from the bottom up; **0 ends the roll** (then `steps` 0, `n` 0, `cur_col`, `cur_row` and `play` 127; the page's own layout comes back) |
| `steps` | 1..32 step columns in the window |
| `first14` | the pattern's step at the window's first column (0-based) |
| `total14` | the pattern's length in steps; columns at or past it are drawn shaded |
| `cur_col` | the cursor's column, `< steps`, or 127 (none) |
| `cur_row` | the cursor's row, `< rows`, or 127 (none: only its column is lit) |
| `play` | the playhead's column, `< steps`, or 127 (none, or outside the window) |
| `beat` | steps per beat, 0..32: a beat line every `beat` steps, a bar line every 4 beats (counted from step 0 of the pattern); 0 none |
| `label` | up to 24 characters over the roll, e.g. `POLY 2  C4-C5` (empty: none) |
| `n` | 0..64 notes inside the window |
| note `col`, `len` | the column the bar starts on and its length in columns: `len >= 1`, `col + len <= steps` (the host cuts a note to the window) |
| note `row` | its row, `< rows` |
| note `vel` | 0..127, drawn as the bar's brightness |
| note `nflags` | bit 0 IN (it began before the window), bit 1 OUT (it goes on after it), bit 2 MUTED (grey), bit 3 SEL (the note under the cursor: outlined), bit 4 CHANCE (it plays by chance: a dark line through it); other bits 0 |

A ROLL that breaks any of these rules is dropped whole. Each frame is the whole window: send it when the view starts,
at every change of the cursor, the window or the notes, and while playing when the playhead's column changes (at most
15 a second). A frame that differs from the last only in `play` (and bit 0 of `flags`) redraws just the playhead's old
and new columns (measured in the simulator: ~5 KB to the panel); any other change the whole band (~55 KB). The
display: the header and footer as on every page, the roll in a band x 4..235, y 34..151 (key column 22 pixels, the grid
208 pixels wide at most, a row 102 / `rows` pixels high), the page's four slots as compact tiles under it (y 152..199;
the tile in focus in the accent). The big value view (1.2) is not shown over the roll; a toast and a LIST cover it and
it comes back. It ends with ROLL `rows = 0`, a new HELLO_ACK, a link change and a switch to Synth mode; a PAGE does
not end it.

Bytes, an example (12 rows from C4, steps 17..32 of 32, playing, the cursor on the note at step 19, E4):

```
F0 7D 52 34 5E 01 3C 0C 10 00 10 00 20 02 04 05 04 05 "C4-B4" 03      flags PLAY, lo 60, 12 rows, 16 steps, first 16,
   00 02 00 64 01   02 01 04 7F 08   0E 02 0B 40 06   F7                total 32, cursor (2, 4), play 5, beat 4, 3 notes
F0 7D 52 34 5E 00 00 00 00 00 00 00 00 7F 7F 7F 00 00 00 F7            the roll ends
```

ROLL is 20 bytes (the end) to 388 (64 notes, a label of 24 accented letters), far below 640.

## Connection

1. On boot, and every second until HELLO_ACK: the device sends HELLO and shows "NOT CONNECTED". The keys send notes
   regardless.
2. On HELLO_ACK the device sends SYNC_REQ (and one POT) and switches to the page screen; the host answers with
   PAGE, LEDS, STATUS. A repeated HELLO_ACK does the same (host restarted). To a 1.4 host the device then sends MODE
   (in a local mode only MODE: nothing to draw).
3. A new USB session (cable out and in, `plat_usb_session` changed) drops back to step 1.
4. PAGE / VALUE / STATUS / TOAST received before the HELLO_ACK are ignored.
5. A malformed frame of ours is dropped (and counted); frames from the host that are device-to-host commands are ignored.

## Firmware-local rules (the only logic on the device)

* SEL held is SHIFT (drawn in the footer). SEL is still sent as BTN.
* OCT- / OCT+ without SHIFT shift the keyboard octave locally (-3..+3) and are still sent as BTN. With SHIFT held they
  do not change the octave (and are sent).
* **SETUP page** (local, never sent to the host): SEL held + GLO opens it, on any screen, connected or not. The press of
  GLO, every button pressed while the page is open and the encoders stay local (the SEL press itself was already sent).
  K1 VELOCITY: SOFT / NORMAL / HARD (touch curve -3 / 0 / +3) / FIXED 100. K2 AFTERTOUCH: OFF (default) / POLY /
  CHANNEL. K3 LED LEVEL: OFF / LOW / HIGH, the brightness of the dim LEDs (the lit ones keep their level). K4 KEY
  CHANNEL: MIDI channel 1..16 for the keys (default 1). A page button (HOME ENV LFO FX EDIT GLO ARP SEQ) leaves; the
  settings are written to the store when the page is left and survive a power cycle. A held key keeps the channel and
  note it started with. MASTER still works while the page is open. The curve values are first guesses and need tuning
  on the device. SELECT or PRESETS turn to its second sub-page, VOICE BACKUP (protocol 1.3, "Patch backup"): K1
  BACKUP, K2 RESTORE, K3 shows how many user voices are stored; SAVE carries out the armed action there.
* **Scene page** (protocol 1.6, local like SETUP): SEL + SEQ, in RUN4 mode with a host that sent scenes; the white keys
  recall scenes 1..16 of the bank, OCT- / OCT+ the bank (see "Protocol 1.6: the scene page").
* Everything else is forwarded as BTN / ENC / POT.
* The lights are exactly what LEDS says; with no host, held keys light themselves.

## The modes, and Bluetooth

**Modes.** The firmware has four (two in `app-noamb`): RUN4 mode (everything above), **AMB mode**, **DUB mode** (above), **Synth mode**, a standalone
six-operator FM synthesizer, and **AMB mode** (docs/MANUAL.md). **SEL + HOME** switches, round (the HOME press is not
sent); a 1.4 host switches with MODE_SET ("Protocol 1.4"). With a 1.4 host the link stays up at a switch (MODE, not
HELLO); what follows is how it is with a 1.0 .. 1.3 host. The mode the FM-1 was left in is kept and booted
into. HELLO's `mode` byte says which: 0 = RUN4, 1 = a local mode (SYNTH, AMB or DUB). After a switch the firmware sends a HELLO with the new mode
every second until HELLO_ACK, in both directions (the first HELLO goes at once, so a host that answers resyncs
immediately). In Synth mode it sends nothing else but two buttons: **SEL + REC** and **SEL + PLAY** go out as BTN
REC and BTN PLAY (press and release, ids 11 and 10, no SEL) while a HELLO_ACK for mode 1 was received, so the host's
transport can record the synth; REC and PLAY alone, and every other control, stay local: no other BTN, no ENC or POT,
the keys do not send notes, and PAGE / VALUE / LEDS / STATUS / TOAST / LIST from the host are ignored; MASTER is the volume and
the pages are drawn locally. (Protocol 1.3 adds, to a 1.3 host only: HV_STATE, the answers VOICE_NAMES / VOICE_DATA, and
the patch backup's frames, which work in both modes.) A host should stop driving the screen when it sees mode 1 and start
again (new HELLO_ACK) when it sees mode 0. In Synth mode the MIDI in (USB, Bluetooth, the jack) plays the synth, so the FM-1 also works as a
sound module (note on/off, sustain, pitch bend, all notes off, on the channel set in GLOBAL; omni by default) and DX7
SysEx dumps are imported (see the manual). MIDI clock (F8) and START / CONTINUE / STOP (FA / FB / FC) in drive its
sequencer and arpeggiator (clock AUTO, the default; the internal tempo again half a second after the last F8); with
clock OUT it sends them. **USB audio:** the firmware keeps fm1-base's default `usb_audio_in` and (since 0.9.1) sets `usb_audio_out = false`
(stated in `app/fm1.toml`): the FM-1 is a USB audio input device in both modes, so the host can record Synth mode's
output over the same cable while its own sound stays on its own speaker or headphones.

**Bluetooth MIDI.** The protocol runs unchanged over a Bluetooth MIDI link when no USB computer is connected (with a
computer on USB, the frames go to both links). The firmware is built with `ble = true` and fm1-base's own Bluetooth
stack (`ble_stack = "own"`, `docs/bt/BLE-STACK.md` in fm1-base), whose BLE-MIDI carries SysEx in both directions ("SysEx
over packets"), which JieLi's library stack is not documented to do. On the link a SysEx frame is a run of USB-MIDI
SysEx packets (cable 0, CIN 4 = three bytes and more to come, 5 / 6 / 7 = the last 1 / 2 / 3 bytes): a BTN frame is
`04 F0 7D 52`, `04 34 02 00`, `06 01 F7`. A host that speaks BLE-MIDI through an OS driver (CoreMIDI, Android MIDI) never
sees this level. Frames from the device are at most 136 bytes (protocol 1.3 to 1.5; up to 1.2 they were 11): the firmware sends
one only when the 64-slot queue has room for it whole; frames from the host are reassembled in the firmware up to 640
bytes between F0 and F7, as on USB, **one at a time** (a frame that arrives while the last one waits is dropped): a host
must not send bursts over Bluetooth. The 1.3 exchanges that move data (backup, restore) are paced by the device's
requests for that reason; 1.4's description is one request answered by many device frames (only the device's queue
paces it), and PSET frames from the host must be spaced (one at a time, wait for the PVAL or ~30 ms). Notes go out on both links. Bluetooth
starts at every power-on (as M-VAVE's firmware does) and the firmware has no switch for it; `build.py --no-ble` builds
the USB-only package. **Not verified on a device**: the simulator models the link, not the radio.

## Versioning

`proto_ver` is 1. A host should reject a HELLO with a `proto_ver` it does not know; unknown commands are ignored by
the device. Minors so far: 1.0 (no minor byte), 1.1 (LIST, LIST_PICK), 1.2 (PAGE trailer, FOCUS, VALUE flags, the big
value view), 1.3 (host voice, voice list, tape view, patch backup), 1.4 (local modes: list, switch, self-description,
values, status line), 1.5 (the local mode's page: LVIEW, LVIEW_SET; the value pace 40 ms), 1.6 (the scene page: SCENES,
SCENE_STATE, SCENE_PICK), 1.7 (the piano roll: ROLL); the firmware's HELLO says
7 (since RUN4 CTL firmware 0.9.2, whose HELLO still says firmware 0.9; 5 in firmware 0.8, 0.9 and 0.9.1: 0.9 adds DUB mode,
id 3, with no new message). Firmware 0.9.3 (synth/dub CPU fixes) keeps minor 7. Both sides speak the lower of the two minors.

## Implementing RUN4 CTL in your own firmware or device

This document and the codec (`src/r4p.h`, `src/r4p.c`: plain C99, no libc beyond `stddef.h` / `stdint.h`, no
allocation, no floating point) are MIT-licensed so that other firmwares and devices can speak RUN4 CTL. Copy the codec
or write your own from this document; `tests/r4p_test.c` has the known bytes, `tests/r4p_fuzz.c` the robustness test,
`tools/fake_run4.py` (a host) and `tools/fake_fm1.py` (a device) are independent encoders to check against.

**The minimum a device implements:**

1. **HELLO** every second until a HELLO_ACK, with `proto_ver` 1, your own `fw_major.fw_minor`, `mode` (0 if the host
   is to draw your screen, 1 if you play on your own) and `proto_minor` 5 (4 without LVIEW); take **HELLO_ACK** (`proto_ver`, minor) and
   speak the lower minor. A new link: HELLO again.
2. **Protocol 1.4's self-description** for every mode that plays on its own: MODES_REQ / MODES, MODE_SET / MODE,
   DESC_REQ (DESC_HEAD .. DESC_END with the hash), VALS_REQ, PSET, PVAL, LSTATE, LM_ERR, and with 1.5 LVIEW /
   LVIEW_SET if your screen shows the description's pages (a mode without a device page sends no LVIEW, takes
   LVIEW_SET's checks and changes nothing). With it the host shows and
   edits your parameters without knowing your device. Your mode ids, names, pages and parameter ids are yours (keep
   ids stable across your firmware versions); a device with one mode lists one. A mode with flag CTRL is a controller
   mode: the host drives your screen with PAGE / VALUE / LEDS / STATUS (1.0 .. 1.2) and you send BTN / ENC / POT; a
   device without a screen leaves CTRL out.
3. **The host voice's notes**, if your device makes sound: after HV_ON (`on`, `chan`) play plain MIDI channel messages
   on `chan` from the host's link (note on / off, pressure, pitch bend, CC 64, 120, 123) and answer HV_STATE. HV_VOICE
   and HV_PARAM are the FM-1's (a DX7-style patch and its parameters): a device with another engine ignores the patch
   kinds it cannot play and the HV_PARAM ids it does not have, and says so in HV_STATE (`voice` 127, its own name). A
   device without sound ignores HV_* and never sends HV_STATE.

Everything else (LIST, the tape view, the patch backup, the big value view) is optional: ignore a host frame you do not
implement (it is never answered), never send a device frame the host did not ask for or a lower minor does not have.
Keep frames from the device at most 136 bytes and to a host that stops reading never block: send a frame only when it
fits whole.

**Manufacturer ID 0x7D.** `7D` is MIDI's ID for non-commercial and educational use; RUN4 CTL uses `7D 52 34` ("R4")
as the protocol's own header, not as anyone's manufacturer identity. Any device may use this header for these frames.
Do not put other data under `7D 52 34`, and check that your other SysEx does not collide (fm1-base's are listed under
"Transport"). A commercial product that also has SysEx of its own should use its registered manufacturer ID for that.

**How RUN4 treats a device it does not know** (the rules a host follows):

* Any MIDI port whose device sends a valid HELLO with `proto_ver` 1 is a RUN4 CTL device, whatever its USB or
  Bluetooth name; the name is only shown. A port that never sends HELLO is an ordinary MIDI port. A HELLO with another
  `proto_ver` is refused (the device stays an ordinary MIDI port).
* `fw_major.fw_minor` are the device's own and mean nothing across devices; the host decides by `proto_minor` and,
  from 1.4, by MODES / MODE flags and the descriptions, never by firmware numbers or names (an exception the FM-1 needs:
  HV_PARAM ids 18..20 only to RUN4 CTL firmware 0.6 and later).
* Mode ids, parameter ids, page and label texts are taken as the device sends them; a description is cached per device
  (its port's identity) and hash. A kind, flag bit or frame the host does not know: it skips that parameter or frame
  and goes on (it does not drop the device). A malformed frame is dropped.
* The host voice is used only after an HV_STATE answered HV_ON (no HV_STATE within a second: no host voice).
* The host never sends 1.7 frames (ROLL) to a device that said a minor below 7, 1.6 frames (SCENES, SCENE_STATE) to a device that said a minor below 6, 1.5 frames (LVIEW_SET) to a device that said a minor below 5, 1.4 frames below 4, nor 1.3
  frames below 3.
