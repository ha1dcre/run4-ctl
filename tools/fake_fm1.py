#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A Mac stand-in for the FM-1 running RUN4 CTL, to try the RUN4 app before the hardware is there.

  fake_fm1.py              open the virtual CoreMIDI ports "FM-1" (a source and a destination) and play the device:
                           HELLO every second until HELLO_ACK, then SYNC_REQ + POT; the terminal is the panel
  fake_fm1.py --bridge [NAME]  the same, and also talk through the Mac's Network MIDI session port (name contains NAME,
                           default "Session"), so an iPad that joined the session sees the FM-1 (see README)
  fake_fm1.py --selftest   no MIDI at all: encode / decode checks against the protocol's known bytes (make test)
  fake_fm1.py --loopback   a CoreMIDI round trip in this process (no app needed): a fake host answers the HELLO

Standard library only (tools/coremidi.py talks to CoreMIDI through ctypes). Protocol: docs/PROTOCOL.md; the frame
builders are shared with tools/fake_run4.py. Keys on the terminal (TAB switches between panel and piano mode):
  buttons   h HOME  e ENV  l LFO  f FX  d EDIT  g GLO  a ARP  s SEQ  space PLAY  r REC  u SAVE(undo)  [ OCT-  ] OCT+
            x SEL (toggles: SHIFT held / released)
  encoders  , . SELECT   ; ' ALGORITHM   - = PRESETS   1 2 K1   3 4 K2   5 6 K3   7 8 K4   (first key: -1, second: +1;
            the shifted digits ! @ # $ % ^ & * turn five detents)
  MASTER    9 down, 0 up (512 of 16383 each)
  1.3       B send a backup of the fake's user voices, R restore the host's last one (both answer like the firmware;
            the fake also answers NAMES_REQ, VOICE_GET and the host voice's frames with HV_STATE)
  piano     (after TAB) z s x d c v g b h n j m , l . ; /  = C4 up;  q 2 w 3 e r 5 t 6 y 7 u = C5 up;  - + octave; TAB back
  other     ? help   p show the page again   Q quit
"""
import os
import select
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fake_run4 as f4  # noqa: E402

LED_NAMES = ["off", "dim", "ON", "BLINK"]
KIND_NAMES = ["empty", "uni", "bi", "enum", "toggle"]
ENC_NAMES = ["SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"]


# ------------------------------------------------------------------ decoding (host -> device) ---
class Bad(Exception):
    pass


class Rd:
    def __init__(self, b):
        self.b, self.i = list(b), 0

    def u8(self):
        if self.i >= len(self.b):
            raise Bad("truncated")
        v = self.b[self.i]
        self.i += 1
        return v

    def v14(self):
        return self.u8() << 7 | self.u8()

    def string(self):
        n = self.u8()
        if n > 24:
            raise Bad("string longer than 24")
        s = ""
        for _ in range(n):
            c = self.u8()
            if c == 0x7F:
                s += chr(0x80 + self.u8())
            else:
                s += chr(c)
        return s


def decode(frame):
    """a whole frame F0 .. F7 (host -> device) as a dict; raises Bad. Returns None for a frame that is not ours."""
    f = list(frame)
    if len(f) < 6 or f[0] != 0xF0 or f[-1] != 0xF7:
        raise Bad("not a SysEx frame")
    if f[1:4] != [0x7D, 0x52, 0x34]:
        return None
    if any(b >= 0x80 for b in f[1:-1]):
        raise Bad("byte >= 0x80 inside")
    cmd, r = f[4], Rd(f[5:-1])
    if cmd == 0x41:
        return dict(cmd="HELLO_ACK", proto=r.u8(), minor=r.u8() if r.i < len(r.b) else 0)
    if cmd == 0x47:
        d = dict(cmd="LIST", title=r.string(), rgb=(r.u8(), r.u8(), r.u8()), total=r.u8(), first=r.u8(), sel=r.u8())
        n = r.u8()
        if n > 7 or d["first"] + n > d["total"] or (d["total"] and d["sel"] >= d["total"]):
            raise Bad("bad LIST window")
        d["items"] = [r.string() for _ in range(n)]
        return d
    if cmd == 0x42:
        d = dict(cmd="PAGE", title=r.string(), track=r.string(), rgb=(r.u8(), r.u8(), r.u8()), page_idx=r.u8(),
                 page_count=r.u8(), bank_idx=r.u8(), bank_count=r.u8(), slots=[])
        for _ in range(4):
            name, kind, val = r.string(), r.u8(), r.v14()
            d["slots"].append(dict(name=name, kind=KIND_NAMES[kind] if kind < 5 else "?%d" % kind, value=val, text=r.string()))
        if r.i < len(r.b):                                   # protocol 1.2 trailer
            d.update(cursor=r.u8(), item_idx=r.v14(), item_count=r.v14(), path=r.string())
            if (d["cursor"] > 3 and d["cursor"] != 127) or (d["item_idx"] >= d["item_count"] if d["item_count"] else d["item_idx"]):
                raise Bad("bad PAGE trailer")
        if r.i != len(r.b):
            raise Bad("trailing bytes")
        return d
    if cmd == 0x43:
        d = dict(cmd="VALUE", slot=r.u8(), value=r.v14(), text=r.string(), flags=r.u8() if r.i < len(r.b) else 0)
        if d["flags"] & ~1:
            raise Bad("VALUE flags")
        return d
    if cmd == 0x48:
        d = dict(cmd="FOCUS", cursor=r.u8(), item_idx=r.v14(), item_count=r.v14())
        if (d["cursor"] > 3 and d["cursor"] != 127) or (d["item_idx"] >= d["item_count"] if d["item_count"] else d["item_idx"]):
            raise Bad("bad FOCUS")
        return d
    if cmd == 0x44:
        e = []
        for byte in f[5:-1]:
            e += [byte & 3, byte >> 2 & 3, byte >> 4 & 3]
        if len(f[5:-1]) != 14:
            raise Bad("LEDS needs 14 bytes")
        return dict(cmd="LEDS", btn={f4.BTN[i]: LED_NAMES[e[i]] for i in range(14) if e[i]},
                    keys={i: LED_NAMES[e[14 + i]] for i in range(27) if e[14 + i]})
    if cmd == 0x45:
        return dict(cmd="STATUS", bpm=r.v14() / 10.0, bar=r.v14(), beat=r.u8(), flags=r.u8())
    if cmd == 0x46:
        return dict(cmd="TOAST", text=r.string(), seconds=(r.u8() or 15) / 10.0)
    d = decode13(cmd, r)
    if d is not None:
        if r.i != len(r.b):
            raise Bad("trailing bytes")
        return d
    return dict(cmd="0x%02X" % cmd)


def decode13(cmd, r):
    """protocol 1.3 host -> device frames (docs/PROTOCOL.md); None for another command"""
    if cmd == 0x49:
        d = dict(cmd="HV_ON", on=r.u8(), chan=r.u8())
        if d["on"] > 1 or d["chan"] > 15:
            raise Bad("bad HV_ON")
        return d
    if cmd == 0x4A:
        kind = r.u8()
        if kind == 0:
            d = dict(cmd="HV_VOICE", kind="stored", idx=r.u8())
            if d["idx"] >= 40:
                raise Bad("bad voice index")
            return d
        if kind in (1, 2):
            return dict(cmd="HV_VOICE", kind=("packed", "vced")[kind - 1], data=[r.u8() for _ in range((128, 155)[kind - 1])])
        raise Bad("bad HV_VOICE kind")
    if cmd == 0x4B:
        pairs = []
        while r.i < len(r.b):
            i, v = r.u8(), r.u8()
            if i >= len(f4.HVP) or v > f4.HVP_MAX[i]:
                raise Bad("bad HV_PARAM pair")
            pairs.append((f4.HVP[i], v))
        if not pairs or len(pairs) > len(f4.HVP):
            raise Bad("HV_PARAM needs 1..18 pairs")
        return dict(cmd="HV_PARAM", pairs=pairs)
    if cmd == 0x4C:
        return dict(cmd="NAMES_REQ")
    if cmd == 0x4D:
        d = dict(cmd="VOICE_GET", idx=r.u8())
        if d["idx"] >= 40 and d["idx"] != f4.VOICE_HV:
            raise Bad("bad voice index")
        return d
    if cmd == 0x4E:
        d = dict(cmd="TAPE_WAVE", track=r.u8(), label=r.string(), n=r.u8())
        if d["track"] > 3 or d["n"] > 120:
            raise Bad("bad TAPE_WAVE")
        d["peaks"] = [r.u8() for _ in range(d["n"])]
        return d
    if cmd == 0x4F:
        d = dict(cmd="TAPE_POS", play=r.v14(), lin=r.v14(), lout=r.v14(), flags=r.u8())
        if d["flags"] > 7 or (not d["flags"] & 4 and d["lin"] > d["lout"]):
            raise Bad("bad TAPE_POS")
        return d
    if cmd == 0x50:
        d = dict(cmd="METERS", levels=[r.u8() for _ in range(5)], clip=r.u8())
        if d["clip"] > 31:
            raise Bad("bad METERS")
        return d
    if cmd == 0x51:
        d = dict(cmd="BK_ACK", status=r.u8())
        if d["status"] > 2:
            raise Bad("bad BK_ACK")
        return d
    if cmd == 0x52:
        d = dict(cmd="BK_INFO", count=r.u8(), crc=sum(r.u8() << (7 * i) for i in range(5)))
        if d["count"] > 32 or d["crc"] >> 32:
            raise Bad("bad BK_INFO")
        return d
    if cmd == 0x53:
        d = dict(cmd="BK_DATA", k=r.u8(), used=r.u8(), data=[r.u8() for _ in range(128)])
        if d["k"] > 31 or d["used"] > 1:
            raise Bad("bad BK_DATA")
        return d
    return None


def show(d):
    c = d["cmd"]
    if c == "PAGE":
        s = "PAGE  %s  [%s]  rgb%s  page %d/%d  bank %d/%d" % (d["title"], d["track"], d["rgb"], d["page_idx"] + 1,
                                                          d["page_count"], d["bank_idx"] + 1, d["bank_count"])
        for i, t in enumerate(d["slots"]):
            if t["kind"] != "empty":
                v = t["value"]
                if t["kind"] == "enum":
                    v = "%d of %d" % ((v & 0x7F) + 1, v >> 7)
                s += "\n        %sK%d %-10s %-9s %s  (%s)" % (">" if d.get("cursor") == i else " ", i + 1, t["name"], t["kind"], t["text"], v)
        if "path" in d:
            s += "\n        at %s  %s" % (d["path"] or "-", "%d/%d" % (d["item_idx"] + 1, d["item_count"]) if d["item_count"] else "")
        return s
    if c == "VALUE":
        return "VALUE K%d = %s (%d)%s" % (d["slot"] + 1, d["text"], d["value"], "  BIG" if d["flags"] & 1 else "")
    if c == "FOCUS":
        return "FOCUS %s  %s" % ("K%d" % (d["cursor"] + 1) if d["cursor"] < 4 else "none",
                                 "%d/%d" % (d["item_idx"] + 1, d["item_count"]) if d["item_count"] else "")
    if c == "LEDS":
        return "LEDS  buttons %s  keys %s" % (d["btn"] or "-", d["keys"] or "-")
    if c == "STATUS":
        fl = [n for b, n in ((1, "play"), (2, "rec"), (4, "overdub")) if d["flags"] & b]
        return "STATUS %.1f BPM  bar %d.%d  %s" % (d["bpm"], d["bar"], d["beat"] + 1, "+".join(fl) or "stopped")
    if c == "TOAST":
        return "TOAST \"%s\" %.1f s" % (d["text"], d["seconds"])
    if c == "HELLO_ACK":
        return "HELLO_ACK proto %d.%d" % (d["proto"], d["minor"])
    if c == "HV_ON":
        return "HV_ON %s channel %d" % ("on" if d["on"] else "off", d["chan"] + 1)
    if c == "HV_VOICE":
        return "HV_VOICE %s" % (d["idx"] if d["kind"] == "stored" else "%s %r" % (d["kind"], voice_name(d)))
    if c == "HV_PARAM":
        return "HV_PARAM " + " ".join("%s=%d" % p for p in d["pairs"])
    if c == "TAPE_WAVE":
        return "TAPE_WAVE T%d %r %d columns" % (d["track"] + 1, d["label"], d["n"]) if d["n"] else "TAPE_WAVE off"
    if c == "TAPE_POS":
        return "TAPE_POS play %d  loop %s  %s" % (d["play"], "-" if d["flags"] & 4 else "%d..%d" % (d["lin"], d["lout"]),
                                                 "rec" if d["flags"] & 2 else "play" if d["flags"] & 1 else "stop")
    if c == "METERS":
        return "METERS %s clip %s" % (d["levels"], bin(d["clip"]))
    if c == "BK_ACK":
        return "BK_ACK %s" % ("stored", "bad checksum", "not stored")[d["status"]]
    if c == "BK_INFO":
        return "BK_INFO %d voices crc %08X" % (d["count"], d["crc"])
    if c == "BK_DATA":
        return "BK_DATA %d%s" % (d["k"], "" if d["used"] else " (empty)")
    if c in ("NAMES_REQ", "VOICE_GET"):
        return c + (" %d" % d["idx"] if "idx" in d else "")
    if c == "LIST":
        if not d["total"]:
            return "LIST closed"
        rows = ["%s%2d %s" % (">" if d["first"] + i == d["sel"] else " ", d["first"] + i + 1, t) for i, t in enumerate(d["items"])]
        return "LIST  %s  (%d items, window from %d)  selected %d\n        %s" % (d["title"], d["total"], d["first"] + 1, d["sel"] + 1, "\n        ".join(rows))
    return c


# ------------------------------------------------------------------ the device ---
MODE_RUN4, FW = 0, (0, 5)


def hello():
    return f4.frame("HELLO", [1, FW[0], FW[1], MODE_RUN4, 3])      # protocol 1.3


def voice_name(d):
    """the name in a 128-byte packed voice (bytes 118..127) or a 155-byte VCED one (145..154)"""
    data = d["data"]
    return "".join(chr(c) for c in data[118:128] if 32 <= c < 127).rstrip() if len(data) == 128 else \
        "".join(chr(c) for c in data[145:155] if 32 <= c < 127).rstrip()


def btn(i, down):
    return f4.frame("BTN", [i, 1 if down else 0])


def enc(i, d):
    return f4.frame("ENC", [i, 0x40 + max(-63, min(63, d))])


def pot(v):
    return f4.frame("POT", f4.v14(v))


BUTTON_KEYS = {"h": "HOME", "e": "ENV", "l": "LFO", "f": "FX", "d": "EDIT", "g": "GLO", "a": "ARP", "s": "SEQ",
               " ": "PLAY", "r": "REC", "u": "SAVE", "[": "OCT-", "]": "OCT+"}
ENC_KEYS = {}
for _i, (_m, _p) in enumerate([(",", "."), (";", "'"), ("-", "="), ("1", "2"), ("3", "4"), ("5", "6"), ("7", "8")]):
    ENC_KEYS[_m] = (_i, -1)
    ENC_KEYS[_p] = (_i, 1)
for _i, _c in enumerate("!@#$%^&*"):                   # shifted digits: five detents (K1 -, K1 +, K2 -, ...)
    ENC_KEYS[_c] = (3 + _i // 2, (-5, 5)[_i % 2])
PIANO = {c: 60 + i for i, c in enumerate("zsxdcvgbhnjm,l.;/")}
PIANO.update({c: 72 + i for i, c in enumerate("q2w3er5t6y7u")})


class Device:
    def __init__(self, send, note_send, out=print):
        self.send, self.note_send, self.out = send, note_send, out
        self.acked, self.hello_at, self.master = False, 0.0, 8192
        self.sel, self.octave, self.piano = False, 0, False
        self.page = None
        self.list, self.sel = None, 0
        self.lock = threading.Lock()
        # protocol 1.3: the voices (8 factory, 32 user slots), the host voice, the backup and restore under way
        self.voices = [list(v) for v in f4.dx7_voices()] + [None] * 32
        self.hv = dict(on=0, chan=15, voice=0, data=list(self.voices[0]))
        self.restore = None                             # dict(k, used, data, info) while restoring

    def hv_state(self):
        name = "".join(chr(c) for c in self.hv["data"][118:128]).rstrip()
        return f4.hv_state(1 if self.hv["on"] else 0, self.hv["chan"], self.hv["voice"], name)

    def backup(self):
        """B: the 32 user voices to the host as BK_PART x 32 + BK_END (the firmware's SETUP > VOICE BACKUP)"""
        parts, crc = f4.backup_parts(self.voices[8:])
        for k, used, data in parts:
            self.send(f4.bk_part(k, used, data))
        self.send(f4.bk_sum(32, crc))
        self.out("-> backup: 32 parts, crc %08X" % crc)

    def restore_start(self):
        self.restore = dict(k=0, used=[], data=[], info=None)
        self.send(f4.frame("BK_GET", [127]))
        self.out("-> BK_GET summary")

    def tick(self, now):
        if not self.acked and now - self.hello_at >= 1.0:
            self.hello_at = now
            self.send(hello())
            self.out("-> HELLO")

    def on_frame(self, frame):
        try:
            d = decode(frame)
        except Bad as e:
            self.out("!! bad frame (%s): %s" % (e, f4.hx(frame)))
            return
        if d is None:
            return
        self.out("<- " + show(d))
        if d["cmd"] == "HELLO_ACK":
            self.acked = True
            self.send(f4.frame("SYNC_REQ"))
            self.send(pot(self.master))
            self.out("-> SYNC_REQ, POT %d" % self.master)
        elif d["cmd"] == "PAGE":
            self.page = d
        elif d["cmd"] == "LIST":
            self.list = d if d["total"] else None
            self.sel = d["sel"]
        else:
            self.on_frame13(d)

    def on_frame13(self, d):
        c = d["cmd"]
        if c == "HV_ON":
            self.hv.update(on=d["on"], chan=d["chan"])
            self.send(self.hv_state())
        elif c == "HV_VOICE":
            if d["kind"] == "stored":
                if self.voices[d["idx"]] is not None:
                    self.hv.update(voice=d["idx"], data=list(self.voices[d["idx"]]))
            elif d["kind"] == "packed":
                self.hv.update(voice=f4.VOICE_HV, data=list(d["data"]))
            else:                                       # (the fake keeps only the name of a VCED voice)
                self.hv.update(voice=f4.VOICE_HV, data=self.hv["data"][:118] + d["data"][145:155])
            self.send(self.hv_state())
        elif c == "NAMES_REQ":
            for first in range(0, 40, 8):
                self.send(f4.voice_names(first, [(0, "") if v is None else (1, "".join(chr(x) for x in v[118:128]).rstrip())
                                                 for v in self.voices[first:first + 8]]))
        elif c == "VOICE_GET":
            v = self.hv["data"] if d["idx"] == f4.VOICE_HV else self.voices[d["idx"]]
            self.send(f4.voice_data(d["idx"], v))
        elif c == "BK_ACK":
            self.out("(backup: %s)" % ("BACKUP SENT", "BAD CHECKSUM", "BACKUP NOT STORED")[d["status"]])
        elif c == "BK_INFO" and self.restore is not None:
            if d["count"] != 32:
                self.out("(restore: %s)" % ("NO BACKUP" if not d["count"] else "BAD BACKUP"))
                self.restore = None
                return
            self.restore["info"] = d
            self.send(f4.frame("BK_GET", [0]))
        elif c == "BK_DATA" and self.restore is not None and d["k"] == self.restore["k"]:
            rs = self.restore
            rs["used"].append(d["used"])
            rs["data"].append(d["data"])
            rs["k"] += 1
            if rs["k"] < 32:
                self.send(f4.frame("BK_GET", [rs["k"]]))
                return
            _, crc = f4.backup_parts([data if used else None for used, data in zip(rs["used"], rs["data"])])
            if crc != rs["info"]["crc"]:
                self.out("(restore: BAD CHECKSUM)")
            else:
                self.voices[8:] = [list(data) if used else None for used, data in zip(rs["used"], rs["data"])]
                self.out("(restore: %d voices written)" % sum(rs["used"]))
            self.restore = None

    def key(self, ch):
        if ch == "\t":
            self.piano = not self.piano
            return self.out("(piano mode %s)" % ("on: z s x d c v g b h n j m , l . ; /  q 2 w 3 e r 5 t 6 y 7 u" if self.piano else "off"))
        if self.piano and ch in PIANO:
            n = PIANO[ch] + 12 * self.octave
            if 0 <= n <= 127:
                self.note_send([0x90, n, 100])
                threading.Timer(0.25, self.note_send, ([0x80, n, 0],)).start()
                self.out("-> note %d" % n)
            return
        if self.piano and ch in "-+":
            self.octave = max(-3, min(3, self.octave + (1 if ch == "+" else -1)))
            return self.out("(octave %+d)" % self.octave)
        if self.list:                                   # a LIST is open: SELECT moves, HOME / PLAY pick, others cancel
            total, first, n = self.list["total"], self.list["first"], len(self.list["items"])
            if ch in ",.":
                self.sel = max(0, min(total - 1, self.sel + (1 if ch == "." else -1)))
                if not first <= self.sel < first + n:
                    self.send(f4.frame("LIST_PICK", [self.sel, 2]))
                return self.out("(list: selected %d)" % (self.sel + 1))
            if ch in BUTTON_KEYS:
                ok = BUTTON_KEYS[ch] in ("HOME", "PLAY")
                self.send(f4.frame("LIST_PICK", [self.sel, 0 if ok else 1]))
                self.list = None
                return self.out("-> LIST_PICK %d %s" % (self.sel, "OK" if ok else "CANCEL"))
        if ch == "x":                                   # SEL = SHIFT, a toggle
            self.sel = not self.sel
            self.send(btn(1, self.sel))
            return self.out("-> SEL %s" % ("down" if self.sel else "up"))
        if ch in BUTTON_KEYS and not self.piano:
            i = f4.BTN.index(BUTTON_KEYS[ch])
            if ch in "[]":
                self.octave = max(-3, min(3, self.octave + (1 if ch == "]" else -1)))
            self.send(btn(i, True))
            self.send(btn(i, False))
            return self.out("-> %s (down, up)" % BUTTON_KEYS[ch])
        if ch in ENC_KEYS and not self.piano:
            i, d = ENC_KEYS[ch]
            self.send(enc(i, d))
            return self.out("-> ENC %s %+d" % (ENC_NAMES[i], d))
        if ch in "90" and not self.piano:
            self.master = max(0, min(16383, self.master + (512 if ch == "0" else -512)))
            self.send(pot(self.master))
            return self.out("-> POT %d" % self.master)
        if ch == "p" and self.page:
            return self.out(show(self.page))
        if ch == "B" and not self.piano:
            return self.backup()
        if ch == "R" and not self.piano:
            return self.restore_start()
        if ch == "?":
            return self.out(__doc__)


# ------------------------------------------------------------------ modes ---
def run_interactive(bridge=None):
    import coremidi
    import termios
    import tty
    cl = coremidi.Client("RUN4 CTL fake FM-1")
    cl.virtual_source("FM-1")
    cl.virtual_destination("FM-1")
    sess_dst = []
    if bridge:                                          # the session's port: what it brings in is the host's, what we send goes out
        srcs, dsts = coremidi.find(bridge, False), coremidi.find(bridge, True)
        srcs = [x for x in srcs if x[0] != cl.src]
        dsts = [x for x in dsts if x[0] != cl.dst]
        if not srcs or not dsts:
            sys.exit("no Network MIDI session port containing %r (Audio MIDI Setup: enable the session first)" % bridge)
        for ref, nm in srcs:
            cl.listen_to(ref, cl.rx)
        sess_dst = [d[0] for d in dsts]
        print("bridged to:", ", ".join(n for _, n in srcs + dsts))

    def to_host(data):
        cl.send(data)
        for d in sess_dst:
            cl.send_to(d, data)
    dev = Device(to_host, to_host, out=lambda s: (sys.stdout.write("\r" + s.replace("\n", "\r\n") + "\r\n"), sys.stdout.flush()))
    print("virtual MIDI ports \"FM-1\" open. ? = help, Q = quit. Waiting for RUN4 ...")
    old = termios.tcgetattr(0)
    tty.setcbreak(0)
    try:
        while True:
            dev.tick(time.time())
            try:
                while True:
                    m = cl.rx.get_nowait()
                    if m and m[0] == 0xF0:
                        dev.on_frame(m)
            except Exception:
                pass
            if select.select([0], [], [], 0.05)[0]:
                ch = os.read(0, 1).decode("latin-1")
                if ch == "Q":
                    break
                dev.key(ch)
    finally:
        termios.tcsetattr(0, termios.TCSADRAIN, old)


def run_loopback():
    import coremidi
    import queue
    cl = coremidi.Client("fake FM-1 loopback")
    cl.virtual_source("FM-1 loopback")
    cl.virtual_destination("FM-1 loopback")
    host_rx = queue.Queue()
    cl.listen_to(cl.src, host_rx)                      # the "host": hears the device, answers into its destination
    log = []
    dev = Device(cl.send, cl.send, out=log.append)
    t0 = time.time()
    sent_ack = False
    while time.time() - t0 < 2.5:
        dev.tick(time.time())
        while not host_rx.empty():
            m = host_rx.get()
            if m[:5] == bytes([0xF0, 0x7D, 0x52, 0x34, 0x01]) and not sent_ack:
                sent_ack = True
                cl.send_to(cl.dst, bytes(f4.frame("HELLO_ACK", [1])))
                cl.send_to(cl.dst, bytes(f4.tape()))
        while not cl.rx.empty():
            dev.on_frame(cl.rx.get())
        time.sleep(0.02)
    print("\n".join(log))
    assert dev.acked and dev.page and dev.page["title"] == "TAPE", "loopback did not complete"
    print("loopback ok")


def decode_dev(fr):
    """a device -> host BK_PART / BK_END (the selftest's host)"""
    if fr[4] == 0x0A:
        return ("BK_PART", fr[5], fr[6], list(fr[7:-1]))
    if fr[4] == 0x0B:
        return ("BK_END", fr[5], sum(fr[6 + i] << (7 * i) for i in range(5)))
    return None


def selftest():
    n = [0]

    def eq(a, b, what):
        n[0] += 1
        if a != b:
            print("FAIL %s:\n  got  %r\n  want %r" % (what, a, b))
            sys.exit(1)

    h = lambda s: [int(x, 16) for x in s.split()]  # noqa: E731
    # device -> host: the bytes the C codec produces (tests/r4p_test.c, tests/scenarios)
    eq(hello(), h("F0 7D 52 34 01 01 00 05 00 03 F7"), "HELLO")
    eq(btn(0, True), h("F0 7D 52 34 02 00 01 F7"), "BTN down")
    eq(btn(13, False), h("F0 7D 52 34 02 0D 00 F7"), "BTN up")
    eq(enc(3, 1), h("F0 7D 52 34 03 03 41 F7"), "ENC +1")
    eq(enc(2, -63), h("F0 7D 52 34 03 02 01 F7"), "ENC -63")
    eq(enc(2, -100), h("F0 7D 52 34 03 02 01 F7"), "ENC clamp")
    eq(pot(8192), h("F0 7D 52 34 04 40 00 F7"), "POT")
    eq(f4.frame("SYNC_REQ"), h("F0 7D 52 34 05 F7"), "SYNC_REQ")
    # host -> device: decode what fake_run4 builds, and a hand-written frame
    eq(decode(f4.frame("HELLO_ACK", [1])), dict(cmd="HELLO_ACK", proto=1, minor=0), "ACK 1.0")
    eq(decode(f4.frame("HELLO_ACK", [1, 1]))["minor"], 1, "ACK 1.1")
    lst = decode(f4.list_frame("MODE", (127, 70, 20), 5, 0, 1, ["TAPE", "POLY", "DISC", "PERFORM", "T\u00d3LF"]))
    eq((lst["total"], lst["sel"], lst["items"][4]), (5, 1, "T\u00d3LF"), "LIST")
    eq(f4.frame("LIST_PICK", [7, 0]), h("F0 7D 52 34 06 07 00 F7"), "LIST_PICK")
    p = decode(f4.tolf())
    eq((p["title"], p["track"], p["rgb"], p["page_idx"], p["page_count"], p["bank_idx"], p["bank_count"]),
       ("CREATE", "T\u00d3LF 3", (30, 105, 100), 1 + 1, 5, 1, 4), "PAGE head")
    eq([(s["name"], s["kind"], s["value"], s["text"]) for s in p["slots"]],
       [("STEPS", "uni", 11000, "11/16"), ("DENSITY", "uni", 10200, "62%"), ("MODE", "enum", 5 << 7 | 2, "EUCLID"),
        ("GATE", "toggle", 1, "ON")], "PAGE slots")
    eq(decode(h("F0 7D 52 34 43 02 43 28 03 31 30 78 F7")), dict(cmd="VALUE", slot=2, value=8616, text="10x", flags=0), "VALUE")
    # protocol 1.2: the PAGE trailer, FOCUS, VALUE flags
    w = decode(f4.wear())
    eq((w["cursor"], w["item_idx"], w["item_count"], w["path"]), (1, 4, 12, "TAPE > WEAR"), "PAGE 1.2 trailer")
    eq("cursor" in p, False, "PAGE 1.1 has no trailer")
    eq(decode(f4.focus(2, 5, 12)), dict(cmd="FOCUS", cursor=2, item_idx=5, item_count=12), "FOCUS")
    eq(decode(f4.value(2, 12000, "0.73", 1))["flags"], 1, "VALUE flags")
    for bad in (f4.frame("FOCUS", [5, 0, 0, 0, 0]), f4.frame("VALUE", [1, 0, 1, 1, 0x41, 2])):
        try:
            decode(bad)
            eq("accepted", "Bad", "bad 1.2 frame")
        except Bad:
            n[0] += 1
    eq(decode(f4.status(1203, 12, 2, 1)), dict(cmd="STATUS", bpm=120.3, bar=12, beat=2, flags=1), "STATUS")
    eq(decode(f4.toast("SAVED", 12)), dict(cmd="TOAST", text="SAVED", seconds=1.2), "TOAST")
    eq(decode(f4.toast("X", 0))["seconds"], 1.5, "TOAST default")
    d = decode(f4.leds({"PLAY": "on", "REC": "blink"}, {0: "dim", 12: "on"}))
    eq((d["btn"], d["keys"]), ({"PLAY": "ON", "REC": "BLINK"}, {0: "dim", 12: "ON"}), "LEDS")
    eq(f4.string("T\u00d3LF"), h("04 54 7F 53 4C 46"), "Latin-1 string escape (docs/PROTOCOL.md)")
    eq(decode(h("F0 7D 53 52 01 F7")), None, "other header is not ours")
    for bad in (h("F0 7D 52 34 42 01 41 F7"), h("F0 7D 52 34 41 F7")):
        try:
            decode(bad)
            eq("accepted", "Bad", "truncated frame")
        except Bad:
            n[0] += 1
    # the device state machine: HELLO until the ack, then SYNC_REQ + POT
    sent = []
    dev = Device(sent.append, lambda m: None, out=lambda s: None)
    dev.tick(1.0)
    dev.tick(1.5)
    dev.tick(2.1)
    eq(len(sent), 2, "HELLO throttled to 1 per second")
    dev.on_frame(f4.frame("HELLO_ACK", [1]))
    eq(sent[2:], [f4.frame("SYNC_REQ"), pot(8192)], "after the ack: SYNC_REQ, POT")
    dev.tick(9.0)
    eq(len(sent), 4, "no more HELLO")
    dev.key("f")
    eq(sent[4:], [btn(0, True), btn(0, False)], "FX key")
    dev.key("4")
    eq(sent[6], enc(4, 1), "K2 +")
    # protocol 1.3: decoding the host's frames, the answers, the backup both ways
    eq(decode(f4.hv_on(1, 15)), dict(cmd="HV_ON", on=1, chan=15), "HV_ON")
    eq(decode(f4.hv_param(ALG=4, BRIGHT=70))["pairs"], [("ALG", 4), ("BRIGHT", 70)], "HV_PARAM")
    eq(decode(f4.tape_pos(100, 0, 0, 5))["flags"], 5, "TAPE_POS")
    eq(decode(f4.meters([1, 2, 3, 4, 5], 3))["levels"], [1, 2, 3, 4, 5], "METERS")
    eq(decode(f4.tape_wave(1, "T2", [3] * 120))["n"], 120, "TAPE_WAVE")
    eq(decode(f4.bk_sum(32, 0xCBF43926, True))["crc"], 0xCBF43926, "BK_INFO crc")
    eq(f4.bk_sum(32, 0xCBF43926), h("F0 7D 52 34 0B 20 26 72 50 5F 0C F7"), "BK_END bytes (as tests/r4p_test.c)")
    for bad in (f4.frame("HV_PARAM", [0, 32]), f4.frame("HV_ON", [1, 16]), f4.frame("TAPE_POS", f4.v14(1) + f4.v14(9) + f4.v14(8) + [0]),
                f4.frame("BK_INFO", [32, 0, 0, 0, 0, 0x10]), f4.frame("HV_VOICE", [3])):
        try:
            decode(bad)
            eq("accepted", "Bad", "bad 1.3 frame")
        except Bad:
            n[0] += 1
    del sent[:]
    dev.on_frame(f4.hv_on(1, 15))
    eq(sent, [f4.hv_state(1, 15, 0, f4.factory_name(0))], "HV_ON -> HV_STATE")
    del sent[:]
    dev.on_frame(f4.frame("NAMES_REQ"))
    eq(len(sent), 5, "NAMES_REQ -> 5 VOICE_NAMES")
    eq(sent[0], f4.voice_names(0, [(1, f4.factory_name(k)) for k in range(8)]), "VOICE_NAMES 0")
    del sent[:]
    dev.on_frame(f4.frame("VOICE_GET", [20]))
    eq(sent, [f4.voice_data(20, None)], "VOICE_GET of an empty slot")
    # backup: the fake's user voices out; restore them into a second fake through a host that keeps the backup
    dev.voices[8:] = f4.import_bank_records()
    del sent[:]
    dev.backup()
    eq(len(sent), 33, "backup: 32 parts and the end")
    store = dict(parts=[decode_dev(x) for x in sent[:32]], end=decode_dev(sent[32]))
    _, crc = f4.backup_parts(f4.import_bank_records())
    eq(store["end"], ("BK_END", 32, crc), "backup's CRC")
    out2 = []
    dev2 = Device(out2.append, lambda m: None, out=lambda s: None)
    dev2.restore_start()
    for _ in range(40):                                 # the host answers each BK_GET from its store
        if not out2:
            break
        q = out2.pop(0)
        k = q[5]
        if k == 127:
            dev2.on_frame(f4.bk_sum(32, store["end"][2], True))
        else:
            dev2.on_frame(f4.bk_part(*store["parts"][k][1:], host=True))
    eq(dev2.voices[8:], f4.import_bank_records(), "restore round trip")
    print("fake_fm1 selftest: %d checks ok" % n[0])


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    elif "--loopback" in sys.argv:
        run_loopback()
    elif "--bridge" in sys.argv:
        i = sys.argv.index("--bridge")
        run_interactive(sys.argv[i + 1] if len(sys.argv) > i + 1 else "Session")
    else:
        run_interactive()
