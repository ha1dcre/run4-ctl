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
| 0x01 | HELLO | `proto_ver, fw_major, fw_minor, mode` (mode 0 = RUN4, 1 = SYNTH, reserved) |
| 0x02 | BTN | `id (0..13), state (1 down / 0 up)` |
| 0x03 | ENC | `id (0..6), delta` |
| 0x04 | POT | `value14` (MASTER) |
| 0x05 | SYNC_REQ | none: "send me everything" |

Button ids follow fm1-base `plat.h`: 0 FX, 1 SEL, 2 ENV, 3 LFO, 4 EDIT, 5 GLO, 6 HOME, 7 SAVE, 8 ARP, 9 SEQ, 10 PLAY,
11 REC, 12 OCT-, 13 OCT+. Encoder ids: 0 SELECT, 1 ALGO, 2 PRESETS, 3..6 K1..K4.

ENC delta: byte `0x40 + d`, so `0x40` = 0, `0x41` = +1, `0x3F` = -1, clamped to +-63 (`0x01..0x7F`; `0x00` is
invalid). Detents are summed per ~60 Hz frame, one ENC frame per encoder per frame; if a frame has more than 63, the
rest goes out in the next frame (`r4p_enc_add` / `r4p_enc_take`).

POT: sent when MASTER moves by 6 counts of 14 bits or more (at most every 20 ms), when it reaches 0 or 16383, and once
after HELLO_ACK.

Keys: plain Note On / Note Off, channel 1, `note = 53 + key + 12 * octave` (key 0 = F3), velocity fixed at 100 in
phase 0 (`key_velocity()` in `app/app.c` is the hook for fm1-base's touch sensing, `APP_TOUCH`, off by default;
poly pressure would go out as Poly Aftertouch). A note-off uses the note the key started with, so an octave change
while a key is held leaves no stuck note.

## Host to device

| cmd | name | payload |
| --- | --- | --- |
| 0x41 | HELLO_ACK | `proto_ver` |
| 0x42 | PAGE | `title(str) track(str) r g b page_idx page_count bank_idx bank_count` then 4 x `name(str) kind value14 text(str)` |
| 0x43 | VALUE | `slot (0..3) value14 text(str)` |
| 0x44 | LEDS | 41 states, 2 bits each, packed 3 per byte LSB first, 14 bytes |
| 0x45 | STATUS | `bpm10 (14 bit), bar (14 bit), beat (0..15), flags` |
| 0x46 | TOAST | `text(str), duration (100 ms units)` |

* PAGE colour: r, g, b each 0..127; the device draws them x2. `kind`: 0 empty, 1 unipolar, 2 bipolar, 3 enum, 4 toggle.
  `value14` by kind: unipolar 0..16383 from the left; bipolar 8192 = centre; **enum: `(segment_count << 7) | index`**
  (the segment count is needed to draw the bar; 2..16 are drawn); toggle: 0 off, non-zero on. `text` is the finished
  value including its unit.
* VALUE replaces one slot's value and text; the device redraws that tile only.
* LEDS: entries 0..13 are the buttons (BTN order), 14..40 the keys (key 0..26). 0 off, 1 dim, 2 on, 3 blink (2 Hz).
  Entry 41, the padding in the last byte, is ignored.
* STATUS flags: bit 0 playing, bit 1 recording, bit 2 overdub. `beat` is 0-based; the device shows `bar.(beat+1)`.
* TOAST duration 0 means 1.5 s.

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
* Everything else is forwarded as BTN / ENC / POT.
* The lights are exactly what LEDS says; with no host, held keys light themselves.

## Versioning

`proto_ver` is 1. A host should reject a HELLO with a `proto_ver` it does not know; unknown commands are ignored by
the device.
