#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RUN4 CTL's USB backup, over fm1-base's tool (base/tools/fm1_backup.py, as a library): the same commands, and the
upgrade of an older RUN4 CTL's backup.

    python3 tools/r4ctl_backup.py backup FILE [--port NAME]
    python3 tools/r4ctl_backup.py restore FILE [--yes] [--port NAME]     (a version 1 or 2 file is upgraded on the way)
    python3 tools/r4ctl_backup.py upgrade OLD NEW                        (version 1 or 2 -> 3, for the web backup page)
    python3 tools/r4ctl_backup.py downgrade NEW OLD [VERSION]            (the tests: 3 -> 1 or 2, as that firmware made it)
    python3 tools/r4ctl_backup.py info FILE
    python3 tools/r4ctl_backup.py --selftest

The image (app/app.h APP_BACKUP_*): magic R4CB. Version 1 (firmware up to 0.6): SETUP, the settings, two banks, four
pattern objects, the Bluetooth record (objects 1, 2, 3, 4, 6, 7, 8, 9, 5). Version 2 (firmware 0.7, 0.8): the same and
AMB mode's pieces (object 10) at the end. Version 3 (firmware 0.9): the same and DUB mode's pieces (object 11) after
it. fm1-base's firmware restores only its own version, so an older file becomes version 3 here: every part as it was,
the parts it lacks absent (a restore makes them absent: those modes' recipes, as before they existed). The firmware
without AMB and DUB mode (app-noamb) makes and takes version 3 too.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "tools"))
import fm1_backup as fb  # noqa: E402

MAGIC = b"R4CB"
OBJS_V1 = [1, 2, 3, 4, 6, 7, 8, 9, 5]
OBJ_AMB, OBJ_DUB = 10, 11
OBJS_V2 = OBJS_V1 + [OBJ_AMB]
OBJS_V3 = OBJS_V2 + [OBJ_DUB]
OBJS = {1: OBJS_V1, 2: OBJS_V2, 3: OBJS_V3}
VERSION = 3
NAMES = {1: "SETUP", 2: "Synth settings", 3: "user bank A", 4: "user bank B", 5: "Bluetooth", 6: "patterns 1-4",
         7: "patterns 5-8", 8: "patterns 9-12", 9: "patterns 13-16", 10: "AMB pieces", 11: "DUB pieces"}
KIND = fb.Kind(magic=MAGIC, version=VERSION, objs=OBJS_V3, names=lambda o: NAMES.get(o, f"object {o}"),
               identity="FM-1_74", name="RUN4 CTL")
KINDS = {v: fb.Kind(magic=MAGIC, version=v, objs=o, names=KIND.names, identity="FM-1_74", name="RUN4 CTL")
         for v, o in OBJS.items()}


def version_of(image):
    if len(image) < 8 or image[:4] != MAGIC:
        raise fb.BackupError("not a RUN4 CTL backup")
    return struct.unpack_from("<H", image, 4)[0]


def upgrade(data):
    """a RUN4 CTL backup file (version 1, 2 or 3, with any sections after it) -> version 3"""
    image, sections = fb.split(data)
    ver = version_of(image)
    if ver == VERSION:
        fb.parse(image, KIND)
        return bytes(data)
    if ver not in OBJS:
        raise fb.BackupError(f"a backup of version {ver}: this tool knows 1, 2 and 3")
    flags, objs = fb.parse(image, KINDS[ver])
    have = [o for o, _ in objs]
    objs = objs + [(o, b"") for o in OBJS_V3 if o not in have]     # (the parts that version had not: absent)
    return fb.join(fb.build(objs, flags=flags, kind=KIND), sections)


def downgrade(data, to=1):
    """(the tests) a version 3 file -> version `to` as that firmware made it (the parts it had not left out)"""
    image, sections = fb.split(data)
    flags, objs = fb.parse(image, KIND)
    keep = OBJS[to]
    return fb.join(fb.build([o for o in objs if o[0] in keep], flags=flags, version=to, kind=KIND), sections)


def selftest():
    n = 0

    def eq(a, b, what):
        nonlocal n
        if a != b:
            raise SystemExit(f"r4ctl_backup selftest: {what}: {a!r} != {b!r}")
        n += 1
    parts = [(o, bytes([o]) * (o * 7)) for o in OBJS_V1]
    v1 = fb.build(parts, flags=1, version=1, kind=KIND)
    v3 = upgrade(v1)
    eq(version_of(v3), 3, "upgraded version")
    flags, objs = fb.parse(v3, KIND)
    eq(flags, 1, "flags kept")
    eq([o for o, _ in objs], OBJS_V3, "objects in order")
    eq(objs[-2][1] + objs[-1][1], b"", "the AMB and DUB parts absent")
    eq(objs[:-2], parts, "every part as it was")
    eq(upgrade(v3), v3, "a version 3 file stays")
    eq(downgrade(v3), v1, "down and up again")
    amb = (OBJ_AMB, b"\x0a" * 476)
    v2 = fb.build(parts + [amb], flags=0, version=2, kind=KIND)
    flags, objs = fb.parse(upgrade(v2), KIND)
    eq([o for o, _ in objs], OBJS_V3, "version 2: objects in order")
    eq(objs[-2], amb, "version 2: the AMB part kept")
    eq(objs[-1][1], b"", "version 2: the DUB part absent")
    eq(downgrade(upgrade(v2), 2), v2, "version 2: down and up again")
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
    if (argv[:1] == ["upgrade"] and len(argv) == 3) or (argv[:1] == ["downgrade"] and len(argv) in (3, 4)):
        try:
            data = open(argv[1], "rb").read()
            to = int(argv[3]) if len(argv) == 4 else 1
            if argv[0] == "downgrade" and to not in (1, 2):
                raise fb.BackupError("downgrade to version 1 or 2")
            out = upgrade(data) if argv[0] == "upgrade" else downgrade(data, to)
        except (OSError, fb.BackupError) as e:
            print(f"{argv[1]}: {e}", file=sys.stderr)
            return 2
        open(argv[2], "wb").write(out)
        print(f"{argv[2]}: a version {version_of(out)} backup ({len(out)} bytes)")
        return 0
    if argv[:1] == ["restore"] and len(argv) >= 2 and os.path.exists(argv[1]):
        try:
            data = open(argv[1], "rb").read()
            ver = version_of(fb.split(data)[0])
            if ver in (1, 2):                          # an older RUN4 CTL's: upgraded on the way
                tmp = argv[1] + ".v3"
                open(tmp, "wb").write(upgrade(data))
                print(f"{argv[1]}: a version {ver} backup (firmware {'0.6 and older' if ver == 1 else '0.7, 0.8'}): "
                      f"restored as version 3 ({tmp})")
                argv = ["restore", tmp] + argv[2:]
        except (OSError, fb.BackupError) as e:
            print(f"{argv[1]}: {e}", file=sys.stderr)
            return 2
    return fb.main(argv, kind=KIND)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) or 0)
