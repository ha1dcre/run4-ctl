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
  and F7 (513 between them), below the 640 bytes fm1-base's USB-MIDI SysEx buffer takes (`HOST_SX_MAX`, the same as
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

## Versioning

`proto_ver` is 1. A host should reject a HELLO with a `proto_ver` it does not know; unknown commands are ignored by
the device.
