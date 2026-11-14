# RUN4 CTL

Open-source firmware (GPL-3.0) that turns the **M-VAVE FM-1** into a thin hardware controller for the iOS app RUN4.
Its 27 keys, 14 buttons, 7 encoders and the MASTER pot are sent to the app over USB-MIDI, and the app draws pages (four
labelled values with bars, track colour, transport, a tape view) and the LEDs on the FM-1. The FM-1 is silent in this
mode unless RUN4 plays it as an instrument: the **host voice** (protocol 1.3) lets a RUN4 track play the FM-1's own FM
engine. RUN4 also keeps a backup of the FM-1's user voices.

**Not affiliated with M-VAVE.** FM-1 is M-VAVE's product name; this is an independent project built on the community
firmware platform [fm1-base](https://github.com/charlesvestal/fm1-base). See `NOTICE`.

Status: **complete in the host simulator, not yet run on an FM-1.** RUN4 mode (protocol v1.3 in `docs/PROTOCOL.md`: codec,
LIST, non-blocking output, SETUP page, touch velocity and aftertouch, the host voice, the voice list, the tape view, the
patch backup) and Synth mode (Felucca's FM6 engine, effects, DX7
SysEx import, arpeggiator, sequencer, pages) are implemented and tested in fm1-base's host simulator and in native
tests (`make test`). Bluetooth MIDI is wired in and simulated. **Nothing has been built for or run on the real device**:
the toolchain is Linux x86-64 and needs Docker. User manual: `docs/MANUAL.md`.

## Layout

| Path | What |
| --- | --- |
| `docs/PROTOCOL.md` | the wire protocol v1 (minor 3) |
| `src/r4p.[ch]` | the codec: firmware and native tests share it (no libc, no floats) |
| `src/ui.c` | the display: state from host frames, redraw by region |
| `src/tape_ui.c` | RUN4 mode's tape view (protocol 1.3): waveform, loop region, playhead and meters in Synth mode's layout |
| `src/synth.c`, `src/synth_seq.c`, `src/synth_ui.c`, `src/synth/` | Synth mode: the FM6 engine (Felucca, Apache-2.0, see NOTICE), voices, effects, arpeggiator, DX7 import; the step sequencer (16 x 64 steps, after Felucca's); its pages |
| `app/` | the fm1-base app: `app.c` (input, local rules, host frames), `app.h`, `fm1.toml` |
| `tests/r4p_test.c`, `tests/synth_test.c` | codec unit tests; native tests of the synth engine (levels, polyphony, DX7 import, CPU) |
| `tests/scenarios/*.txt`, `tests/run_sim.sh` | host-simulator scenarios (boot, pages, input, robustness) |
| `tests/screens/`, `tests/audio/` | screenshots and a short listening file the simulator wrote |
| `tools/fake_fm1.py`, `tools/coremidi.py` | a Mac stand-in for the FM-1 (virtual CoreMIDI ports "FM-1", terminal panel); standard library only |
| `tools/fake_run4.py` | stand-in for the RUN4 app (frames as hex, simulator scripts); standard library only |
| `base/` | fm1-base (git submodule; it brings felucca-bsp) |

Store (flash data area, two 4 KiB sectors per object, `app/app.h`): panel 0xDC000, SETUP 0xDA000, Synth settings
0xD8000 (186 B), two user banks 0xD6000 / 0xD4000 (2056 B each), Bluetooth 0xDE000, and the sequencer's 16 patterns in
four objects 0xD2000 / 0xD0000 / 0xCE000 / 0xCC000 (2088 B each, 8352 B; 32 KiB of flash). RAM for the patterns: 8.3 KB.

Display budget (Synth mode's live band, `src/synth_ui.c`): at most 25 frames a second, drawn in RAM and sent as 16 px
strips only as tall as the old and new trace (measured in the simulator, scenario `14_synth_look`: ~18.5 KB a frame for
a full-scale note or chord, 0 for the idle line; the whole band would be 38 KB). At the panel's ~44 MHz effective SPI
(`base/device/lcd.c`, ~5.5 MB/s, DMA) that is ~3.4 ms of transfer a frame, ~8 % of the time at 25 fps, in the main loop;
the audio interrupt only stores 16 samples a 32-frame block into the scope's ring. Host draw time ~15-25 us a frame;
on the device not measured (assumed well under 1 ms at 360 MHz).

RUN4 mode's tape view (protocol 1.3, `src/tape_ui.c`, scenario `16_tape_view`): the band is drawn in RAM at most 25
times a second, and a playhead or meter update sends only the columns it touches: ~5.6 KB a frame at the host's 15
updates a second (playhead and all five meters moving), ~11 us of host draw time; a new waveform or loop region sends
the whole band (38 KB). That is under a third of Synth mode's scope per frame, so RUN4 mode with the tape view costs the
display less than Synth mode does.

Host voice (protocol 1.3): RUN4 mode plays Synth mode's engine with its own settings (`syn_swap` in `src/synth.c`
trades the two 186-byte setting sets at a mode switch; the render is held silent meanwhile and the voices and effect
lines start empty). Its load is Synth mode's: 8 voices and the effects, 0.4-0.6 % of real time on this Mac (scenarios
`15_host_voice` and `16_tape_view`, `make synth`); the device's share was not measured (no device). With the tape view
moving, the sum is no more than Synth mode with its scope. The USB audio from the iPad is mixed with it on the FM-1 by
fm1-base after the USB recording's tap (`base/device/uac_mix.h`): the iPad records the FM-1 alone and hears both.
RAM added: the parked settings (186 B); the restore's 32 voices wait in the DX7 import's 4 KiB buffer.

## Build and test (macOS or Linux, host only)

    git clone --recursive <this repo>          # or: git submodule update --init --recursive
    python3 -m venv .venv && .venv/bin/pip install pillow    # fm1-base's font generator needs Pillow
    make test        # codec unit tests (ASan/UBSan), synth tests, the simulator scenarios, the fake FM-1's self-test
    make unit        # only the codec tests
    make sim         # only the simulator (builds app/build/host/run4ctl_host)

`tools/hostshim/` drops OpenType features when Pillow lacks libraqm ; it only affects the
simulator's font generation (digit widths), never the device build.

Browser emulator (play it with mouse, computer keyboard or a MIDI keyboard; needs Emscripten, e.g. emsdk in `~/emsdk`
— not on an exFAT volume, it needs symlinks): `source ~/emsdk/emsdk_env.sh && python3 base/tools/build.py app --web`,
then `cd app/build/emu && python3 -m http.server 8741` and open http://127.0.0.1:8741 in Chrome (Web MIDI with SysEx).

Run a scenario by hand: `python3 tools/fake_run4.py sim tests/scenarios/02_connected_pages.txt > /tmp/s.txt &&
app/build/host/run4ctl_host /tmp/s.txt /tmp/out` (screenshots land in `/tmp/out`). Frames as hex:
`python3 tools/fake_run4.py hex`.

## Try RUN4 without the hardware (fake FM-1)

`python3 tools/fake_fm1.py` opens a virtual MIDI source and destination called **FM-1** on the Mac, answers like the
firmware (HELLO every second until RUN4 answers, then SYNC_REQ) and turns the terminal into the panel (`?` lists the
keys; TAB is piano mode). It prints every PAGE / VALUE / LEDS / STATUS / TOAST that RUN4 sends. To reach an iPad or
iPhone:

1. Mac: Audio MIDI Setup -> Window -> Show MIDI Studio -> double-click the Network icon -> under "My Sessions" tick
   **Enabled** (the session's port is called like the session, "Session 1" by default).
2. `python3 tools/fake_fm1.py --bridge` (or `--bridge "Session 1"`): besides the virtual "FM-1" ports it talks through
   the session's port, so the iPad sees what the FM-1 would send and RUN4's frames reach the fake. A Mac running RUN4
   needs no bridge: it sees the virtual port "FM-1".
3. iPad: in Audio MIDI Setup on the Mac the iPad appears under Directories (same Wi-Fi): select it, **Connect**. On
   the iPad, RUN4 lists the network session as a MIDI port; enable it as the FM-1 in RUN4's settings.

Self-tests without MIDI: `python3 tools/fake_fm1.py --selftest` (part of `make test`); `--loopback` makes a CoreMIDI
round trip in one process. python-rtmidi does not build here (RtMidi lacks `sem_timedwait` on this macOS), so
`tools/coremidi.py` binds CoreMIDI through ctypes. The interactive mode was run only through a pseudo-terminal, and the bridge
was not tried against a real Network MIDI session; neither was run against RUN4.

## Device build (Apple silicon Mac with Docker) — not tried yet

The JieLi toolchain is Linux x86-64 only; on macOS fm1-base's `tools/build.py` runs each tool in a
`linux/amd64` container (`debian:bookworm-slim`).

1. Docker Desktop → Settings → General → enable **"Use Rosetta for x86_64/amd64 emulation"**, and start Docker.
2. Once, fetch the toolchain and the SDK (outside this repository; neither is committed):

       sh base/tools/get_toolchain.sh             # → ~/.jieli/toolchain
       sh base/bsp/tools/get_sdk.sh               # → ~/.jieli/ac79-sdk, checks file hashes
       export JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk

   `get_sdk.sh` calls `sha256sum`; if the Mac lacks it, `brew install coreutils`.
3. Build: `python3 base/tools/build.py app --no-ble` → `app/build/<name>-usb.fwsc` (phase 1 is USB only).
4. Install over USB: the web installer, or `base/tools/fm1_install.py app/build/<name>-usb.fwsc`
   (needs `pip install mido python-rtmidi` in a venv). The stock V15 can be restored; back up presets first.

## Licence

GPL-3.0 (`LICENSE`). Files from fm1-base and felucca-bsp stay MPL-2.0 (see `NOTICE`).
