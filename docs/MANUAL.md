# RUN4 CTL

RUN4 CTL is a firmware for the M-VAVE FM-1. It turns the FM-1 into a hardware controller for the iOS app RUN4: the 27
keys, 14 buttons, 7 encoders and the MASTER knob are sent to the app over USB-MIDI, and the app draws its pages and
lights on the FM-1's 240 x 240 screen and LEDs. The FM-1 makes no sound in this mode.

RUN4 CTL is an independent project. It is not made or supported by M-VAVE.

## What it does

* **Keys** play notes (MIDI channel 1 by default) with velocity, whether or not the app is there. F3 is the lowest key;
  OCT- and OCT+ shift the keyboard by octaves (-3 to +3).
* **Buttons, encoders and MASTER** are sent to RUN4, which decides what they do on the current page.
* **Screen**: a header in the colour of the current RUN4 track, four tiles (name, value, bar), and a footer with the
  page dots, tempo, bar and beat, transport and octave.
* **LEDs** show what RUN4 tells them to. Without the app, a held key lights itself.
* Keys still play without the app. If the app is not there, the screen says NOT CONNECTED and the notes go on.

## Install

You need a Chrome or Edge browser (Web MIDI) and a USB cable.

1. **Back up first.** Installing replaces M-VAVE's firmware, including your presets. Save the presets you want to keep
   with M-VAVE's own tools before you start.
2. Connect the FM-1 and open the installer page of this firmware (`install/` in the site folder, published by the
   project owner). Follow its steps. The command-line installer in fm1-base (`tools/fm1_install.py`) does the same.
3. After the install the FM-1 restarts and shows RUN4 CTL.

**Going back to stock.** Install M-VAVE's own firmware package (V15) with the same installer: it accepts M-VAVE's
package. RUN4 CTL's own settings (SETUP page) are not part of it and are simply gone afterwards.

## Connect to RUN4

Plug the FM-1 into the iPad or iPhone (or use a Mac session), open RUN4 and enable the FM-1 in its settings. The FM-1
sends HELLO every second until RUN4 answers; then the screen switches to the RUN4 page. Unplug and replug: it starts
over.

## Button map

The FM-1's controls keep the same meaning on every page. The **fixed layer**:

| Control | Use |
| --- | --- |
| ALGORITHM encoder (track) | choose the track |
| PRESETS encoder | choose the sub-page |
| SELECT encoder | choose the bank |
| K1 to K4 | the four values on screen |
| MASTER | RUN4's master level |
| PLAY, REC | transport |
| SAVE | Undo |
| OCT-, OCT+ | shift the keys by an octave (local); also sent to RUN4 |
| SEL | SHIFT: hold it for the second function of the other buttons |

The **page buttons** open the page of the same name: HOME, ENV, LFO, FX, EDIT, GLO, ARP, SEQ.

What each page shows is decided by RUN4 and by the track you are on.

## SETUP page

This page lives in the firmware, not in RUN4. Hold **SEL** and press **GLO** to open it (it works with or without the
app). K1 to K4 change the four tiles, any page button leaves it. Nothing you do on it is sent to RUN4 and the settings
are kept when you power the FM-1 off.

| Tile | Values |
| --- | --- |
| K1 VELOCITY | SOFT, NORMAL, HARD, FIXED 100 |
| K2 AFTERTOUCH | OFF (default), POLY, CHANNEL |
| K3 LED LEVEL | OFF, LOW, HIGH (the dim LEDs) |
| K4 KEY CHANNEL | MIDI channel 1 to 16 (default 1) |

Aftertouch is measured with the keys' own sensors. The velocity and pressure curves were set up without a device; if a
setting feels too soft or too hard, pick another curve.

## Help

* The keys do not play: check the USB cable, and that the host shows an input called RUN4 CTL FM-1.
* The screen says NOT CONNECTED: RUN4 has not answered. Open RUN4 and enable the FM-1.
* Everything about the wire protocol is in `docs/PROTOCOL.md`.

## Licence

GPL-3.0-only. Parts from fm1-base and felucca-bsp stay under their own licences (MPL-2.0); see `NOTICE`.
