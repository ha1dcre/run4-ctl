#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds the parts of the site that need no device build: front page and manual (with fm1-base's manual_html.py, the
same call as base/tools/make_site.py). The installer page and the emulator need a release package and Emscripten
(`python3 base/tools/build.py app --release X.Y`, then `python3 base/tools/make_site.py app PKG X.Y site/`), which
this machine cannot build, so the result is a preview: site/index.html, site/manual/index.html. Not published.

  .venv/bin/python tools/make_site_preview.py"""
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
conf = tomllib.loads((ROOT / "app" / "fm1.toml").read_text())
conf["name"] = "RUN4 CTL"
out = ROOT / "site"
if out.exists():
    shutil.rmtree(out)
(out / "manual").mkdir(parents=True)


def page(md, dst, home):
    args = [sys.executable, str(ROOT / "base" / "tools" / "manual_html.py"), str(md), str(dst), "--name", conf["name"],
            "--repo", conf.get("repo", ""), "--tagline", conf.get("tagline", "")]
    subprocess.run(args + (["--home"] if home else []), check=True, capture_output=True)


page(ROOT / "docs" / "landing.md", out / "index.html", True)
page(ROOT / "docs" / "MANUAL.md", out / "manual" / "index.html", False)
shutil.copytree(ROOT / "docs" / "img", out / "manual" / "img")
shutil.copytree(ROOT / "docs" / "img", out / "img")
print("site preview:", out, sorted(str(p.relative_to(out)) for p in out.rglob("*") if p.is_file()))
