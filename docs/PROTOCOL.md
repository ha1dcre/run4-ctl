# RUN4 CTL protocol v1

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
  the device's). The codec test asserts it (`tests/r4p_test.c`, "max PAGE frame").
* Coexistence: fm1-base reserves SysEx under `7D 4C ..` (sub-IDs `43` remote control, `47` crash log, `48` crash history,
  `53` debug stats, `57` WiFi, `41` Bluetooth audio, `44` Bluetooth devices) and `7D 53 52 ..` (fm1-reader's screen
  reader); its updater uses `F0 22 24 35 ..` and a `F0 pack7(00 59 ..)` framing (the screen mirror reuses the latter). `7D 52 34` collides
  with none of them (checked in `base/device/*.c` and felucca-bsp's `chip/ota.c`). A parser that sees another header
  must return "not mine" without touching state: `r4p_parse_body` returns 0.

## Device to host

| cmd | name | payload |
| --- | --- | --- |
| 0x01 | HELLO | `proto_ver, fw_major, fw_minor, mode, [proto_minor]` (mode 0 = RUN4, 1 = SYNTH; `proto_minor` since 1.1, absent = 0) |
| 0x02 | BTN | `id (0..13), state (1 down / 0 up)` |
| 0x03 | ENC | `id (0..6), delta` |
| 0x04 | POT | `value14` (MASTER) |
| 0x05 | SYNC_REQ | none: "send me everything" |
| 0x06 | LIST_PICK | `index, action` (action 0 = OK, 1 = CANCEL, 2 = MOVE) - protocol 1.1 |

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

## Connection

1. On boot, and every second until HELLO_ACK: the device sends HELLO and shows "NOT CONNECTED". The keys send notes
   regardless.
2. On HELLO_ACK the device sends SYNC_REQ (and one POT) and switches to the page screen; the host answers with
   PAGE, LEDS, STATUS. A repeated HELLO_ACK does the same (host restarted).
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
  on the device.
* Everything else is forwarded as BTN / ENC / POT.
* The lights are exactly what LEDS says; with no host, held keys light themselves.

## Two modes, and Bluetooth

**Modes.** The firmware has two: RUN4 mode (everything above) and **Synth mode**, a standalone six-operator FM synthesizer
(docs/MANUAL.md). **SEL + HOME** switches (the HOME press is not sent). The mode the FM-1 was left in is kept and booted
into. HELLO's `mode` byte says which: 0 = RUN4, 1 = SYNTH. After a switch the firmware sends a HELLO with the new mode
every second until HELLO_ACK, in both directions (the first HELLO goes at once, so a host that answers resyncs
immediately). In Synth mode it sends nothing else but two buttons: **SEL + REC** and **SEL + PLAY** go out as BTN
REC and BTN PLAY (press and release, ids 11 and 10, no SEL) while a HELLO_ACK for mode 1 was received, so the host's
transport can record the synth; REC and PLAY alone, and every other control, stay local: no other BTN, no ENC or POT,
the keys do not send notes, and PAGE / VALUE / LEDS / STATUS / TOAST / LIST from the host are ignored; MASTER is the volume and
the pages are drawn locally. A host should stop driving the screen when it sees mode 1 and start again (new HELLO_ACK)
when it sees mode 0. In Synth mode the MIDI in (USB, Bluetooth, the jack) plays the synth, so the FM-1 also works as a
sound module (note on/off, sustain, pitch bend, all notes off, on the channel set in GLOBAL; omni by default) and DX7
SysEx dumps are imported (see the manual). MIDI clock (F8) and START / CONTINUE / STOP (FA / FB / FC) in drive its
sequencer and arpeggiator (clock AUTO, the default; the internal tempo again half a second after the last F8); with
clock OUT it sends them. **USB audio:** the firmware keeps fm1-base's defaults `usb_audio_in` and `usb_audio_out`
(stated in `app/fm1.toml`): the FM-1 is a USB audio device in both modes, so the host can record Synth mode's output
over the same cable.

**Bluetooth MIDI.** The protocol runs unchanged over a Bluetooth MIDI link when no USB computer is connected (with a
computer on USB, the frames go to both links). The firmware is built with `ble = true` and fm1-base's own Bluetooth
stack (`ble_stack = "own"`, `docs/bt/BLE-STACK.md` in fm1-base), whose BLE-MIDI carries SysEx in both directions ("SysEx
over packets"), which JieLi's library stack is not documented to do. On the link a SysEx frame is a run of USB-MIDI
SysEx packets (cable 0, CIN 4 = three bytes and more to come, 5 / 6 / 7 = the last 1 / 2 / 3 bytes): a BTN frame is
`04 F0 7D 52`, `04 34 02 00`, `06 01 F7`. A host that speaks BLE-MIDI through an OS driver (CoreMIDI, Android MIDI) never
sees this level. Frames from the device are at most 11 bytes (the 64-slot queue is never the limit); frames from the host
are reassembled in the firmware up to 640 bytes between F0 and F7, as on USB. Notes go out on both links. Bluetooth
starts at every power-on (as M-VAVE's firmware does) and the firmware has no switch for it; `build.py --no-ble` builds
the USB-only package. **Not verified on a device**: the simulator models the link, not the radio.

## Versioning

`proto_ver` is 1. A host should reject a HELLO with a `proto_ver` it does not know; unknown commands are ignored by
the device.
