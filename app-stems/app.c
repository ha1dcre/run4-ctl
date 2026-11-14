/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL STEMS: the same firmware (app/app.c) built with six USB audio input channels (fm1.toml usb_audio_channels =
 * 6): 1-2 the mix, 3-4 the dry voices, 5-6 the effects' return. A separate package so the default one stays a stereo
 * USB device (what RUN4 and most hosts expect); fm1-base fixes the channel count when it builds. */
#include "../app/app.c"
