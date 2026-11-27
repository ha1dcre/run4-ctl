<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Eugen Schott -->
# Connecting a MIDI stack

RUN4 CTL sends its display and panel messages as MIDI 1.0 SysEx. Your transport
must pass SysEx in both directions. A controller with only CC and note messages
can use RUN4's normal MIDI mapping. The display and session feedback use SysEx.

## Pick the input your stack exposes

| Stack | Receive | Send |
| --- | --- | --- |
| Raw serial MIDI (DIN/TRS), UART, ALSA RawMIDI | Feed arbitrary chunks to `r4ctl_receive_bytes()`. | Queue the output bytes through the same port. |
| USB-MIDI 1.0 with TinyUSB-style event packets | Feed each four-byte event to `r4ctl_receive_usb_midi1()` for its cable. | `r4ctl_pack_usb_midi1()` produces events for your packet writer. |
| BLE-MIDI via a BLE stack or CoreMIDI | Feed decoded MIDI chunks or complete SysEx frames. | Give the BLE stack the complete raw MIDI message and let it handle timestamps and MTU fragmentation. |
| CoreMIDI with `MIDIPacketList` | Feed each packet's data bytes. | Send the complete bytes with the destination's MIDI port. |
| CoreMIDI `MIDIEventList` or another UMP stack | Feed each SysEx7 UMP to `r4ctl_receive_ump()` for its group. | `r4ctl_pack_ump()` produces MIDI 1.0 channel/system words and SysEx7 words. |
| ALSA Sequencer, JACK MIDI, RtMidi | Feed MIDI bytes or complete SysEx messages. For ALSA Sequencer's `SND_SEQ_EVENT_SYSEX`, use the external data and length. | Use the stack's byte output or queue a sequencer SysEx event. |
| RTP-MIDI, or a bridge over Wi-Fi | Feed the MIDI bytes or frames recovered by the bridge. | Send raw messages to the bridge. It owns connection setup, RTP framing and timing. |

Pass a complete framed callback to `r4ctl_receive_frame()` when it includes
`F0 ... F7`. A complete body-only callback uses `r4ctl_receive_sysex_payload()`.
For fragmented data with delimiters in the stream, pass each chunk to
`r4ctl_receive_bytes()`. Other MIDI belongs to your normal MIDI input handler.

Allocate one `r4ctl_t` per peer/port or selected cable/group. On disconnect,
call `r4ctl_set_link(device, 0)` and clear transport queues before reusing the
link. The transmit callbacks report success only when the **entire** message
was accepted into a queue. If a stack accepts partial writes, keep the cursor in
your queue and retry the write there. Keep both callbacks and the SDK on a
serialized owner loop. Copy any input or output you queue across threads.

## USB-MIDI event packets

With TinyUSB's device API, read using `tud_midi_packet_read()` and give each event
to `r4ctl_receive_usb_midi1(device, cable, packet)`. In the transmit callback,
convert both SysEx and ordinary MIDI through `r4ctl_pack_usb_midi1()`:

```c
uint8_t packets[(R4P_DEV_MAX + 2u) / 3u][4];
size_t count = r4ctl_pack_usb_midi1(cable, bytes, length, packets,
                                  sizeof packets / sizeof packets[0]);
return count && queue_usb_events(packets, count);
```

`queue_usb_events` is your all-or-nothing queue. Its consumer can use
`tud_midi_packet_write()` one event at a time, preserving order and retaining
each event until accepted. With an API that already exposes raw MIDI streams,
use the byte path instead.

## BLE-MIDI and network MIDI

Configure your library to accept incoming SysEx up to `R4P_FRAME_MAX` bytes.
For example, with Arduino MIDI and a BLE transport, set its
`SysExMaxSize` to at least that value and forward the data and length from
`setHandleSystemExclusive()` to `r4ctl_receive_frame()`. The outbound SysEx
sender can drain a queue with `sendSysEx(length, frame, true)` so the existing
delimiters are kept. The BLE library supplies the timestamps and packets.

For RtMidi, enable SysEx reception with `ignoreTypes(false, false, true)` before
opening the input, and copy callback vectors to your SDK loop. Use
`RtMidiOut::sendMessage()` on the output side, checking errors in the queue
consumer. The same byte interface works when those ports are provided by a
network MIDI bridge. For direct Wi-Fi use, the bridge must expose a
bidirectional MIDI connection to RUN4. The SDK does not speak a vendor remote
API or open a socket in RUN4.

## UMP

Feed one UMP message at a time on the selected group. A CoreMIDI
`MIDIEventPacket` may hold multiple messages, so iterate the messages rather
than treating the whole packet as a SysEx7 message. The helper accepts the
two-word SysEx7 Complete, Start, Continue and End formats. Its words are
host-endian `uint32_t` values. For a byte-oriented USB UMP driver, use the
driver's word encoding at the boundary.

For output, use `r4ctl_pack_ump()` and queue all emitted words. With CoreMIDI,
use a `MIDIEventList` with `kMIDIProtocol_1_0`. Ordinary MIDI is packed as
System or MIDI 1.0 Channel Voice. MIDI 2.0 Channel Voice, SysEx8 and MIDI-CI
are outside the RUN4 CTL wire format.

## Connecting to RUN4 1.9

The standalone RUN4 app discovers the controller's source through CoreMIDI.
To receive replies reliably, expose an input and output on the same CoreMIDI
entity, or use matching display names when creating independent virtual
endpoints. In AUv3, route MIDI both ways through a host that forwards SysEx.
Keep the controller's device/port name independent of its protocol version.

Use the session API for RUN4 buttons, encoders and display feedback.
`r4ctl_send_midi_message()` also queues ordinary MIDI 1.0 CC, PC, pitch bend,
pressure, notes and clock. The app's ordinary MIDI routing and mappings
determine what those messages do. To verify a port, check that HELLO receives
ACK, SYNC_REQ yields PAGE and VALUE, encoder/button traffic goes back, and the
transport still forwards both sides after reconnect.

## References

- [USB-IF USB-MIDI 1.0 packet format](https://www.usb.org/sites/default/files/midi10.pdf)
- [TinyUSB device MIDI example](https://github.com/hathach/tinyusb/tree/master/examples/device/midi_test)
- [Arduino BLE-MIDI transport](https://github.com/lathoub/Arduino-BLE-MIDI)
- [CoreMIDI and UMP](https://developer.apple.com/documentation/coremidi/incorporating-midi-2-into-your-apps)
- [ALSA sequencer event definitions](https://www.alsa-project.org/alsa-doc/alsa-lib/group___seq_events.html)
- [RtMidi input and output API](https://github.com/thestk/rtmidi/blob/master/RtMidi.h)
- [RTP-MIDI packet format](https://www.rfc-editor.org/rfc/rfc6295)
