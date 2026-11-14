# RUN4 CTL

Open-source firmware (GPL-3.0) that turns the **M-VAVE FM-1** into a thin hardware controller for the iOS app RUN4.
The FM-1 makes no sound in this mode: its 27 keys, 14 buttons, 7 encoders and the MASTER pot are sent to the app over
USB-MIDI, and the app draws pages (four labelled values with bars, track colour, transport) and the LEDs on the FM-1.

**Not affiliated with M-VAVE.** FM-1 is M-VAVE's product name; this is an independent project built on the community
firmware platform [fm1-base](https://github.com/charlesvestal/fm1-base). See `NOTICE`.

Status: **phase 0**. The protocol (`docs/PROTOCOL.md`), the shared codec, the firmware skeleton and its tests run on a
computer in fm1-base's host simulator. It has not been built for or run on an FM-1.

## Layout

| Path | What |
| --- | --- |
| `docs/PROTOCOL.md` | the wire protocol v1 |
| `src/r4p.[ch]` | the codec: firmware and native tests share it (no libc, no floats) |
| `src/ui.c` | the display: state from host frames, redraw by region |
| `app/` | the fm1-base app: `app.c` (input, local rules, host frames), `app.h`, `fm1.toml` |
| `tests/r4p_test.c` | codec unit tests |
| `tests/scenarios/*.txt`, `tests/run_sim.sh` | host-simulator scenarios (boot, pages, input, robustness) |
| `tests/screens/` | screenshots the simulator wrote |
| `tools/fake_fm1.py`, `tools/coremidi.py` | a Mac stand-in for the FM-1 (virtual CoreMIDI ports "FM-1", terminal panel); standard library only |
| `tools/fake_run4.py` | stand-in for the RUN4 app (frames as hex, simulator scripts); standard library only |
| `base/` | fm1-base (git submodule; it brings felucca-bsp) |

## Build and test (macOS or Linux, host only)

    git clone --recursive <this repo>          # or: git submodule update --init --recursive
    python3 -m venv .venv && .venv/bin/pip install pillow    # fm1-base's font generator needs Pillow
    make test        # codec unit tests (ASan/UBSan), then the simulator scenarios
    make unit        # only the codec tests
    make sim         # only the simulator (builds app/build/host/run4ctl_host)

`tools/hostshim/` drops OpenType features when Pillow lacks libraqm ; it only affects the
simulator's font generation (digit widths), never the device build.

Run a scenario by hand: `python3 tools/fake_run4.py sim tests/scenarios/02_connected_pages.txt > /tmp/s.txt &&
app/build/host/run4ctl_host /tmp/s.txt /tmp/out` (screenshots land in `/tmp/out`). Frames as hex:
`python3 tools/fake_run4.py hex`.

## Try RUN4 without the hardware (fake FM-1)

`python3 tools/fake_fm1.py` opens a virtual MIDI source and destination called **FM-1** on the Mac, answers like the
firmware (HELLO every second until RUN4 answers, then SYNC_REQ) and turns the terminal into the panel (`?` lists the
keys; TAB is piano mode). It prints every PAGE / VALUE / LEDS / STATUS / TOAST that RUN4 sends. To reach an iPad or
iPhone:

1. Mac: Audio MIDI Setup -> Window -> Show MIDI Studio -> the Network icon (double-click) -> in "My Sessions" tick
   **Enabled**, and note the session name.
2. Start `tools/fake_fm1.py`, so that "FM-1" exists; in the session window press **+** under Directories if needed.
   The session's inputs/outputs list the ports: the session sends and receives what the virtual "FM-1" ports carry.
3. iPad: connect to the session (RUN4 shows Network MIDI sessions among its MIDI ports, or connect in another app
   that lists them), then enable the FM-1 port in RUN4's settings.

Self-tests without MIDI: `python3 tools/fake_fm1.py --selftest` (part of `make test`); `--loopback` makes a CoreMIDI
round trip in one process. python-rtmidi does not build here (RtMidi lacks `sem_timedwait` on this macOS), so
`tools/coremidi.py` binds CoreMIDI through ctypes. The interactive mode was run only through a pseudo-terminal, not
against RUN4.

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
