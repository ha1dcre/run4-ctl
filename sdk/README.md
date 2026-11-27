<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Eugen Schott -->
# RUN4 CTL SDK 0.1.0

Add a RUN4 controller page to another FM-1 firmware or MIDI device while keeping
its native instrument, UI and storage. This SDK contains newly written MIT code
and the same MIT wire codec used by the RUN4 CTL firmware. It has no dependency on
our FM synth, AMB, DUB, fm1-base, JieLi SDK, iOS app or a particular display library.

The RUN4 app with controller support (RUN4 1.9) supplies parameter names, formatted
values, pages, lists and LED states. Your firmware supplies MIDI transport,
physical controls and drawing. RUN4 1.8.5 and other MIDI hosts can still receive
ordinary keyboard notes without completing the controller handshake.

## Try it in one minute

From the repository root, or from the extracted SDK archive:

```sh
make -C sdk test example
```

Requirements: a C99 compiler, a C++11 compiler for the header/link smoke test, and
Make. Python 3 is needed only for packaging. No submodules or hardware required.
The example is a console device plus an in-process host, **not a USB device
emulator**. It exchanges real protocol frames and demonstrates:

- HELLO / ACK negotiation, SYNC_REQ and MASTER;
- a keyboard note before ACK, and another in native mode;
- a Tape page, Knob 1, a matching value update, and PLAY press/release;
- retry after transport backpressure;
- returning to native mode and notifying RUN4 to stop drawing.

The last line says `minimal example: OK`. For an MCU, compile these three files:

```text
sdk/src/run4ctl.c
src/r4p.c
YOUR_PLATFORM_ADAPTER.c
```

Use include paths `sdk/include` and `src`. See the complete runnable
[example](examples/minimal.c), [public API](include/run4ctl.h) and
[integration guide](INTEGRATION.md).

## What is included

| Component | Scope |
| --- | --- |
| `sdk/include/run4ctl.h`, `sdk/src/run4ctl.c` | Static C99 controller session, bounded SysEx assembly, non-blocking input helpers, LIST navigation, keyboard MIDI |
| `src/r4p.h`, `src/r4p.c` | Canonical codec for the complete protocol through 1.8; C++ linkage supported |
| `sdk/examples/minimal.c` | Buildable end-to-end example with an in-process host |
| `sdk/tests/` | Session, input, malformed-stream, backpressure, version, C++ and standalone-package checks |
| `docs/PROTOCOL.md` | Full wire specification, including optional native engines and advanced views |

Paths above are relative to the repository/archive root. The codec is kept once
in `src/`; the archive exporter copies that exact source, never a separately
maintained SDK fork.

## Controller profile and optional features

**The session helper defaults to protocol 1.2.** It implements the handshake,
four parameter tiles, VALUE, LEDS, STATUS, TOAST, LIST / LIST_PICK and FOCUS.
LIST navigation is local; SELECT/PRESETS move, HOME/PLAY accept, page buttons or
SAVE cancel. The UI receives callbacks for highlight changes and local closure.
Your renderer must apply incoming PAGE/VALUE updates and display LIST entries.

Call `r4ctl_set_display_profile()` before opening the link to receive more data
through the same `message` callback. `R4CTL_DISPLAY_TAPE` (1.3) adds waveform,
loop, playhead and meter data. `R4CTL_DISPLAY_ROLL` (1.7) also adds the current
Poly or TÓLF piano-roll window. Pick the profile your layout can display.
Each connection negotiates a version the host supports.

The example uses a simulated host and renders the values in the terminal. Use
`make -C sdk example` to run both the default and the display-feedback paths.
The [integration guide](INTEGRATION.md#display-feedback) explains how to use
the data with your own screen or step LEDs. The full codec covers 1.8, including
wire messages outside the session helper's controller profiles.

## Export a standalone SDK

```sh
make -C sdk archive package-check
# sdk/build/run4ctl-sdk-0.2.0.zip and .zip.sha256
```

The repeatable ZIP contains only the SDK, canonical MIT codec, full protocol and
MIT licence. It preserves the small `sdk/`, `src/` and `docs/` layout, so the same
build commands work after extraction. The package test builds the extracted
archive in a fresh temporary directory without the firmware or its submodules.

For sanitizers: `make -C sdk BUILD=build/sanitized SANITIZE=1 test example`.

## Integration status and licence

The portable helper, example and package are tested on host compilers. A port to
another firmware still needs testing on that firmware's physical hardware, MIDI
transport and display. This is not a drop-in replacement for a device's firmware.

All files in `sdk/`, the shared codec and protocol are MIT-licensed;
[LICENSE-MIT](../LICENSE-MIT) contains the notice to retain when copying. The SDK
archive contains no DSP engine or platform code. Other files in the full
repository retain their own licences. The FM-1 installer in `tools/ios` is a
separate MPL-2.0 component, not this controller SDK.

This is an independent project, not affiliated with M-VAVE. Keep your own firmware
name; describe the feature as RUN4 CTL protocol support.
