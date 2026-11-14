#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RUN4 CTL's USB backup, over fm1-base's tool (base/tools/fm1_backup.py, as a library): the same commands, and the
upgrade of an older RUN4 CTL's backup.

    python3 tools/r4ctl_backup.py backup FILE [--port NAME]
    python3 tools/r4ctl_backup.py restore FILE [--yes] [--port NAME]     (a version 1 file is upgraded on the way)
    python3 tools/r4ctl_backup.py upgrade OLD NEW                        (version 1 -> 2, for the web backup page)
    python3 tools/r4ctl_backup.py info FILE
    python3 tools/r4ctl_backup.py --selftest

The image (app/app.h APP_BACKUP_*): magic R4CB. Version 1 (firmware up to 0.6): SETUP, the settings, two banks, four
pattern objects, the Bluetooth record (objects 1, 2, 3, 4, 6, 7, 8, 9, 5). Version 2 (firmware 0.7): the same and AMB
mode's pieces (object 10) at the end. fm1-base's firmware restores only its own version, so a version 1 file becomes
version 2 here: every part as it was and the AMB part absent (a restore makes it absent: AMB mode's recipes, as before
it existed). The firmware without AMB mode (app-noamb) makes and takes version 2 too.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "tools"))
import fm1_backup as fb  # noqa: E402

MAGIC = b"R4CB"
OBJS_V1 = [1, 2, 3, 4, 6, 7, 8, 9, 5]
OBJ_AMB = 10
OBJS_V2 = OBJS_V1 + [OBJ_AMB]
NAMES = {1: "SETUP", 2: "Synth settings", 3: "user bank A", 4: "user bank B", 5: "Bluetooth", 6: "patterns 1-4",
         7: "patterns 5-8", 8: "patterns 9-12", 9: "patterns 13-16", 10: "AMB pieces"}
KIND = fb.Kind(magic=MAGIC, version=2, objs=OBJS_V2, names=lambda o: NAMES.get(o, f"object {o}"),
               identity="FM-1_74", name="RUN4 CTL")
KIND_V1 = fb.Kind(magic=MAGIC, version=1, objs=OBJS_V1, names=KIND.names, identity="FM-1_74", name="RUN4 CTL")


def version_of(image):
    if len(image) < 8 or image[:4] != MAGIC:
        raise fb.BackupError("not a RUN4 CTL backup")
    return struct.unpack_from("<H", image, 4)[0]


def upgrade(data):
    """a RUN4 CTL backup file (version 1 or 2, with any sections after it) -> version 2"""
    image, sections = fb.split(data)
    ver = version_of(image)
    if ver == 2:
        fb.parse(image, KIND)
        return bytes(data)
    if ver != 1:
        raise fb.BackupError(f"a backup of version {ver}: this tool knows 1 and 2")
    flags, objs = fb.parse(image, KIND_V1)
    return fb.join(fb.build(objs + [(OBJ_AMB, b"")], flags=flags, kind=KIND), sections)


def downgrade(data):
    """(the tests) a version 2 file -> version 1 as firmware 0.6 made it (the AMB part left out)"""
    image, sections = fb.split(data)
    flags, objs = fb.parse(image, KIND)
    return fb.join(fb.build([o for o in objs if o[0] != OBJ_AMB], flags=flags, version=1, kind=KIND), sections)


def selftest():
    n = 0

    def eq(a, b, what):
        nonlocal n
        if a != b:
            raise SystemExit(f"r4ctl_backup selftest: {what}: {a!r} != {b!r}")
        n += 1
    parts = [(o, bytes([o]) * (o * 7)) for o in OBJS_V1]
    v1 = fb.build(parts, flags=1, version=1, kind=KIND)
    v2 = upgrade(v1)
    eq(version_of(v2), 2, "upgraded version")
    flags, objs = fb.parse(v2, KIND)
    eq(flags, 1, "flags kept")
    eq([o for o, _ in objs], OBJS_V2, "objects in order")
    eq(objs[-1][1], b"", "the AMB part absent")
    eq(objs[:-1], parts, "every part as it was")
    eq(upgrade(v2), v2, "a version 2 file stays")
    eq(downgrade(v2), v1, "down and up again")
    with_sec = fb.join(v1, [("XTRA", b"abc")])
    eq(fb.split(upgrade(with_sec))[1], [("XTRA", b"abc")], "sections kept")
    bad = bytearray(v1)
    bad[30] ^= 1
    try:
        upgrade(bytes(bad))
        eq(0, 1, "a damaged file refused")
    except fb.BackupError:
        n += 1
    print(f"r4ctl_backup selftest: {n} checks ok")


def main(argv):
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    if argv[:1] in (["upgrade"], ["downgrade"]) and len(argv) == 3:
        try:
            data = open(argv[1], "rb").read()
            out = upgrade(data) if argv[0] == "upgrade" else downgrade(data)
        except (OSError, fb.BackupError) as e:
            print(f"{argv[1]}: {e}", file=sys.stderr)
            return 2
        open(argv[2], "wb").write(out)
        print(f"{argv[2]}: a version {version_of(out)} backup ({len(out)} bytes)")
        return 0
    if argv[:1] == ["restore"] and len(argv) >= 2 and os.path.exists(argv[1]):
        try:
            data = open(argv[1], "rb").read()
            if version_of(fb.split(data)[0]) == 1:      # an older RUN4 CTL's: upgraded on the way
                tmp = argv[1] + ".v2"
                open(tmp, "wb").write(upgrade(data))
                print(f"{argv[1]}: a version 1 backup (firmware 0.6 and older): restored as version 2 ({tmp})")
                argv = ["restore", tmp] + argv[2:]
        except (OSError, fb.BackupError) as e:
            print(f"{argv[1]}: {e}", file=sys.stderr)
            return 2
    return fb.main(argv, kind=KIND)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) or 0)
