# SPDX-License-Identifier: GPL-3.0-only
# Host-simulator only: Pillow without libraqm refuses OpenType `features` (fm1-base's gen_font.py asks for tabular
# figures). Drop them so the fonts can be generated on this Mac; digit widths then differ slightly from the device's.
# Use with PYTHONPATH=tools/hostshim (the Makefile does).
try:
    from PIL import ImageFont, ImageDraw, features
    if not features.check("raqm"):
        def _wrap(cls, name):
            orig = getattr(cls, name)
            def f(self, *a, **kw):
                kw.pop("features", None)
                return orig(self, *a, **kw)
            setattr(cls, name, f)
        _wrap(ImageFont.FreeTypeFont, "getbbox")
        _wrap(ImageFont.FreeTypeFont, "getlength")
        o = ImageDraw.ImageDraw.text
        def text(self, *a, **kw):
            kw.pop("features", None)
            return o(self, *a, **kw)
        ImageDraw.ImageDraw.text = text
except ImportError:
    pass
