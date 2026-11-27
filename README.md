<!-- SPDX-License-Identifier: MIT -->

# RUN4 CTL Controller SDK

Build an independent control surface for RUN4 with the MIT-licensed C99 SDK.
Your device supplies the controls, display and MIDI transport. RUN4 supplies
pages, values, transport state and display feedback.

- [SDK overview](sdk/README.md)
- [Integration guide](sdk/INTEGRATION.md)
- [MIDI stack adapters](sdk/MIDI-STACKS.md)
- [Runnable example](sdk/examples/minimal.c)
- [Wire protocol](docs/PROTOCOL.md)
- [MIT license](LICENSE-MIT)

```sh
make -C sdk test example
```

The example runs with a simulated host. It demonstrates the default controller
profile and opt-in Tape and Poly/TÓLF piano-roll feedback. The source in this
branch builds on its own with no firmware or hardware dependencies.

The previous packaged release is [SDK 0.1.0](https://github.com/ha1dcre/run4-ctl/releases/tag/sdk-v0.1.0).
