#!/usr/bin/env python3
"""Check both production FM-1 flash revisions before building/releasing RUN4 CTL.

Uses fm1-base's actual storage allowlist, OTA code and USB rescue implementation.
The same package supports both chips; V15/V16 are stock firmware versions, not
hardware revisions. No connected FM-1 is read or written by these tests.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import tomllib

sys.dont_write_bytecode = True
CHIPS = {0x856014: "Puya P25Q80", 0xB36014: "UC25HQ80"}
VARIANTS = ("app", "app-noamb", "app-stems")


def require(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)


def manifest(path):
    data = json.loads(path.read_text())
    require(set(data["hardware"]["flash"]) == {f"{chip:06X}" for chip in CHIPS},
            f"{path}: hardware.flash must declare exactly both supported chips")
    print(f"PASS metadata: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--artifacts", action="store_true", help="check all four built manifests only")
    args = parser.parse_args()
    root = args.root.resolve()
    base = root / "base"
    if args.artifacts:
        for name in VARIANTS:
            manifest(root / name / "build/firmware.json")
        manifest(root / "app/build/firmware-usb.json")
        return

    manifest(root / "firmware.json")
    for name in VARIANTS:
        config = tomllib.loads((root / name / "fm1.toml").read_text())
        require(config.get("usb_audio_in") is True and config.get("usb_audio_out") is False,
                f"{name}: keep USB input and iPad's own audio output")
        for path in (root / name).glob("*.[ch]"):
            require("#define FM1B_FLASH_JEDEC_OK" not in path.read_text(),
                    f"{path}: overrides the platform's shared flash allowlist")

    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="run4-flash-compat-") as temp:
        def ctest(name, source, *flags):
            exe = str(Path(temp) / name)
            subprocess.run([cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", *flags, str(source), "-o", exe], check=True)
            result = subprocess.run([exe], capture_output=True, text=True)
            require(result.returncode == 0, f"{name}\n{result.stdout}\n{result.stderr}")
            print(f"PASS {name}")

        ctest("storage-chip-allowlist", base / "tests/flash_ok_test.c")
        ctest("persistent-storage", base / "tests/storage_test.c")
        for chip in CHIPS:
            for loader in (False, True):
                flags = [f"-DTEST_JEDEC=0x{chip:06X}u"]
                if loader:
                    flags.append("-DFELUCCA_LOADER=1")
                ctest(f"ota-{'loader' if loader else 'app'}-{chip:06X}",
                      base / "bsp/tests/ota_test.c", *flags)

    # Reuse the upstream rescue model, exercising its real chip validation and
    # write policy for each chip with both stock boot heads. Nothing physical.
    rescue = runpy.run_path(str(base / "bsp/tests/rescue_test.py"))
    for chip, label in CHIPS.items():
        for stock, image in rescue["PKGS"]:
            device = rescue["device"](image)
            device.chip = lambda chip=chip: (0x980F, 3, chip)
            before = bytes(device.flash)
            require(rescue["run"](device) is True and device.ran, f"rescue {label}/{stock}")
            require(bytes(device.flash[:len(image)]) == image, f"rescue {label}/{stock}: app bytes")
            require(bytes(device.flash[:0x4000]) == before[:0x4000], "rescue changed boot head")
            require(bytes(device.flash[len(image):]) == before[len(image):], "rescue changed saved data")
            print(f"PASS USB rescue {label}, {stock} boot head preserved")
    device = rescue["device"](rescue["V16"])
    before = bytes(device.flash)
    device.chip = lambda: (0x980F, 3, 0xFFFFFF)
    try:
        rescue["run"](device)
    except rescue["r"].Fail:
        pass
    else:
        raise SystemExit("FAIL: USB rescue accepted an unknown flash chip")
    require(bytes(device.flash) == before and not device.erased, "unknown chip was written")
    print("PASS unknown flash refused without erasing")

    # Keep host-side names aligned with the firmware's accepted JEDEC IDs.
    sys.path.insert(0, str(base / "tools"))
    spec = importlib.util.spec_from_file_location("run4_fm1_install", base / "tools/fm1_install.py")
    installer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(installer)
    require(installer.FLASH_PARTS == CHIPS, "installer chip names differ from firmware support")
    print("PASS installer chip recognition; all RUN4 CTL variants use the shared platform")


if __name__ == "__main__":
    main()
