# tools/ios: the RUN4 app's FM-1 installer (MPL-2.0)

`FM1Package.swift` and `FM1Installer.swift` are the Swift source of the installer inside the iOS app RUN4
(Settings > MIDI > FM-1 Firmware), published here so the MPL-2.0 source goes with the firmware it installs. They are a
Swift port of fm1-base's `base/tools/fm1_install.py`, `fm1_backup.py`, `fm1ports.py` and `fm1pkg_make.py`, following
felucca-bsp's `chip/ota.c` and `loader/ldr_core.c` (Copyright (C) 2026 Charles Vestal; Copyright (C) 2026 Leo Kuroshita
(@kurogedelic), Hügelton Instruments). Each file is MPL-2.0 (`LICENSES/MPL-2.0.txt`), its header says so, and so
does `test/FakeFM1.swift` (a Swift port of the simulated FM-1 in `base/tests/install_test.py`).

The files are Foundation-only (`FM1Package.swift` also uses CryptoKit) and talk to the FM-1 through two small protocols,
`FM1Link` and `FM1Backend`; the app implements them with CoreMIDI. What is not here is the app's own code (the rules, the
download and the sheet): it is not MPL and not part of this repository. The tests run in the RUN4 repository
(`scripts/fm1_installer_test`): the Swift installer against `FakeFM1`, and a transcript check that it sends the same
requests as `fm1_install.py`.

The copies here are the app's at the time of the commit; the app's files are the originals.
