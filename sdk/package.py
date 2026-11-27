#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Eugen Schott
"""Export only the MIT SDK, canonical codec and protocol, never the firmware."""
import argparse
import hashlib
from pathlib import Path
import re
import zipfile

SDK = Path(__file__).resolve().parent
ROOT = SDK.parent
VERSION = re.search(r'#define R4CTL_SDK_VERSION "([0-9.]+)"', (SDK / 'include/run4ctl.h').read_text()).group(1)
SDK_FILES = (
    'README.md', 'INTEGRATION.md', 'Makefile', 'package.py',
    'include/run4ctl.h', 'src/run4ctl.c', 'examples/minimal.c',
    'tests/test.c', 'tests/cpp_smoke.cpp', 'tests/package_test.py',
)
SHARED_FILES = ('LICENSE-MIT', 'docs/PROTOCOL.md', 'src/r4p.c', 'src/r4p.h')


def package(output):
    files = {f'sdk/{name}': (SDK / name).read_bytes() for name in SDK_FILES}
    files.update({name: (ROOT / name).read_bytes() for name in SHARED_FILES})
    files['README.md'] = (
        '<!-- SPDX-License-Identifier: MIT -->\n'
        f'# RUN4 CTL SDK {VERSION}\n\n'
        'Portable C99 controller SDK, C/C++ headers, complete wire codec, runnable example, tests and guide.\n\n'
        'Start with [sdk/README.md](sdk/README.md) and [sdk/INTEGRATION.md](sdk/INTEGRATION.md).\n\n'
        'Build: `make -C sdk test example`. No submodules, FM-1 platform, DSP engine or iOS source required.\n\n'
        'Everything in this archive is MIT-licensed; see [LICENSE-MIT](LICENSE-MIT).\n'
    ).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(f'run4ctl-sdk-{VERSION}/{name}', date_time=(2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + '.sha256').write_text(f'{digest}  {output.name}\n')
    print(f'{output}: {output.stat().st_size} bytes, SHA256 {digest}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.version:
        print(VERSION)
    else:
        package(args.output or SDK / 'build' / f'run4ctl-sdk-{VERSION}.zip')


if __name__ == '__main__':
    main()
