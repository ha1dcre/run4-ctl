<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Eugen Schott -->
# Integrating the RUN4 CTL SDK

This guide adds a controller screen to an existing firmware. Your synth, native
UI, presets, flash layout and audio driver stay owned by that firmware. Start
with [the runnable example](examples/minimal.c); the complete contract is
[docs/PROTOCOL.md](../docs/PROTOCOL.md).

## 1. Add the sources

Compile `sdk/src/run4ctl.c` and `src/r4p.c` as C99, with include paths `sdk/include`
and `src`. Include `run4ctl.h` from C or C++; C++ users link those C object files.
Use the MIT files only if you want the SDK without the complete firmware.

Allocate one `r4ctl_t` statically per MIDI peer/transport, and install callbacks:

```c
static r4ctl_t controller;
r4ctl_callbacks_t callbacks = {0};
callbacks.user = &your_driver_state;
callbacks.send_sysex = try_send_whole_sysex;
callbacks.send_midi = try_send_whole_midi;
callbacks.message = apply_host_message;
callbacks.connection = host_connection_changed;
callbacks.list_selection = highlight_list_item;
callbacks.list_closed = close_list;
if (!r4ctl_init(&controller, &callbacks, YOUR_FW_MAJOR, YOUR_FW_MINOR)) {
    /* Invalid configuration: do not enter the controller screen. */
}
```

The helper uses roughly 1.1 KiB of state on tested hosts; use `sizeof(r4ctl_t)`
for your compiler. It allocates no heap memory. Stack use includes a decoded
message and a 136-byte transmit frame; run it on the control loop, not in your
audio interrupt. Callbacks must copy data they retain and must not re-enter the
mutating SDK operations synchronously; read-only state queries are fine. Queue
interrupt/USB/Bluetooth input for the owner loop.

## 2. Connect the MIDI transport

`send_sysex` receives one complete `F0 ... F7` frame. `send_midi` receives a complete
ordinary MIDI message, such as `90 3C 64` or `FA`. They receive **raw MIDI bytes**:

- USB: your driver adds/removes USB-MIDI cable/CIN event headers. Use cable 0 for
  a normal single-cable endpoint. SysEx CIN 4 continues; CIN 5/6/7 ends with 1/2/3
  bytes. Notes use CIN 9/8 and realtime CIN 15.
- Bluetooth: your BLE-MIDI stack adds/removes packet timestamps and handles SysEx
  spanning packets. Do not pass BLE headers to the SDK.
- A driver that already reassembles SysEx calls `r4ctl_receive_frame()` with F0
  and F7 included. For a body-only callback, feed F0, the body, then F7 through
  `r4ctl_receive_byte()`.

Both transmit callbacks return **exactly 1** only after copying/queuing the entire
message; 0 (or an error) must send no bytes. Check room for the whole frame. Never
sleep, wait for the host, or queue a partial SysEx frame. Serialize this writer
with your firmware's other SysEx writers so frames cannot interleave. If your
transport reserves/enqueues in another task, use its atomic whole-message API.

For an existing fm1-base USB port, the adaptation is approximately:

```c
/* Platform-specific illustration: plat_* comes from fm1-base, not this SDK. */
static int try_send_whole_sysex(void *user, const uint8_t *frame, size_t n)
{
    (void)user;
    if (!plat_usb_session() || plat_sysex_room() < n) return 0;
    return plat_sysex_send(frame, (uint32_t)n) == 0;
}
static int try_send_whole_midi(void *user, const uint8_t *m, size_t n)
{
    uint32_t packet;
    (void)user;
    if (!plat_usb_session() || !plat_midi_out_room(TO_USB)) return 0;
    packet = (m[0] >= 0xF8 ? 15u : m[0] >> 4) | (uint32_t)m[0] << 8;
    if (n > 1) packet |= (uint32_t)m[1] << 16;
    if (n > 2) packet |= (uint32_t)m[2] << 24;
    plat_midi_send(packet, TO_USB);
    return 1;
}
```

This illustration assumes a single serialized writer. Adapt queue reservation
and ISR ownership to your firmware; it is not a replacement for your driver.
A two-link device needs two SDK instances with callbacks targeting their own
peer. An ACK received over Bluetooth must not pair a different USB peer.

Initialization defaults to CONTROLLER. If booting into your native UI, call
`r4ctl_set_mode(device, R4CTL_LOCAL)` before activating the transport.

Call `r4ctl_set_link(device, 1)` when a MIDI peer is available, and 0 on loss.
On a physical reconnect notify loss first, even if your driver reuses the same
port handle. Call `r4ctl_poll(device, now_ms)` regularly, for example at 60 Hz.
The timer is unsigned monotonic milliseconds and handles 32-bit wraparound.

## 3. Handle the handshake and display

While unpaired, poll sends HELLO immediately and every second, retrying a blocked
send on the next poll. It advertises **your firmware version**, controller/local
mode, and the helper's protocol minor 2. Do not use `r4p_hello()` here: that codec
convenience function advertises the full codec minor 8, which this helper does
not implement as a device profile.

A valid HELLO_ACK with major 1 pairs the session. The negotiated minor is the
lower of host and helper. In controller mode, poll then sends SYNC_REQ and the
latest MASTER value (if supplied). Repeated ACKs trigger a fresh sync. The wire
protocol has no generic host heartbeat: call set_link on driver disconnect;
there is no invented timeout that disconnects an idle app.

The message callback receives ACK and supported display messages only after
pairing. Copy the decoded data you need; do not retain the callback pointer:

| Message | Renderer action |
| --- | --- |
| HELLO_ACK | Clear cached list/page state; a repeated ACK also needs a new sync |
| PAGE | Replace the title, track colour and all four parameter tiles; honour the optional 1.2 path/focus trailer |
| VALUE | Replace only the addressed slot's value/text; flags may request the temporary large value display |
| LEDS | Apply all 14 button and 27 key states (off, dim, on, blink) |
| STATUS | Update BPM, bar/beat and play/record flags |
| TOAST | Show the text for dur100ms; zero means 1.5 seconds |
| LIST | Show the provided window and selected row; total 0 closes it |
| FOCUS | Update the focus frame and item position |

PAGE colours are 0..127 per channel (scale to your display), and strings are
Latin-1 with the terminator already decoded, not UTF-8. Value text is formatted
by RUN4; keep its units. Enum values encode segment count/index, not a continuous
0..16383 bar: see the wire specification. LIST callbacks handle local highlight
and closure without waiting for the next round trip.

Foreign SysEx, malformed frames, and ordinary MIDI must not accidentally alter
your UI. The byte assembler skips interleaved realtime, bounds an unfinished
frame to 640 bytes, and recovers at a new F0. Route your own updater, patch SysEx,
notes and clock to their native handlers too. The SDK never echoes MIDI input.

## 4. Map the panel and preserve retries

Use the named button/encoder ids in `run4ctl.h`; protocol ids describe the FM-1's
layout even on another controller. Send both press and release. K1..K4 are encoder
ids 3..6. SELECT is 0, ALGORITHM 1, PRESETS 2; SEL is button 1, PLAY 10, REC 11.

```c
/* Button scan: retain an unsent edge, including a note/button release. */
if (physical_down != last_accepted_down) {
    if (r4ctl_button(&controller, R4CTL_PLAY, physical_down))
        last_accepted_down = physical_down;
}
/* Encoder accumulator: consume only the delta successfully queued. */
int delta = pending_detents > 63 ? 63 : pending_detents < -63 ? -63 : pending_detents;
if (delta && r4ctl_encoder(&controller, R4CTL_KNOB1, delta))
    pending_detents -= delta;
/* Absolute pot: newest value wins; poll handles pacing and retry. */
r4ctl_master(&controller, master_14bit);
```

Run panel scanning after poll. Button/encoder calls return 0 while unpaired or in
LOCAL; keep native control handling outside that route. Discard accumulated
controller-only detents when leaving that mode, so native edits are not replayed
into RUN4 later. Maintain a bounded edge queue if a user can press and release
between retries; retain the scan transition when the queue is full. The helper
owns no unbounded queue and cannot reconstruct an edge your scanner discarded.

While LIST is open, the helper consumes panel navigation and sends LIST_PICK.
Unrelated encoders/buttons stay local. Keep keyboard notes and MASTER working.
Handle firmware-local combinations (mode/SETUP/octave) before forwarding panel
controls; the SDK does not impose a SETUP page, octave scheme or mode shortcut.
If a modifier press was already forwarded before opening a local UI, keep its
release routed consistently so the host does not retain SHIFT.

## 5. Keep the keyboard useful without RUN4

`r4ctl_note_on/off()` work whenever the physical MIDI link is available, before
ACK and in native mode too. They do not need RUN4 1.9. Channels are zero-based:
0 is MIDI channel 1. Velocity must be 1..127; note-off sends velocity 0.

Map each physical key once. Keep the note **and channel captured at note-on**
until its matching note-off; do not recompute them after an octave/channel change.
Retry a rejected note-off. Keep an explicit held-note table and perform cleanup
when changing routes/modes or disabling your keyboard. Do not have both your old
MIDI-output path and the SDK send the same physical key. Aftertouch and pitch
bend can continue using your existing ordinary MIDI path.

`r4ctl_realtime()` sends an explicitly requested clock/start/continue/stop message.
The helper never generates a second Start automatically. If your engine already
sends MIDI transport, keep one owner of that output. The helper does not provide
a clock generator or a sequencer. Ordinary MIDI compatibility and RUN4 controller
pairing are separate concepts.

## 6. Enter and leave native mode

On your controller page call `r4ctl_set_mode(device, R4CTL_CONTROLLER)`; on your
native synth/effect page call `r4ctl_set_mode(device, R4CTL_LOCAL)`. Keep polling
and receiving while LOCAL. A new mode 1 HELLO tells RUN4 to stop driving the UI.
In LOCAL, the helper ignores display updates and panel control calls return 0;
your own engine/UI runs normally. To come back, mode 0 HELLO/ACK and SYNC_REQ
restore the current app view. Choosing a mode does not stop/start/mute audio.

LOCAL uses the legacy Synth-mode wire flag; named native engines and remote
parameter pages require the 1.4 description backend below.

This base profile exposes no native parameters or host voice to RUN4. USB audio
must already be supplied by your platform and routed by the app. It does not
silence your hardware speaker, change MASTER, write flash, or install firmware.

## Extending beyond the minimal profile

The package's canonical codec parses/encodes all messages through 1.8. Use it to
build an extended backend, and advertise a higher minor only when the associated
routes/UI/state are actually implemented. The shipped session helper remains
profile 1.2; changing one define is not sufficient to add these features.

| Protocol | Integration needed beyond the helper |
| --- | --- |
| 1.3 | Optional tape waveform/playhead/meters, voice/patch handling; implement HV_ON/HV_STATE before exposing an engine as a host voice. Safely ignore unsupported FM patch kinds. |
| 1.4 | Stable local-mode ids, MODES/MODE flags, MODE_SET, parameter/page descriptions with hashes, values, edits, state and errors. This enables remote control of another firmware's own engine. |
| 1.5 | Native page and knob assignment synchronization through LVIEW/LVIEW_SET |
| 1.6 | Scene banks, active/queued state and picks |
| 1.7 | Render the host's ROLL window exactly; the host continues to own note editing |
| 1.8 | USB-only monitor lease: unity signal to USB, muted direct output, one MASTER owner, explicit release and mandatory four-second expiry / USB-loss / handshake restoration |

Implement optional views with your own renderer. The SDK is MIT; the complete
RUN4 CTL firmware UI and DSP files have their own licences. For a first integration,
ship the four-tile controller and test it on-device before extending the profile.

## Acceptance checklist

- Run `make -C sdk test example package-check`, including the extracted archive.
- Connect to RUN4 1.9: see HELLO/ACK/SYNC and matching page, focus and lights.
- Turn each physical knob: only the corresponding tile changes; screen edits
  in RUN4 arrive back on the same display slot.
- Exercise list selection, accept, cancel, window changes, and button releases.
- Return to native mode: RUN4 stops drawing, your instrument still works, and
  held notes receive matching note-offs.
- With RUN4 1.8.5 or a DAW: standard keyboard notes work without ACK.
- Stop reading the MIDI port: local audio/UI keep running; input retries remain
  bounded and resume in order. Check a queued note-off is not lost.
- Disconnect/reconnect USB and BLE separately, test foreign/update SysEx, and
  verify your firmware's own MIDI input and updater still work.

Physical validation of each third-party firmware port remains the port author's
test; the shipped tests exercise the portable protocol/session and example.
