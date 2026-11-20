# RUN4 CTL

Open-source firmware (GPL-3.0) that turns the **M-VAVE FM-1** into a thin hardware controller for the iOS app RUN4.
Its 27 keys, 14 buttons, 7 encoders and the MASTER pot are sent to the app over USB-MIDI, and the app draws pages (four
labelled values with bars, track colour, transport, a tape view) and the LEDs on the FM-1. The FM-1 is silent in this
mode unless RUN4 plays it as an instrument: the **host voice** (protocol 1.3) lets a RUN4 track play the FM-1's own FM
engine. RUN4 also keeps a backup of the FM-1's user voices. Three more modes play on their own: **Synth mode**, a
six-operator FM synth with a step sequencer; **AMB mode**, AMB-1's generative ambient garden (Charles Vestal,
MPL-2.0); and **DUB mode**, DUB-1's generative dub techno (Santiago Begueria, GPL-3.0-only, release 1.2: chord stabs
into a tape echo, pings, a sub bass, a ducking kick; the master actions Filter, Dub, Energy and Space, PERFORM's mutes,
solos and moves, dub hold and an effects grid). SEL + HOME goes round RUN4 -> SYNTH -> AMB -> DUB. Holding any button
for a second shows a help card.

**Not affiliated with M-VAVE.** FM-1 is M-VAVE's product name; this is an independent project built on the community
firmware platform [fm1-base](https://github.com/charlesvestal/fm1-base). See `NOTICE`.

**Other apps are welcome to speak the protocol** (MIT, `docs/PROTOCOL.md`). The names "RUN4" and "RUN4 CTL" belong to
the RUN4 app; please don't use them for another app or a modified firmware, or in a way that suggests it is RUN4.

Status: **complete in the host simulator, not yet run on an FM-1.** RUN4 mode (protocol v1.3 in `docs/PROTOCOL.md`: codec,
LIST, non-blocking output, SETUP page, touch velocity and aftertouch, the host voice, the voice list, the tape view, the
patch backup) and Synth mode (Felucca's FM6 engine, effects, DX7
SysEx import, arpeggiator, sequencer with parameter locks, trig conditions, generators, phrase tools and a song chain,
scale and chord keys, mutate, an A / B morph, MIDI learn, pages) are implemented and tested in fm1-base's host
simulator and in native tests (`make test`); so are the USB backup, the screen mirror, sample-accurate MIDI in, the
overload guard, LOCAL OUT and the USB stems package; firmware 0.7 adds AMB mode, the Dattorro plate as Synth mode's
reverb and the help cards; firmware 0.8 protocol 1.4: RUN4 lists and switches the modes and remote-controls the local
modes from their self-description (AMB and Synth mode described), the protocol and codec under MIT; protocol 1.5: the
FM-1 tells RUN4 the page it shows and RUN4 picks one (LVIEW, LVIEW_SET), values every 40 ms; firmware 0.9 adds DUB mode
(described to RUN4 as mode 3 with the same 1.4 / 1.5 messages: nothing new on the wire); firmware 0.9.1 drops the USB
audio output; firmware 0.9.2 protocol 1.6: the scene page (SCENES, SCENE_STATE, SCENE_PICK) and 1.7: the piano roll (ROLL); firmware 0.9.3 adds the synth/dub CPU fixes (no protocol change). Bluetooth MIDI is wired in and simulated. **Nothing has been built for or run on the real device**:
the toolchain is Linux x86-64 and needs Docker. User manual: `docs/MANUAL.md`.

## Layout

| Path | What |
| --- | --- |
| `docs/PROTOCOL.md` | the wire protocol v1 (minor 7), MIT |
| `src/r4p.[ch]` | the codec: firmware and native tests share it (no libc, no floats), MIT; `tests/r4p_fuzz.c` its fuzz test |
| `src/lm.c`, `src/synth_lm.c`, `src/amb_lm.c`, `src/dub_lm.c` | protocol 1.4: the local modes for the host (list, switch, description, values, status line), table-driven; Synth mode's, AMB mode's and DUB mode's descriptions |
| `src/ui.c` | the display: state from host frames, redraw by region |
| `src/scenes.c` | RUN4 mode's scene page (protocol 1.6: SEL + SEQ, the white keys recall Sets, OCT- / OCT+ the bank) |
| `src/roll_ui.c` | RUN4 mode's piano roll (protocol 1.7): the host's pattern window, cursor, notes and playhead over compact tiles (the drawing after fmini-1's graph_roll, GPL-3.0, see NOTICE) |
| `src/tape_ui.c` | RUN4 mode's tape view (protocol 1.3): waveform, loop region, playhead and meters in Synth mode's layout |
| `src/synth.c`, `src/synth_seq.c`, `src/synth_ui.c`, `src/synth/` | Synth mode: the FM6 engine (Felucca, Apache-2.0, see NOTICE), voices, effects, arpeggiator, DX7 import, scales and chords, mutate, the morph, sample-accurate MIDI in, the overload guard; the step sequencer (16 x 64 steps, after Felucca's) with parameter locks, trig conditions and the song chain; its pages |
| `src/synth_gen.c`, `src/synth_learn.c` | the SEQ page's generators, phrase tools, copy / paste and undo; MIDI learn and the default CC map |
| `src/amb/amb.[ch]` | AMB mode's engine: AMB-1's (fm1-amb `app/dsp/amb.c` at da0047dc, MPL-2.0, modified: see its header), an o2 unit |
| `src/amb_ui.c` | AMB mode's pages, keys, store (OBJ_AMB) and screen in RUN4 CTL's look |
| `src/dub/dub.[ch]` | DUB mode's engine: DUB-1's (fm1-dub `firmware/src/dsp/amb.c` at v1.2.0, GPL-3.0-only, modified: see its header), an o2 unit |
| `src/dub_ui.c` | DUB mode's pages, keys (scale, PERFORM, the effects grid, the chord slots, the pattern), lights, store (OBJ_DUB) and screen |
| `src/plate.[ch]` | Jon Dattorro's plate (AMB-1's, MPL-2.0, int16 lines): Synth mode's reverb, AMB mode's and DUB mode's, one buffer; an o2 unit |
| `src/arena.h` | the mode arena: Synth's delay / chorus lines, AMB's or DUB's tape loop, Echoplex and buffers (DUB's cloud), in one pool buffer |
| `src/help.c` | the help cards' texts and drawing (the rules of precedence: `app/app.c`, "help cards") |
| `app/` | the fm1-base app: `app.c` (input, local rules, host frames, USB backup hooks, LOCAL OUT, stems), `app.h`, `fm1.toml` |
| `app-stems/` | the RUN4 CTL STEMS package: `app/` built with six USB audio input channels (its own `fm1.toml`) |
| `app-noamb/` | the package without AMB and DUB mode (`R4_AMB 0`, `R4_DUB 0`, see "AMB and DUB mode off") |
| `tests/r4p_test.c`, `tests/synth_test.c` | codec unit tests; native tests of the synth engine (levels, polyphony, DX7 import, CPU) |
| `tests/reverb_ab.c`, `tests/reverb_ref.c` | the reverb A/B (part of `make synth`): the old combs (kept as a reference) against the plate |
| `tests/amb_test.c` | native tests of AMB mode's engine (`make amb`) |
| `tests/dub_test.c` | native tests of DUB mode's engine (`make dub`; writes `tests/audio/dub_demo.wav`) |
| `tools/r4ctl_backup.py` | the USB backup tool over fm1-base's: restores / upgrades a firmware 0.6 (version 1) backup |
| `tests/scenarios/*.txt`, `tests/scenarios_stems/`, `tests/scenarios_noamb/`, `tests/scenarios_cross/`, `tests/run_sim.sh` | host-simulator scenarios (boot, pages, input, robustness; the stems package; the package without AMB and DUB and a backup between the two) |
| `tests/screens/`, `tests/audio/` | screenshots; listening files (the synth; the reverb A/B: `ir_*_hall.wav`, `reverb_*_hall.wav`) |
| `tools/fake_fm1.py`, `tools/coremidi.py` | a Mac stand-in for the FM-1 (virtual CoreMIDI ports "FM-1", terminal panel); standard library only |
| `tools/fake_run4.py` | stand-in for the RUN4 app (frames as hex, simulator scripts); standard library only |
| `base/` | fm1-base (git submodule; it brings felucca-bsp) |

Store (flash data area, two 4 KiB sectors per object, `app/app.h`): DUB mode's pieces 0xC8000 (firmware 0.9, layout 1,
2396 B: the eight pieces as left, each with its ping loops, the piece, TUNE, MIDI OUT, and whether the FM-1 was left in
DUB mode, SETUP keeping SYNTH then as for AMB; +8 KiB of flash; in RAM it lives in the pool), AMB mode's pieces 0xCA000 (firmware 0.7, layout 1,
476 B: the eight pieces as left, the piece, TUNE, MIDI OUT, and whether the FM-1 was left in AMB mode; SETUP keeps
SYNTH then, so an older firmware still reads SETUP; +8 KiB of flash), panel 0xDC000, SETUP 0xDA000 (56 B, layout 2:
LOCAL OUT, the default CC map, 16 MIDI learn bindings; a layout-1 store of 6 B loads with them at their defaults),
Synth settings 0xD8000 (222 B, layout 4: the morph; layouts 2 (186 B) and 3 (198 B) load with the new fields at their
defaults), two user banks
0xD6000 / 0xD4000 (2056 B each), Bluetooth 0xDE000, and the sequencer's 16 patterns in four objects 0xD2000 / 0xD0000 /
0xCE000 / 0xCC000 (layout 2: 3724 B each, 14896 B, still one object per sector pair, so no more flash than before; a
layout-1 object of 2088 B loads unchanged, without locks and chain). Each object holds four patterns (520 B each),
their parameter locks (6 lanes a pattern: a parameter id and a value per step, 392 B) and the song's chain (66 B,
object 0's counts). RAM: patterns 8.3 KB, locks 6.1 KB (6272 B), chain 66 B. A step's trig condition lives in three
spare bits of its flags (no layout change; older firmware clears them on load). No new store object: the new
settings grew two objects inside their own sector pairs (no flash added).

USB backup (`fm1.toml` backup = true, fm1-base `device/backup.c`, `tools/fm1_backup.py`): the image's magic is
"R4CB", version 3 since firmware 0.9 (2 since 0.7), and holds SETUP, the settings, both banks, the four pattern
objects, the Bluetooth record, AMB mode's pieces and DUB mode's pieces in that order (never the panel's calibration).
fm1-base restores only its own version, so a version 1 image (0.6 and older: the same without the AMB and DUB parts)
or a version 2 image (0.7, 0.8: without the DUB part) goes through `tools/r4ctl_backup.py` (`restore` upgrades it on
the way, `upgrade OLD NEW` for the web page): the parts it lacks absent, those modes at their recipes; the simulator
restores such upgraded images (`30_backup_v1`, `35_backup_v2`) and refuses the raw ones. A new layout of an existing object
needs no new version (each loads its older layouts). The restore borrows the store's 3840-byte scratch `r4_io`
(`APP_BACKUP_BUF`), which also took the place of two 3724-byte pattern buffers.

RAM added by this round (sizes measured natively; the device's linker map not made, no toolchain here): pool +1,056 B
(a 128-byte carry per voice for sample-accurate note starts; the voices 3,488 B in all), the app's pool 112,044 B in
all (the DX7 import buffer 4,108, the voices, the effects' lines 104,448); .bss about -1.1 KB net (pattern undo 915,
clipboard 903, the timed MIDI queue 192, the morph 48 + 2 x 24 in the two setting sets, MIDI learn ~290, SETUP +50,
minus 3,608 for the shared scratch). fm1-base's parts: the mirror 9,800 B of pool (row hashes 7,680, frame buffers
2,120) and 5.1 KB of .bss (its 4 KiB ring, the frame's hashes); the backup ~0.5 KB of .bss and no pool (the scratch is
lent). Pool estimate: the canvas 69,120 + the app 112,044 + the mirror 9,800 + the words: about 192 KB of the 336 KiB
pool, ~150 KB spare (fm1-base insists on 8 KiB); the build's own `pool headroom` line is the authority once it runs.
The STEMS package adds 4.5 KB of .bss (the USB ring at six channels, 6,144 B instead of 2,048, and its packet buffer).

Sample-accurate MIDI in (`midi_stamp`): measured in `tests/synth_test.c` over 64 notes arriving at random points of a
block: onset jitter 5.60 ms (as before: taken at the next block's start), 0 frames placed (a constant 257-frame
latency); MIDI clock F8s 551 frames apart measured 524..588 before, 551 placed. The simulator's MIDI is stamped when
read, so `midiat FRAME ..` stands in for a stamped message there.

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
moving, the sum is no more than Synth mode with its scope. The FM-1 is a USB audio input only (firmware 0.9.1,
`usb_audio_out = false`): the iPad records the FM-1 and plays RUN4 on its own speaker or headphones.
RAM added: the parked settings (186 B); the restore's 32 voices wait in the DX7 import's 4 KiB buffer.

RAM of firmware 0.7 (sizes measured natively, `make amb` prints them; no device link map, no toolchain here): the
mode arena 150,496 B of pool (AMB's tape loop 124,000, Echoplex 14,200, sine table and mix buffers 12,296; Synth's
delay and chorus lines, 67,584 B, live in the same bytes, which is all Synth mode needs), the plate's lines 69,412 B
of pool (the old reverb's combs and allpasses, 36,864 B, are gone): the app's pool 227,504 B (0.6: 112,044). With the
canvas (69,120) and the mirror (9,800): about 306 KB of the 336 KiB pool, ~37 KB spare (fm1-base wants 8 KiB); the
build's `pool headroom` line is the authority. .bss: AMB's engine ~2.0 KB, its UI and store ~0.5 KB, the cards ~20 B,
the plate's state ~0.2 KB. AMB's 5.6 s tape loop fits at full length: nothing had to be shortened. The arena changes
hands at a mode switch with the render held silent: Synth's lines and the plate cleared for Synth / RUN4, AMB's
buffers set up for AMB (`app.c` mode_switch); the overload guard never touches the arena while AMB has it.

CPU (this Mac, -O2): AMB mode 0.43 % of real time at its busiest piece (VOICES, everything up; step 2 0.35 %, step 4
0.30 %); in the simulator 0.4 %. The plate alone 0.10 % against the old reverb's 0.08 % (+0.02 % of real time); Synth
mode's whole engine with 8 voices and every effect 0.58 % (0.6: 0.63 %, the difference is noise). On the device not
measured; AMB-1 runs the same engine (with float lines) on the same hardware.

Protocol 1.7, the piano roll (`src/roll_ui.c`, drawn while RUN4 sends a pattern window): RAM about 0.4 KB of .bss (the
last window, 64 notes of 5 B, and its state; `expect roll_ram` in `37_roll`), no pool; r4p_msg_t, which app_sysex keeps
on the stack, grew from 312 to 364 B (the window). Not measured on a device.

Protocol 1.6, the scene page (`src/scenes.c`, SEL + SEQ in RUN4 mode with a RUN4 that offers Sets): RAM 1,168 B of .bss (the 128
short names of 8 characters, 9 B each, and a few bytes of state; `expect scn_ram` in `36_scenes`), no pool, no arena; the
codec grew by about 0.8 KB (clang -Os thumbv7em as a stand-in: 14,600 -> 15,436 B); the page itself is a few KB of text.
Not measured on a device (no JieLi toolchain here).

Protocol 1.4 and 1.5 (firmware 0.8, `src/lm.c`): RAM 944 B of .bss (the shadow of the values last sent, 160 x 2 B, the frame
being built, the state; measured natively, `expect lm_ram` in `31_modes`), no pool; the descriptions are tables in
flash. Code: the codec grew 5,170 -> 7,798 B (arm-none-eabi-gcc -Os Thumb-2, as a stand-in for the JieLi toolchain);
the whole of 1.4 adds 28 KB to the host simulator's x86-64 -O2 text (about a third of that expected on the device: not
measured, no device toolchain here). A description is worked out once per mode for its hash (encoding it whole, in
the main loop, at the first MODE of that mode).

DUB mode (firmware 0.9; sizes measured natively and with arm-none-eabi-gcc -O2 Thumb-2 as a stand-in for the JieLi
toolchain, `make dub` prints the arena's; no device link map: the CI's device build is the authority). DUB shares AMB's
mode arena, which grows to DUB's 163,296 B (+12,800: its cloud's line 16,384 B is new, its nine mix buffers of 128
samples take 4,608 B where AMB's eight of 256 take 8,192; the tape loop 124,000 and the Echoplex 14,200 are AMB's
sizes, the sine table 4,104); the plate is the shared one. Pool: the arena +12,800, the DUB store 2,396, the engine's
command queue 1,024: about +16.2 KB, ~322.6 KB of the 344,064 B pool, ~21 KB spare (fm1-base wants 8 KiB). RAM
.data + .bss: the engine's state ~4.0 KB (.bss) and ~0.15 KB (.data), its UI and the protocol's DUB entry ~0.1 KB:
about +4.3 KB, ~89.7 KB of 96 KiB, ~8.5 KB spare. Flash: the engine ~62 KB with its tables (the stand-in; AMB's engine
is ~26 KB there), the UI and its description 15..35 KB (the host simulator's x86-64 text grows by 119 KB in all, the
engine's 78 KB of it): about +80..100 KB, ~380..400 KB of the 581,564 B slot. CPU on
this Mac: 0.60 % of real time at its busiest (Haze, Cloud and the pings up; the overload guard's step 2 0.58 %, step 4
0.55 %), AMB 0.43 %; on the device not measured (DUB-1 runs the same engine on the FM-1 at 360 MHz). The overload guard
sheds in four steps: the plate's input diffusers, the cloud, two of the six pings and the crackle, one saw a chord
voice. DUB's description: 20 pages, 135 parameters (the drum pattern's steps and accents among them, two MASKs of
eight a lane, ids 160..179), 239 frames (`34_dub_modes`).

### AMB and DUB mode off

AMB mode and DUB mode are build-time switches, `R4_AMB` and `R4_DUB` (`app/app.h`, default 1 each; SEL + HOME skips a
mode that is off, and the arena is the largest of the modes built). The package without them is `app-noamb/` (`app/`
with `R4_AMB 0` and `R4_DUB 0`; the engines' o2 units are in its `fm1.toml` no more): `python3 base/tools/build.py
app-noamb` (simulator: `app-noamb --host`, run by `make sim`). Without them: SEL + HOME goes RUN4 -> SYNTH -> RUN4, no
AMB or DUB code, cards or RAM (the arena is Synth's 67,584 B: the app's pool 144,592 B); their store objects stay in
the store map and in the backup image (version 3) untouched, so a backup goes from one package to the other and back
(`tests/scenarios_noamb/`, `tests/scenarios_cross/`). The plate and the help cards are in both. (DUB mode needs AMB's
arena and more, so a package with DUB and without AMB would save no RAM: there is none.)

## Build and test (macOS or Linux, host only)

    git clone --recursive <this repo>          # or: git submodule update --init --recursive
    python3 -m venv .venv && .venv/bin/pip install pillow    # fm1-base's font generator needs Pillow
    make test        # codec unit tests and 1e6 fuzzed frames (ASan/UBSan), synth tests (with the reverb A/B), AMB and DUB tests, the simulator
                     # scenarios (all three packages), the fake FM-1's and the backup tool's self-tests
    make audio       # the listening files in tests/audio/ (the reverb A/B)
    make unit        # only the codec tests
    make sim         # only the simulator (builds app/build/host/run4ctl_host)

`tools/hostshim/` drops OpenType features when Pillow lacks libraqm ; it only affects the
simulator's font generation (digit widths), never the device build.

Browser emulator (play it with mouse, computer keyboard or a MIDI keyboard; needs Emscripten, e.g. emsdk in `~/emsdk`
— not on an exFAT volume, it needs symlinks): `source ~/emsdk/emsdk_env.sh && python3 base/tools/build.py app --web`,
then `cd app/build/emu && python3 -m http.server 8741` and open http://127.0.0.1:8741 in Chrome (Web MIDI with SysEx).

The STEMS package: `python3 base/tools/build.py app-stems` (the simulator: `app-stems --host`, run by `make sim`).

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

## FM-1 hardware compatibility

The same RUN4 CTL package supports both factory flash chips:

| Flash chip | JEDEC ID | Support |
|---|---|---|
| Puya P25Q80 (original units) | `856014` | Included |
| UCUN UC25HQ80 (newer units) | `B36014` | Included |

Both are 1 MiB SPI NOR parts. This support is already in v0.9.3's pinned
fm1-base revision `c8e05997ce970875038156d2bbd69e712eac1543`; no separate
new-chip package is needed. V15 and V16 are M-VAVE firmware versions and do
not identify the flash chip. RUN4's installer shows the chip when the running
firmware reports its JEDEC ID.

`make flashcompat` checks the production storage allowlist, persistence,
app/loader handshakes for both IDs, and simulated USB recovery with both
V15 and V16 boot headers. Unknown flash IDs stay refused. CI runs these and
the update-loader protocol tests before building, then verifies the hardware
metadata of all four packages (full, USB-only, no-AMB and STEMS).
These are host-side tests: physical testing so far is on a V15 unit; newer-chip
hardware operation and persistence after a cold power cycle still need a device test.

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
   (needs `pip install mido python-rtmidi` in a venv). With an FM-1 connected to an iPad or iPhone, the RUN4 app
   installs RUN4 CTL itself (Settings > MIDI > FM-1 Firmware: backup, download, install, resume of a cut-off install;
   its Swift source is `tools/ios/`, MPL-2.0). Back up first either way.

   **Going back to M-VAVE's firmware.** M-VAVE's V15 package (`fm1_open/sdk/firmware/FM-1_015.fwsc`) can be installed
   only on an FM-1 that has never run V16: M-VAVE's V16 rewrites the flash's boot head, and the FM-1 refuses V15 on
   a V16 head (it checks the package before anything is written, so nothing changes). A unit that ever ran V16
   goes back to V16 with M-VAVE's own M-UPGRADE tool. A unit that shipped with V16 installs RUN4 CTL normally; its
   way back is V16 too. RUN4 CTL's data area (flash `0xC8000..0xE0000`, `app/app.h` `APP_STORE_MAP`: two 4 KiB sectors
   per object) does not overlap stock's image: V16's ends at `0xA9000`, V15's at `0x93000` (felucca-bsp
   `tools/fm1_rescue.py`, `docs/FLASH.md`), and the updater writes only the application window below
   `0x97000` and its own update records (`0xE0000..`). The settings and patterns RUN4 CTL keeps are not part of any
   M-VAVE package and are gone after the way back: take the USB backup first.

## Licence

GPL-3.0 (`LICENSE`). Files from fm1-base and felucca-bsp, and AMB-1's engine and plate (`src/amb/`, `src/plate.[ch]`),
stay MPL-2.0, file by file (see `NOTICE`, `LICENSES/MPL-2.0.txt`). DUB-1's engine (`src/dub/`) is GPL-3.0-only, as
DUB-1 is (Santiago Begueria, https://github.com/sbegueria/fm1-dub, release v1.2.0; see `NOTICE`).

**The protocol and its codec are MIT** (`LICENSE-MIT`): `docs/PROTOCOL.md`, `src/r4p.h`, `src/r4p.c` (and the codec's
fuzz test `tests/r4p_fuzz.c`), so other firmwares and devices can implement RUN4 CTL (`docs/PROTOCOL.md`, "Implementing
RUN4 CTL in your own firmware or device"). They are original work of this project; nothing in them comes from
fm1-base, felucca-bsp, Felucca, AMB-1, DUB-1 or Mutable Dreams. The rest of the firmware stays GPL-3.0.

### Firmware 0.9.4 / protocol 1.8

RUN4 can now acquire a temporary USB-only monitoring lease after it has opened
the FM-1 input. VOICE, Synth, AMB and DUB reach the app at unity before the FM-1
MASTER control, while its direct speaker/headphone output is muted. MASTER still
controls RUN4's app master once. Local monitoring returns after four seconds
without renewal, USB loss or explicit release; saved LOCAL OUT is preserved.
Older app/firmware combinations retain the manual SETUP > AUDIO > LOCAL OUT
USB ONLY setting. Both supported flash chips and all four package variants remain
covered by the compatibility checks. Physical device verification is pending.
