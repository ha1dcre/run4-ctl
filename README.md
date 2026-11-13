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

The device build (`python3 base/tools/build.py app`) needs fm1-base's Docker/JieLi toolchain and has not been tried.

## Licence

GPL-3.0 (`LICENSE`). Files from fm1-base and felucca-bsp stay MPL-2.0 (see `NOTICE`).
