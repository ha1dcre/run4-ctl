# RUN4 CTL

**Turn an M-VAVE FM-1 into a hands-on controller for RUN4 on iPad and iPhone. Build your own controller with the free, MIT-licensed SDK.**

[Download the SDK](https://github.com/ha1dcre/run4-ctl/releases/tag/sdk-v0.1.0) · [Firmware for RUN4 1.9](https://github.com/ha1dcre/run4-ctl/releases/tag/v0.9.4) · [FM-1 manual](docs/MANUAL.md)

## Build a controller for RUN4

Have an FM-1 firmware project, a MIDI controller with encoders, or a board with a display? Add a RUN4 page alongside your instrument. RUN4 sends parameter names, values, transport state and LED feedback. Your device sends the controls you map and draws the interface however you like.

- **Start with a working C99 session.** It handles the handshake, MIDI/SysEx parsing, parameter pages, lists and keyboard MIDI. It uses static allocation and supports C++ linkage.
- **Bring your own transport and UI.** Map your buttons, encoders and keys, connect your MIDI input and output, and draw the page on your own display.
- **Keep your instrument.** Add controller mode alongside your existing synth, sequencer or firmware. The SDK is a standalone package with its own MIT licence.

### Run the example on your computer

Download [`run4ctl-sdk-0.1.0.zip`](https://github.com/ha1dcre/run4-ctl/releases/download/sdk-v0.1.0/run4ctl-sdk-0.1.0.zip), optionally verify it with the [SHA-256 file](https://github.com/ha1dcre/run4-ctl/releases/download/sdk-v0.1.0/run4ctl-sdk-0.1.0.zip.sha256). Extract it and run the example.

```sh
cd run4ctl-sdk-0.1.0
make -C sdk test example
```

The example runs a controller and a small host in the same process. Watch them exchange a Tape page, knob input and the corresponding value update. The final line is `minimal example: OK`. You need Make, Python 3, a C99 compiler and a C++11 compiler for the linkage test. The example runs entirely on your computer.

[Integration guide](https://github.com/ha1dcre/run4-ctl/blob/sdk-v0.1.0/sdk/INTEGRATION.md) · [Minimal example](https://github.com/ha1dcre/run4-ctl/blob/sdk-v0.1.0/sdk/examples/minimal.c) · [Public C API](https://github.com/ha1dcre/run4-ctl/blob/sdk-v0.1.0/sdk/include/run4ctl.h)

The session helper advertises **controller profile 1.2**. The bundled codec covers protocol **1.8**. Advanced features such as Tape and piano-roll views require additional device integration. The download includes the full protocol specification. [Controller protocol basics](docs/PROTOCOL.md)

## Use the FM-1 with RUN4

**RUN4 1.9 uses the USB firmware package from [RUN4 CTL 0.9.4](https://github.com/ha1dcre/run4-ctl/releases/tag/v0.9.4).** Connect an M-VAVE FM-1 over USB and open **Settings → MIDI → FM-1 Firmware** in RUN4. Back up your device before changing its firmware. [FM-1 manual](docs/MANUAL.md)

RUN4 mode gives you the app's parameters, transport and Tape waveform on the hardware. The firmware also has standalone **Synth**, **AMB** and **DUB** modes. On the FM-1, **SEL + HOME** cycles through them.

## Firmware development and licences

[Build and test instructions for 0.9.4](https://github.com/ha1dcre/run4-ctl/blob/v0.9.4/README.md) · [iOS installer source](https://github.com/ha1dcre/run4-ctl/tree/v0.9.4/tools/ios) · [Credits and third-party notices](NOTICE)

The firmware is GPL-3.0. The standalone SDK, wire protocol and its codec are [MIT-licensed](https://github.com/ha1dcre/run4-ctl/blob/sdk-v0.1.0/LICENSE-MIT). The iOS installer source is MPL-2.0. See [NOTICE](NOTICE) for project credits and component licences.

RUN4 CTL is an independent project built on [fm1-base](https://github.com/charlesvestal/fm1-base). M-VAVE is the maker of the FM-1. For your own device or firmware, use your own name and describe its compatibility as **RUN4 CTL protocol support**.
