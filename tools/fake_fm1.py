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
        return d
    if cmd == 0x43:
        return dict(cmd="VALUE", slot=r.u8(), value=r.v14(), text=r.string())
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
    return dict(cmd="0x%02X" % cmd)


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
                s += "\n        K%d %-10s %-9s %s  (%s)" % (i + 1, t["name"], t["kind"], t["text"], v)
        return s
    if c == "VALUE":
        return "VALUE K%d = %s (%d)" % (d["slot"] + 1, d["text"], d["value"])
    if c == "LEDS":
        return "LEDS  buttons %s  keys %s" % (d["btn"] or "-", d["keys"] or "-")
    if c == "STATUS":
        fl = [n for b, n in ((1, "play"), (2, "rec"), (4, "overdub")) if d["flags"] & b]
        return "STATUS %.1f BPM  bar %d.%d  %s" % (d["bpm"], d["bar"], d["beat"] + 1, "+".join(fl) or "stopped")
    if c == "TOAST":
        return "TOAST \"%s\" %.1f s" % (d["text"], d["seconds"])
    if c == "HELLO_ACK":
        return "HELLO_ACK proto %d.%d" % (d["proto"], d["minor"])
    if c == "LIST":
        if not d["total"]:
            return "LIST closed"
        rows = ["%s%2d %s" % (">" if d["first"] + i == d["sel"] else " ", d["first"] + i + 1, t) for i, t in enumerate(d["items"])]
        return "LIST  %s  (%d items, window from %d)  selected %d\n        %s" % (d["title"], d["total"], d["first"] + 1, d["sel"] + 1, "\n        ".join(rows))
    return c


# ------------------------------------------------------------------ the device ---
MODE_RUN4, FW = 0, (0, 3)


def hello():
    return f4.frame("HELLO", [1, FW[0], FW[1], MODE_RUN4, 1])


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


def selftest():
    n = [0]

    def eq(a, b, what):
        n[0] += 1
        if a != b:
            print("FAIL %s:\n  got  %r\n  want %r" % (what, a, b))
            sys.exit(1)

    h = lambda s: [int(x, 16) for x in s.split()]  # noqa: E731
    # device -> host: the bytes the C codec produces (tests/r4p_test.c, tests/scenarios)
    eq(hello(), h("F0 7D 52 34 01 01 00 03 00 01 F7"), "HELLO")
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
    eq(decode(h("F0 7D 52 34 43 02 43 28 03 31 30 78 F7")), dict(cmd="VALUE", slot=2, value=8616, text="10x"), "VALUE")
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
