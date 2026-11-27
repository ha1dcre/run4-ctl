#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Eugen Schott
"""A repeatable archive must build on its own, with no firmware submodule."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

sys.dont_write_bytecode = True
SDK = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sdk_package', SDK / 'package.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='run4ctl-sdk-package-') as temp:
    temp = Path(temp)
    first, second = temp / 'first.zip', temp / 'second.zip'
    module.package(first)
    module.package(second)
    assert first.read_bytes() == second.read_bytes(), 'archive is not reproducible'
    prefix = f'run4ctl-sdk-{module.VERSION}/'
    expected = {'README.md', *module.SHARED_FILES, *(f'sdk/{name}' for name in module.SDK_FILES)}
    with zipfile.ZipFile(first) as archive:
        assert set(archive.namelist()) == {prefix + name for name in expected}
        for name in module.SHARED_FILES:
            assert archive.read(prefix + name) == (SDK.parent / name).read_bytes()
        archive.extractall(temp / 'extracted')
    standalone = temp / 'extracted' / prefix / 'sdk'
    assert not (standalone.parent / 'base').exists()
    subprocess.run(['make', '-C', str(standalone), 'test', 'example'], check=True)
print('SDK package: repeatable MIT-only archive; standalone C/C++ tests and example passed')
