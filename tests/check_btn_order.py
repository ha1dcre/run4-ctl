#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Checks a simulator log (scenario 05): the BTN frames in the final sysex_out dump are 24 taps in order
(ids 0..13, 0..9, each down then up), none lost, none doubled."""
import sys
frames = []
for line in open(sys.argv[1]):
    if line.strip().startswith("sysex<"):
        frames.append([int(x, 16) for x in line.split("<", 1)[1].split()])
btn = [(f[5], f[6]) for f in frames if f[:5] == [0xF0, 0x7D, 0x52, 0x34, 0x02]]
want = []
for i in range(24):
    want += [(i % 14, 1), (i % 14, 0)]
if btn != want:
    print("FAIL btn order: got %d frames, first difference at %s" % (
        len(btn), next((i for i, (a, b) in enumerate(zip(btn, want)) if a != b), min(len(btn), len(want)))))
    sys.exit(1)
print("  ok btn order: %d frames in order" % len(btn))
