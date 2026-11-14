#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for the RUN4 app: builds RUN4 CTL protocol v1 frames (docs/PROTOCOL.md) with the standard library only.
It does NOT share code with src/r4p.c, so the host simulator tests also cross-check the C codec.

  fake_run4.py hex [DEMO]        print every frame of a demo (tape | tolf | all) as hex, with a label
  fake_run4.py sim SCENARIO      print a host-simulator script (tests/scenarios/*.txt with @NAME@ lines expanded)
  fake_run4.py expect KIND ...   print the hex of one device->host frame: btn ID 0|1, enc ID DELTA, pot V, hello, sync
"""
import sys

HDR = [0xF0, 0x7D, 0x52, 0x34]
CMD = dict(HELLO=0x01, BTN=0x02, ENC=0x03, POT=0x04, SYNC_REQ=0x05,
           HELLO_ACK=0x41, PAGE=0x42, VALUE=0x43, LEDS=0x44, STATUS=0x45, TOAST=0x46)
KIND = dict(empty=0, uni=1, bi=2, enum=3, toggle=4)
LED = dict(off=0, dim=1, on=2, blink=3)
BTN = "FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+".split()


def v14(v):
    assert 0 <= v <= 16383
    return [v >> 7, v & 0x7F]


def string(s):
    out = []
    for ch in s[:24]:
        c = ord(ch)
        if c > 0xFF:
            c = ord("?")
        if c >= 0x80:
            out += [0x7F, c - 0x80]
        else:
            out.append(c if 0x20 <= c < 0x7F else ord("?"))
    return [min(len(s), 24)] + out


def frame(cmd, payload=()):
    f = HDR + [CMD[cmd]] + list(payload) + [0xF7]
    assert all(b < 0x80 for b in f[1:-1])
    return f


def hx(f):
    return " ".join("%02X" % b for b in f)


def enum_value(count, idx):
    return (count << 7) | idx          # the protocol's enum encoding


def slot(name, kind, value, text):
    return string(name) + [KIND[kind]] + v14(value) + string(text)


def page(title, track, rgb, page_idx, page_count, bank_idx, bank_count, slots):
    p = string(title) + string(track) + list(rgb) + [page_idx, page_count, bank_idx, bank_count]
    for s in slots:
        p += s
    return frame("PAGE", p)


def leds(btn=None, key=None):
    e = [0] * 41
    for k, v in (btn or {}).items():
        e[BTN.index(k)] = LED[v]
    for k, v in (key or {}).items():
        e[14 + k] = LED[v]
    out = []
    for i in range(0, 42, 3):
        g = e[i:i + 3] + [0, 0]
        out.append(g[0] | g[1] << 2 | g[2] << 4)
    return frame("LEDS", out)


def status(bpm10, bar, beat, flags):
    return frame("STATUS", v14(bpm10) + v14(bar) + [beat, flags])


def toast(text, tenths):
    return frame("TOAST", string(text) + [tenths])


def value(slot_i, v, text):
    return frame("VALUE", [slot_i] + v14(v) + string(text))


# ------------------------------------------------------------------ demo pages ---
def tape():
    return page("TAPE", "T1", (127, 78, 20), 1, 5, 0, 2, [
        slot("GAIN", "bi", 9800, "+2.4 dB"),
        slot("SPEED", "uni", 8192, "1.00x"),
        slot("PITCH", "bi", 8192, "0.0 st"),
        slot("XFADE", "uni", 3000, "12 ms")])


def tolf():
    return page("CREATE", "TÓLF 3", (30, 105, 100), 2, 5, 1, 4, [
        slot("STEPS", "uni", 11000, "11/16"),
        slot("DENSITY", "uni", 10200, "62%"),
        slot("MODE", "enum", enum_value(5, 2), "EUCLID"),
        slot("GATE", "toggle", 1, "ON")])


def demo_frames(which):
    f = [("HELLO_ACK", frame("HELLO_ACK", [1]))]
    if which in ("tape", "all"):
        f += [("PAGE tape", tape()),
              ("LEDS", leds({"PLAY": "on", "REC": "blink", "EDIT": "dim"}, {0: "dim", 12: "on"})),
              ("STATUS", status(1203, 12, 2, 1)),
              ("VALUE slot1", value(1, 9000, "1.10x")),
              ("TOAST", toast("SAVED", 12))]
    if which in ("tolf", "all"):
        f += [("PAGE tolf", tolf()), ("STATUS rec", status(980, 3, 15, 3))]
    return f


def expand(name):
    """a scenario with lines '@name@' replaced by 'sysex HEX'"""
    table = {
        "ACK": frame("HELLO_ACK", [1]), "TAPE": tape(), "TOLF": tolf(),
        "LEDS": leds({"PLAY": "on", "REC": "blink", "EDIT": "dim"}, {0: "dim", 12: "on"}),
        "STATUS": status(1203, 12, 2, 1), "STATUS2": status(980, 3, 15, 3), "VALUE": value(1, 9000, "1.10x"),
        "TOAST": toast("SAVED", 12),
        "BAD_PAGE": HDR + [CMD["PAGE"], 1, 0x41, 0xF7],                    # truncated
        "SR_FRAME": [0xF0, 0x7D, 0x53, 0x52, 0x01, 0xF7],                  # fm1-reader's: not ours
        "HELLO": frame("HELLO", [1, 0, 2, 0]), "SYNC": frame("SYNC_REQ"),
        "POT_8192": frame("POT", v14(8192)), "POT_MAX": frame("POT", v14(16383)),
    }
    for i in range(14):
        table["BTN%d_1" % i] = frame("BTN", [i, 1])
        table["BTN%d_0" % i] = frame("BTN", [i, 0])
    for e in range(7):
        for d in (1, -1, 3, 5, 63, -63, -37):
            table["ENC%d_%+d" % (e, d)] = frame("ENC", [e, 0x40 + d])
    out = []
    for line in open(name).read().splitlines():
        s = line.strip()
        if s.startswith("@") and s.endswith("@"):
            out.append("sysex " + hx(table[s[1:-1]]))
        elif s.startswith("expect_sysex @"):
            out.append("expect_sysex " + hx(table[s.split("@")[1]]))
        else:
            out.append(line)
    return "\n".join(out) + "\n"


def main(a):
    if len(a) < 2:
        print(__doc__)
        return 2
    if a[1] == "hex":
        for label, f in demo_frames(a[2] if len(a) > 2 else "all"):
            print("%-12s %3d bytes  %s" % (label, len(f), hx(f)))
    elif a[1] == "sim":
        sys.stdout.write(expand(a[2]))
    elif a[1] == "expect":
        k = a[2]
        f = {"btn": lambda: frame("BTN", [int(a[3]), int(a[4])]),
             "enc": lambda: frame("ENC", [int(a[3]), 0x40 + int(a[4])]),
             "pot": lambda: frame("POT", v14(int(a[3]))),
             "hello": lambda: frame("HELLO", [1, 0, 2, 0]), "sync": lambda: frame("SYNC_REQ")}[k]()
        print(hx(f))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
