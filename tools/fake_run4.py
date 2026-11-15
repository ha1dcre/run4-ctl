#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for the RUN4 app: builds RUN4 CTL protocol v1 frames (docs/PROTOCOL.md) with the standard library only.
It does NOT share code with src/r4p.c, so the host simulator tests also cross-check the C codec.

  fake_run4.py hex [DEMO]        print every frame of a demo (tape | tolf | all) as hex, with a label
  fake_run4.py sim SCENARIO      print a host-simulator script (tests/scenarios/*.txt with @NAME@ lines expanded)
  fake_run4.py expect KIND ...   print the hex of one device->host frame: btn ID 0|1, enc ID DELTA, pot V, hello, sync
  fake_run4.py desc LOG [amb|synth]  protocol 1.4: read a device's raw SysEx (a simulator "sysex_log"), check every
                                 description in it (frames, labels, counts, the hash) and print it; amb / synth: also
                                 check it against the parameters docs/PROTOCOL.md lists for that mode; 1.5: every
                                 LVIEW in it names a page of the description and only parameters on that page
"""
import sys

HDR = [0xF0, 0x7D, 0x52, 0x34]
CMD = dict(HELLO=0x01, BTN=0x02, ENC=0x03, POT=0x04, SYNC_REQ=0x05, LIST_PICK=0x06,
           HV_STATE=0x07, VOICE_NAMES=0x08, VOICE_DATA=0x09, BK_PART=0x0A, BK_END=0x0B, BK_GET=0x0C,     # 1.3
           HELLO_ACK=0x41, PAGE=0x42, VALUE=0x43, LEDS=0x44, STATUS=0x45, TOAST=0x46, LIST=0x47, FOCUS=0x48,
           HV_ON=0x49, HV_VOICE=0x4A, HV_PARAM=0x4B, NAMES_REQ=0x4C, VOICE_GET=0x4D,                     # 1.3
           TAPE_WAVE=0x4E, TAPE_POS=0x4F, METERS=0x50, BK_ACK=0x51, BK_INFO=0x52, BK_DATA=0x53,
           MODES=0x0D, MODE=0x0E, DESC_HEAD=0x0F, DESC_PAGE=0x10, DESC_PARAM=0x11, DESC_LABELS=0x12,      # 1.4
           DESC_END=0x13, PVAL=0x14, LSTATE=0x15, LM_ERR=0x16,
           MODES_REQ=0x54, MODE_SET=0x55, DESC_REQ=0x56, VALS_REQ=0x57, PSET=0x58,
           LVIEW=0x17, LVIEW_SET=0x59)                                                                    # 1.5
NONE = 127                             # protocol 1.2: no cursor
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


def page(title, track, rgb, page_idx, page_count, bank_idx, bank_count, slots, v12=None):
    """v12 = (cursor, item_idx, item_count, path): the protocol 1.2 trailer; None = a 1.1 page"""
    p = string(title) + string(track) + list(rgb) + [page_idx, page_count, bank_idx, bank_count]
    for s in slots:
        p += s
    if v12 is not None:
        cursor, idx, count, path = v12
        assert (cursor < 4 or cursor == NONE) and (idx < count if count else idx == 0)
        p += [cursor] + v14(idx) + v14(count) + string(path)
    return frame("PAGE", p)


def focus(cursor, idx, count):
    """protocol 1.2 FOCUS"""
    assert (cursor < 4 or cursor == NONE) and (idx < count if count else idx == 0)
    return frame("FOCUS", [cursor] + v14(idx) + v14(count))


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


def list_frame(title, rgb, total, first, sel, items):
    """protocol 1.1 LIST: this frame carries items first .. first+len-1 of total"""
    assert len(items) <= 7 and first + len(items) <= total
    p = string(title) + list(rgb) + [total, first, sel, len(items)]
    for it in items:
        p += string(it)
    return frame("LIST", p)


def value(slot_i, v, text, flags=None):
    """flags (protocol 1.2): bit 0 = show big; None = a 1.1 VALUE"""
    return frame("VALUE", [slot_i] + v14(v) + string(text) + ([flags] if flags is not None else []))


def wear(cursor=1, idx=4):
    """a 1.2 page: Tape > Wear, the focus on FAILURE (5th of 12 parameters)"""
    return page("WEAR", "T1", (127, 78, 20), 2, 5, 0, 1, [
        slot("AGE", "uni", 6000, "37%"),
        slot("FAILURE", "uni", 4100, "25%"),
        slot("FLUTTER", "uni", 9000, "0.55"),
        slot("HISS", "bi", 8192, "0.0 dB")], v12=(cursor, idx, 12, "TAPE > WEAR"))


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


# ------------------------------------------------------------------ protocol 1.3 ---
HVP = ("ALG FB BRIGHT ENVSPD VOLUME VIBRATO TREMOLO LFOSPD ATDEPTH BEND VELSENS TRANSPOSE TUNE CHORUS DELAY REVERB DRIVE "
       "DLYTIME MORPHSET MORPHMODE MORPH").split()                  # 18..20: the morph (firmware 0.6)
HVP_MAX = [31, 7, 127, 127, 100, 99, 99, 99, 63, 12, 100, 48, 100, 100, 100, 100, 100, 5, 2, 11, 100]
VOICE_HV = 127                         # the host voice's own voice (VOICE_GET, VOICE_DATA, HV_STATE)


def hv_on(on, chan):
    """HV_ON: the host voice on / off on MIDI channel chan (0..15)"""
    assert on in (0, 1) and 0 <= chan <= 15
    return frame("HV_ON", [on, chan])


def hv_voice_stored(idx):
    assert 0 <= idx < 40
    return frame("HV_VOICE", [0, idx])


def hv_voice_packed(data):
    assert len(data) == 128
    return frame("HV_VOICE", [1] + list(data))


def hv_voice_vced(data):
    assert len(data) == 155
    return frame("HV_VOICE", [2] + list(data))


def hv_param(**kv):
    """HV_PARAM: name=wire value pairs (ALG=4 is algorithm 5; BRIGHT 64 = 0)"""
    p = []
    for k, v in kv.items():
        i = HVP.index(k)
        assert 0 <= v <= HVP_MAX[i], (k, v)
        p += [i, v]
    return frame("HV_PARAM", p)


def hv_state(state, chan, voice, name):
    """device -> host: HV_STATE"""
    return frame("HV_STATE", [state, chan, voice] + string(name))


def voice_names(first, entries, total=40):
    """device -> host: VOICE_NAMES; entries = [(used, name), ...] (1..8)"""
    p = [first, total, len(entries)]
    for used, name in entries:
        p += [used] + string(name)
    return frame("VOICE_NAMES", p)


def voice_data(idx, data):
    """device -> host: VOICE_DATA; data None = an empty slot"""
    return frame("VOICE_DATA", [idx, 0] if data is None else [idx, 1] + list(data))


def crc_part(crc, k, used, data):
    import zlib
    return zlib.crc32(bytes([k, used] + list(data)), crc)


def crc5(c):
    return [(c >> (7 * i)) & 0x7F for i in range(5)]


def bk_part(k, used, data, host=False):
    """BK_PART (device -> host) or, host=True, BK_DATA (host -> device): part k of a backup"""
    assert len(data) == 128
    return frame("BK_DATA" if host else "BK_PART", [k, used] + list(data))


def bk_sum(count, crc, host=False):
    """BK_END (device -> host) or, host=True, BK_INFO"""
    return frame("BK_INFO" if host else "BK_END", [count] + crc5(crc))


def backup_parts(voices):
    """voices: 32 entries, a packed 128-byte record or None (unused): the parts and their CRC, as the device makes them"""
    parts, crc = [], 0
    for k, v in enumerate(voices):
        used, data = (0, [0] * 128) if v is None else (1, list(v))
        parts.append((k, used, data))
        crc = crc_part(crc, k, used, data)
    return parts, crc


def tape_wave(track, label, peaks):
    assert 0 <= track <= 3 and len(peaks) <= 120 and all(0 <= p <= 127 for p in peaks)
    return frame("TAPE_WAVE", [track] + string(label) + [len(peaks)] + list(peaks))


def tape_pos(play, lin, lout, flags):
    assert flags & 4 or lin <= lout
    return frame("TAPE_POS", v14(play) + v14(lin) + v14(lout) + [flags])


def meters(levels, clip=0):
    assert len(levels) == 5
    return frame("METERS", list(levels) + [clip])


def demo_peaks(n=120):
    """a 4-bar loop's peaks: a hit on every beat, decaying, a louder one on each bar"""
    out = []
    for i in range(n):
        beat, ph = divmod(i * 16, n)
        a = (120 if beat % 4 == 0 else 80) * (1.0 - ph / float(n)) ** 2 + 8 * ((i * 7) % 5) / 5.0
        out.append(max(0, min(127, int(a))))
    return out


def factory_name(k):
    return "".join(chr(c) for c in dx7_voices()[k][118:128]).rstrip()


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


def dx7_voices():
    """the eight packed 128-byte factory voices, read from src/synth/fm6_factory.h (the header Felucca's generator wrote)"""
    import re
    from pathlib import Path
    h = (Path(__file__).resolve().parent.parent / "src" / "synth" / "fm6_factory.h").read_text()
    body = h[h.index("FM6_FACTORY"):]
    nums = [int(x) for x in re.findall(r"\b\d+\b", re.sub(r"/\*.*?\*/", "", body[body.index("{"):], flags=re.S))]
    return [nums[i * 128:(i + 1) * 128] for i in range(8)]


def dx7_checksum(data):
    return (128 - (sum(data) & 127)) & 127


def dx7_bank(names=None):
    """a DX7 32-voice bank dump: F0 43 00 09 20 00 <32 x 128> cksum F7; voice k = factory voice k % 8, renamed"""
    v = dx7_voices()
    data = []
    for k in range(32):
        r = list(v[k % 8])
        nm = (names[k] if names else "IMPORT %02d" % (k + 1)).ljust(10)[:10]
        r[118:128] = [ord(c) for c in nm]
        data += r
    return [0xF0, 0x43, 0x00, 0x09, 0x20, 0x00] + data + [dx7_checksum(data), 0xF7]


def dx7_single(k=1, name="ONE VOICE"):
    """one voice: F0 43 00 00 01 1B <155 bytes> cksum F7, the 155-byte (unpacked) layout of voice k"""
    r = list(dx7_voices()[k])
    v = []
    for op in range(6):
        o = r[op * 17:(op + 1) * 17]
        v += o[:11] + [o[11] & 3, o[11] >> 2 & 3, o[12] & 7, o[13] & 3, o[13] >> 2 & 7, o[14], o[15] & 1, o[15] >> 1 & 31, o[16], o[12] >> 3 & 15]
    v += r[102:110] + [r[110] & 31, r[111] & 7, r[111] >> 3 & 1] + r[112:116] + [r[116] & 1, r[116] >> 1 & 7, r[116] >> 4 & 7, r[117]]
    v += [ord(c) for c in name.ljust(10)[:10]]
    assert len(v) == 155, len(v)
    return [0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B] + v + [dx7_checksum(v), 0xF7]


def import_bank_records():
    """the 32 packed records DX7_BANK puts into the user voices (voice k = factory k % 8, named IMPORT kk)"""
    b = dx7_bank()[6:6 + 4096]
    return [b[k * 128:(k + 1) * 128] for k in range(32)]


def v13_table():
    t = {"ACK13": frame("HELLO_ACK", [1, 3]),
         "HV_ON16": hv_on(1, 15), "HV_ON1": hv_on(1, 0), "HV_OFF": hv_on(0, 15),
         "HV_PARAMS": hv_param(ALG=5, VOLUME=50, BRIGHT=70), "HV_BAD_PARAM": frame("HV_PARAM", [0, 32]),
         "HV_TEMPO_FX": hv_param(DELAY=30, DLYTIME=2, ATDEPTH=63),
         "HV_MO_A": hv_param(BRIGHT=20, VOLUME=40, MORPHSET=0), "HV_MO_B": hv_param(BRIGHT=110, VOLUME=90, MORPHSET=1),
         "HV_MO_KNOB": hv_param(MORPHMODE=1, MORPH=100), "HV_MO_DRIFT": hv_param(MORPHMODE=2, MORPH=0),
         "HV_V3": hv_voice_stored(3), "HV_V20": hv_voice_stored(20), "HV_V9": hv_voice_stored(9),
         "HV_VCED": hv_voice_vced(dx7_single()[6:6 + 155]),
         "HV_PACKED": hv_voice_packed(import_bank_records()[5][:118] + [ord(c) for c in "HOST SENT "]),
         "NAMES_REQ": frame("NAMES_REQ"), "VGET_HV": frame("VOICE_GET", [VOICE_HV]),
         "VGET0": frame("VOICE_GET", [0]), "VGET20": frame("VOICE_GET", [20]), "VGET8": frame("VOICE_GET", [8]),
         "HVS_ON0": hv_state(1, 15, 0, factory_name(0)), "HVS_ON3": hv_state(1, 15, 3, factory_name(3)),
         "HVS_VCED": hv_state(1, 15, VOICE_HV, "ONE VOICE"), "HVS_OFF": hv_state(0, 15, VOICE_HV, "ONE VOICE"),
         "HVS_SUSP": hv_state(2, 15, VOICE_HV, "ONE VOICE"), "HVS_BACK": hv_state(1, 15, VOICE_HV, "ONE VOICE"),
         "HVS_PACKED": hv_state(1, 15, VOICE_HV, "HOST SENT"), "HVS_ON9": hv_state(1, 15, 9, "IMPORT 02"),
         "VD_HV": voice_data(VOICE_HV, dx7_voices()[1][:118] + [ord(c) for c in "ONE VOICE "]),
         "VD0": voice_data(0, dx7_voices()[0]), "VD20": voice_data(20, None),
         "VD8_IMPORT": voice_data(8, import_bank_records()[0]),
         "WAVE": tape_wave(0, "T1  LOOP 2  4 BARS", demo_peaks()), "WAVE_OFF": tape_wave(0, "", []),
         "WAVE_T3": tape_wave(2, "T3  LOOP 1  2 BARS", demo_peaks(64)),
         "POS_A": tape_pos(2000, 2048, 14336, 1), "POS_REC": tape_pos(6000, 2048, 14336, 3),
         "POS_NOLOOP": tape_pos(9000, 0, 0, 5), "METERS_A": meters([96, 70, 40, 0, 104]),
         "METERS_CLIP": meters([127, 70, 40, 0, 120], 0x11),
         "WAVE_BAD": frame("TAPE_WAVE", [0, 0, 121] + [1] * 121), "POS_BAD": frame("TAPE_POS", v14(1) + v14(9) + v14(8) + [0]),
         "BK_GET_SUM": frame("BK_GET", [127]), "BK_INFO_NONE": bk_sum(0, 0, True), "BK_INFO_ODD": bk_sum(16, 0, True),
         "BK_ACK_OK": frame("BK_ACK", [0]), "BK_ACK_BAD": frame("BK_ACK", [1])}
    for k in range(32):
        t["BK_GET%d" % k] = frame("BK_GET", [k])
    # the names of a fresh FM-1: the factory voices, the user slots empty; and after DX7_BANK: IMPORT 01 .. 32
    fac = [(1, factory_name(k)) for k in range(8)]
    t["NAMES0"] = voice_names(0, fac)
    for f in (8, 16, 24, 32):
        t["NAMES%d" % f] = voice_names(f, [(0, "")] * 8)
        t["NAMES%d_IMPORT" % f] = voice_names(f, [(1, "IMPORT %02d" % (f - 8 + i + 1)) for i in range(8)])
    # backups: of a fresh FM-1 (nothing used), of the imported bank; the host's copy of the bank, and a damaged one
    for tag, voices in (("EMPTY", [None] * 32), ("IMPORT", import_bank_records())):
        parts, crc = backup_parts(voices)
        for k, used, data in parts:
            t["BK_PART_%s_%d" % (tag, k)] = bk_part(k, used, data)
            t["BK_DATA_%s_%d" % (tag, k)] = bk_part(k, used, data, host=True)
        t["BK_END_" + tag] = bk_sum(32, crc)
        t["BK_INFO_" + tag] = bk_sum(32, crc, True)
    t["BK_INFO_BADCRC"] = bk_sum(32, crc ^ 1, True)
    return t


# ------------------------------------------------------------------ protocol 1.4 ---
MID = dict(RUN4=0, SYNTH=1, AMB=2)
MF_AVAIL, MF_DESC, MF_CTRL = 1, 2, 4
LK = ["number", "enum", "toggle", "action", "mask"]
LE = dict(NOMODE=1, WRONGMODE=2, NODESC=3, UNKNOWN=4, RANGE=5, READONLY=6, REFUSED=7, CLOCK=8, BUSY=9)


def s14(v):
    """a signed value on the wire: v + 8192 as two 7-bit bytes"""
    assert -8192 <= v <= 8191
    return v14(v + 8192)


def crc5_(c):
    return [(c >> (7 * i)) & 0x7F for i in range(5)]


def pset(mode, pairs):
    """PSET: [(id, value), ...] (1..16) for the mode playing"""
    assert 1 <= len(pairs) <= 16
    p = [mode]
    for i, v in pairs:
        p += v14(i) + s14(v)
    return frame("PSET", p)


def mode_frame(mode, flags, h):
    return frame("MODE", [mode, flags] + crc5_(h))


def modes_frame(cur, entries):
    p = [cur, len(entries)]
    for i, fl, nm in entries:
        p += [i, fl] + string(nm)
    return frame("MODES", p)


def pval(mode, entries):
    p = [mode, len(entries)]
    for i, v, t in entries:
        p += v14(i) + s14(v) + string(t)
    return frame("PVAL", p)


def lm_err(mode, code, pid=0):
    return frame("LM_ERR", [mode, LE[code]] + v14(pid))


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


class R:
    def __init__(self, b):
        self.b, self.i = list(b), 0

    def u8(self):
        if self.i >= len(self.b):
            raise ValueError("truncated")
        self.i += 1
        return self.b[self.i - 1]

    def v14(self):
        return self.u8() << 7 | self.u8()

    def s14(self):
        return self.v14() - 8192

    def h5(self):
        return sum(self.u8() << (7 * i) for i in range(5))

    def string(self, mx):
        n = self.u8()
        if n > mx:
            raise ValueError("string longer than %d" % mx)
        out = ""
        for _ in range(n):
            c = self.u8()
            out += chr(0x80 + self.u8()) if c == 0x7F else chr(c)
        return out

    def done(self):
        if self.i != len(self.b):
            raise ValueError("trailing bytes")


def decode14(f):
    """a device -> host 1.4 frame (F0 .. F7) as a dict, or None for another command; raises ValueError"""
    if len(f) < 6 or f[:4] != HDR or f[-1] != 0xF7 or not 0x0D <= f[4] <= 0x17:
        return None
    c, r = f[4], R(f[5:-1])
    if c == 0x0D:
        d = dict(cmd="MODES", cur=r.u8(), modes=[])
        for _ in range(r.u8()):
            d["modes"].append((r.u8(), r.u8(), r.string(12)))
    elif c == 0x0E:
        d = dict(cmd="MODE", mode=r.u8(), flags=r.u8(), hash=r.h5())
    elif c == 0x0F:
        d = dict(cmd="DESC_HEAD", mode=r.u8(), hash=r.h5(), npages=r.u8(), nparams=r.v14(), nstate=r.u8())
        d["title"] = r.string(16)
        d["state"] = [r.string(16) for _ in range(d["nstate"])]
    elif c == 0x10:
        d = dict(cmd="DESC_PAGE", page=r.u8(), title=r.string(16))
    elif c == 0x11:
        d = dict(cmd="DESC_PARAM", id=r.v14(), page=r.u8(), kind=LK[r.u8()], flags=r.u8(), step=r.u8(), lo=r.s14(),
                 hi=r.s14(), default=r.s14(), nlabels=r.u8(), name=r.string(16), unit=r.string(8))
        d.update(value=r.s14(), text=r.string(16))
        assert d["lo"] <= d["default"] <= d["hi"] and d["lo"] <= d["value"] <= d["hi"], d
    elif c == 0x12:
        d = dict(cmd="DESC_LABELS", id=r.v14(), first=r.u8(), total=r.u8())
        d["labels"] = [r.string(16) for _ in range(r.u8())]
    elif c == 0x13:
        d = dict(cmd="DESC_END", mode=r.u8(), hash=r.h5())
    elif c == 0x14:
        d = dict(cmd="PVAL", mode=r.u8(), values=[])
        for _ in range(r.u8()):
            d["values"].append((r.v14(), r.s14(), r.string(16)))
    elif c == 0x15:
        d = dict(cmd="LSTATE", mode=r.u8(), flags=r.u8())
        d["texts"] = [r.string(16) for _ in range(r.u8())]
    elif c == 0x17:
        d = dict(cmd="LVIEW", mode=r.u8(), page=r.u8())
        n = r.u8()
        if n > 4 or d["page"] >= 32:
            raise ValueError("LVIEW: %d ids, page %d" % (n, d["page"]))
        d["ids"] = [r.v14() for _ in range(n)]
    else:
        d = dict(cmd="LM_ERR", mode=r.u8(), code=r.u8(), id=r.v14())
    r.done()
    assert len(f) <= 136, "a device frame longer than 136 bytes"
    return d


def desc_hash_part(f):
    """the bytes of a description frame its hash covers (docs/PROTOCOL.md "The hash")"""
    if f[4] == 0x0F:
        return f[4:6] + f[11:-1]
    if f[4] in (0x10, 0x12):
        return f[4:-1]
    if f[4] == 0x11:
        r = R(f[5:-1])
        for _ in range(13):
            r.u8()
        r.string(16)
        r.string(8)
        return f[4:5 + r.i]
    return []


class Desc:
    """a description as a host collects it: DESC_HEAD .. DESC_END, checked (counts, labels, hash)"""

    def __init__(self):
        self.head, self.pages, self.params, self.h, self.done = None, {}, [], 0x811C9DC5, False

    def feed(self, f, d):
        c = d["cmd"]
        if c == "DESC_HEAD":
            self.__init__()
            self.head = d
        elif self.head is None:
            return
        if c in ("DESC_HEAD", "DESC_PAGE", "DESC_PARAM", "DESC_LABELS"):
            self.h = fnv(self.h, desc_hash_part(f))
        if c == "DESC_PAGE":
            assert d["page"] == len(self.pages), "pages in order"
            self.pages[d["page"]] = d["title"]
        elif c == "DESC_PARAM":
            d["labels"] = []
            self.params.append(d)
        elif c == "DESC_LABELS":
            p = self.params[-1]
            assert d["id"] == p["id"] and d["total"] == p["nlabels"] and d["first"] == len(p["labels"]), "labels in order"
            p["labels"] += d["labels"]
        elif c == "DESC_END":
            h = self.h or 1
            assert d["mode"] == self.head["mode"], "END's mode"
            assert len(self.pages) == self.head["npages"] and len(self.params) == self.head["nparams"], "counts"
            for p in self.params:
                assert len(p["labels"]) == p["nlabels"], ("labels of", p["name"])
                assert p["page"] < self.head["npages"], ("page of", p["name"])
            assert len(set(p["id"] for p in self.params)) == len(self.params), "ids unique"
            assert h == self.head["hash"] == d["hash"], "hash %08X head %08X end %08X" % (h, self.head["hash"], d["hash"])
            self.done = True


def split_frames(raw):
    out, cur = [], None
    for b in raw:
        if b == 0xF0:
            cur = [b]
        elif cur is not None:
            cur.append(b)
            if b == 0xF7:
                out.append(cur)
                cur = None
    return out


# what docs/PROTOCOL.md promises for each mode: (id, page, kind, name)
AMB_PARAMS = [(32, 0, "enum", "PIECE"), (33, 0, "mask", "NOTES"), (34, 0, "toggle", "GROW"), (35, 0, "toggle", "FREEZE"),
              (36, 0, "action", "NEXT CHORD"), (37, 0, "action", "SAVE"), (38, 0, "action", "RESET PIECE"),
              (18, 1, "number", "TEMPO"), (5, 1, "number", "EVOLVE"), (15, 1, "number", "MOTION"), (6, 1, "number", "DRIFT"),
              (0, 2, "number", "LEVEL"), (13, 2, "number", "SWELL"), (12, 2, "number", "BRIGHT"), (22, 2, "enum", "COLOUR"),
              (1, 3, "number", "LEVEL"), (4, 3, "number", "DENSITY"), (14, 3, "number", "TONE"), (21, 3, "number", "OCTAVE"),
              (2, 4, "number", "LEVEL"), (23, 4, "number", "OCTAVE"), (24, 4, "number", "GLIDE"), (25, 4, "number", "FIFTH"),
              (3, 5, "number", "LEVEL"), (7, 5, "number", "HITS"), (26, 5, "number", "SWING"), (27, 5, "number", "HATS"),
              (8, 6, "number", "ECHO"), (9, 6, "enum", "ECHO TIME"), (10, 6, "number", "REVERB"), (11, 6, "number", "SIZE"),
              (19, 7, "enum", "ROOT"), (16, 7, "number", "TUNE"), (17, 7, "enum", "MIDI OUT"), (20, 7, "enum", "SCALE")]
AMB_PAGES = ["PLAY", "GARDEN", "PAD", "BELLS", "BASS", "PULSE", "SPACE", "KEY"]
SYNTH_PAGES = ["VOICE", "SYNTH", "MORPH", "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "LFO", "EFFECTS", "GLOBAL", "ARP", "SEQ"]


def desc_check(path, which=None):
    raw = open(path, "rb").read()
    descs, modes, n14, views = [], [], 0, []
    cur = Desc()
    for f in split_frames(raw):
        d = decode14(f)
        if d is None:
            continue
        n14 += 1
        if d["cmd"] == "MODE":
            modes.append(d)
        if d["cmd"] == "LVIEW":                 # 1.5: checked against the last description of its mode before it
            last = [x for x in descs if x.head["mode"] == d["mode"]]
            assert last, "an LVIEW before its mode's description"
            views.append((d, last[-1]))
        cur.feed(f, d)
        if cur.done:
            descs.append(cur)
            cur = Desc()
    assert descs, "no complete description in %s" % path
    for ds in descs:
        h = ds.head
        mf = [m for m in modes if m["mode"] == h["mode"]]
        assert mf and all(m["hash"] == h["hash"] and m["flags"] & MF_DESC for m in mf), "MODE's hash is the description's"
        print("description: mode %d %r, %d pages, %d parameters, state %s, hash %08X (MODE agrees)" %
              (h["mode"], h["title"], h["npages"], h["nparams"], "/".join(h["state"]), h["hash"]))
        for p in ds.params:
            lab = (" [%s]" % " ".join(p["labels"])) if p["labels"] else ""
            print("  %4d  %-8s %-6s %-12s %d..%d step %d def %d flags %d unit %r = %d %r%s" %
                  (p["id"], ds.pages[p["page"]], p["kind"], p["name"], p["lo"], p["hi"], p["step"], p["default"],
                   p["flags"], p["unit"], p["value"], p["text"], lab))
    if which == "amb":
        ds = [d for d in descs if d.head["mode"] == 2][-1]
        got = [(p["id"], p["page"], p["kind"], p["name"]) for p in ds.params]
        assert got == AMB_PARAMS, "AMB's parameters differ from docs/PROTOCOL.md:\n%r" % got
        assert [ds.pages[i] for i in range(len(ds.pages))] == AMB_PAGES
        assert ds.head["state"] == ["PIECE", "CHORD", "SCALE", "STATE"]
    if which == "synth":
        ds = [d for d in descs if d.head["mode"] == 1][-1]
        assert [ds.pages[i] for i in range(len(ds.pages))] == SYNTH_PAGES
        assert len(ds.params) == 143 and ds.params[0]["name"] == "VOICE"
        ids = [p["id"] for p in ds.params]
        assert ids[:3] == [150, 151, 152] and ids[3:11] == list(range(8)) and ids[11:107] == list(range(16, 112))
    for d, ds in views:
        assert d["page"] < ds.head["npages"], "LVIEW page %d of %d" % (d["page"], ds.head["npages"])
        assert not d["ids"] or d["ids"][-1] != LVIEW_NONE, "LVIEW: an empty knob at the end"
        on = dict((p["id"], p["page"]) for p in ds.params)
        for i in d["ids"]:
            assert i == LVIEW_NONE or on.get(i) == d["page"], "LVIEW page %d: id %d is not a parameter of that page" % (
                d["page"], i)
        print("  LVIEW mode %d page %d %s: %s" % (d["mode"], d["page"], ds.pages[d["page"]],
                                                " ".join(("-" if i == LVIEW_NONE else "%d %s" % (i, next(
                                                    p["name"] for p in ds.params if p["id"] == i))) for i in d["ids"])))
    print("desc %s: %d 1.4 frames, %d description(s), %d LVIEW checked" % (path, n14, len(descs), len(views)))
    return 0


# ------------------------------------------------------------------ protocol 1.5 ---
LVIEW_NONE = 16383                     # a knob without a described parameter


def lview(mode, page, ids):
    """device -> host LVIEW: the page shown, the ids of K1..Kn (NONE for an empty knob; none at the end)"""
    assert 0 <= page < 32 and len(ids) <= 4 and (not ids or ids[-1] != LVIEW_NONE)
    p = [mode, page, len(ids)]
    for i in ids:
        p += v14(i)
    return frame("LVIEW", p)


def lview_set(mode, page):
    """host -> device LVIEW_SET: show that description page"""
    return frame("LVIEW_SET", [mode, page])


def synth_view(sp, sub=0, op=0):
    """Synth mode's LVIEW by docs/PROTOCOL.md ("Protocol 1.5"): a device page (HOME EDIT ENV LFO FX GLO ARP SEQ), its
    sub-page and the operator"""
    if sp == "HOME":
        return lview(1, 1 + sub, [4 * sub + k for k in range(4)])
    if sp in ("EDIT", "ENV"):
        return lview(1, 3 + op, [16 + 16 * op + (8 if sp == "ENV" else 0) + 4 * sub + k for k in range(4)])
    if sp == "GLO":
        return lview(1, 11, [[128, 129, 130, 131], [132, 133], [136, 137, 138, 139]][sub])
    if sp == "SEQ":
        return lview(1, 13, [])
    base, page = dict(LFO=(112, 9), FX=(120, 10), ARP=(140, 12))[sp]
    return lview(1, page, [base + 4 * sub + k for k in range(4)])


def v15_table():
    """the 1.5 frames the scenarios use"""
    t = {"ACK15": frame("HELLO_ACK", [1, 5])}
    for pg in range(1, 8):                  # AMB's device pages: the ids its description puts on them, in knob order
        t["LVIEW_AMB_%d" % pg] = lview(2, pg, [i for i, p, _, _ in AMB_PARAMS if p == pg])
    for pg in range(0, 16):
        t["LVIEW_SET_AMB_%d" % pg] = lview_set(2, pg)
        t["LVIEW_SET_SYNTH_%d" % pg] = lview_set(1, pg)
        t["LVIEW_SET_RUN4_%d" % pg] = lview_set(0, pg)
    for sp, nsub in (("HOME", 2), ("LFO", 2), ("FX", 2), ("GLO", 3), ("ARP", 2), ("SEQ", 1)):
        for sub in range(nsub):
            t["LVIEW_SY_%s_%d" % (sp, sub)] = synth_view(sp, sub)
    for sp in ("EDIT", "ENV"):
        for op in range(6):
            for sub in range(2):
                t["LVIEW_SY_%s_OP%d_%d" % (sp, op + 1, sub)] = synth_view(sp, sub, op)
    for v in range(0, 101):                 # AMB's PAD LEVEL (id 0) as a knob turns
        t["PVAL_PAD_%d" % v] = pval(2, [(0, v, "%d" % v)])
    t.update({"ERR_RANGE_SYNTH_0": lm_err(1, "RANGE", 0), "ERR_WRONG_SYNTH": lm_err(1, "WRONGMODE"),
              "ERR_NODESC_RUN4": lm_err(0, "NODESC"), "ERR_WRONG_RUN4": lm_err(0, "WRONGMODE")})
    return t


def v14_table():
    """the 1.4 frames the scenarios use"""
    t = {"ACK14": frame("HELLO_ACK", [1, 4]), "MODES_REQ": frame("MODES_REQ")}
    for nm, i in list(MID.items()) + [("NINE", 9)]:
        t["MODE_SET_" + nm] = frame("MODE_SET", [i])
        t["DESC_REQ_" + nm] = frame("DESC_REQ", [i])
        t["VALS_REQ_" + nm] = frame("VALS_REQ", [i])
    av = lambda amb: [(0, MF_AVAIL | MF_CTRL, "RUN4"), (1, MF_AVAIL | MF_DESC, "SYNTH"),     # noqa: E731
                      (2, (MF_AVAIL | MF_DESC) if amb else 0, "AMB")]
    for cur in range(3):
        t["MODES_%d" % cur] = modes_frame(cur, av(True))
        t["MODES_NOAMB_%d" % cur] = modes_frame(cur, av(False))
    t["MODE_RUN4"] = mode_frame(0, MF_AVAIL | MF_CTRL, 0)
    t.update({
        "ERR_NODESC_RUN4": lm_err(0, "NODESC"), "ERR_NOMODE_RUN4": lm_err(0, "NOMODE"), "ERR_NOMODE_SYNTH": lm_err(1, "NOMODE"),
        "ERR_WRONG_AMB": lm_err(2, "WRONGMODE"),
        # AMB (mode 2): PAD LEVEL (id 0), FREEZE (35), NEXT CHORD (36), NOTES (33), GROW (34), PIECE (32), SCALE (20)
        "PSET_PAD70": pset(2, [(0, 70)]), "PVAL_PAD70": pval(2, [(0, 70, "70")]), "PVAL_PAD73": pval(2, [(0, 73, "73")]),
        "PSET_FREEZE1": pset(2, [(35, 1)]), "PVAL_FREEZE1": pval(2, [(35, 1, "ON")]),
        "PSET_FREEZE0": pset(2, [(35, 0)]), "PVAL_FREEZE0": pval(2, [(35, 0, "OFF")]),
        "PSET_NEXT": pset(2, [(36, 1)]), "PSET_NEXT0": pset(2, [(36, 0)]),
        "PSET_TWO": pset(2, [(10, 30), (11, 40)]), "PVAL_TWO": pval(2, [(10, 30, "30"), (11, 40, "40")]),
        "PSET_UNKNOWN": pset(2, [(10, 50), (99, 1)]), "PSET_RANGE": pset(2, [(0, 101)]),
        "PSET_NOROOT": pset(2, [(33, 0xAB5 & ~(1 << 2))]),            # D taken out of D dorian: the root stays
        "PSET_NOTES": pset(2, [(33, 0xAB5 & ~(1 << 5))]),             # F taken out
        "PVAL_NOTES": pval(2, [(33, 0xAB5 & ~(1 << 5), "6 NOTES"), (20, 8, "CUSTOM")]),
        "PSET_GROW0": pset(2, [(34, 0)]), "PSET_GROW1": pset(2, [(34, 1)]),
        "ERR_UNKNOWN_99": lm_err(2, "UNKNOWN", 99), "ERR_RANGE_0": lm_err(2, "RANGE", 0), "ERR_RANGE_36": lm_err(2, "RANGE", 36),
        "ERR_REFUSED_33": lm_err(2, "REFUSED", 33), "ERR_CLOCK_34": lm_err(2, "CLOCK", 34),
        "PSET_WRONG": pset(1, [(0, 1)]), "PSET_PIECE2": pset(2, [(32, 1)]),
        # SYNTH (mode 1): VOICE (150) F4, BRIGHT (0) +10, an empty user slot, PLAY (153)
        "PSET_SY_VOICE4": pset(1, [(150, 4)]), "PSET_SY_BRIGHT": pset(1, [(0, 10)]), "PVAL_SY_BRIGHT": pval(1, [(0, 10, "+10")]),
        "PSET_SY_EMPTY": pset(1, [(150, 20)]), "ERR_SY_REFUSED": lm_err(1, "REFUSED", 150),
        "PSET_SY_OP3": pset(1, [(16 + 2 * 16 + 2, 50)]),               # OP3 LEVEL = 50
        "PSET_SY_PLAY1": pset(1, [(153, 1)]), "PSET_SY_PLAY0": pset(1, [(153, 0)]),
    })
    return t


def expand(name):
    """a scenario with lines '@name@' replaced by 'sysex HEX'"""
    table = {
        "ACK": frame("HELLO_ACK", [1, 1]), "ACK10": frame("HELLO_ACK", [1]),
        "LIST": list_frame("MODE", (127, 70, 20), 5, 0, 1, ["TAPE", "POLY", "DISC", "PERFORM", "T\u00d3LF"]),
        "LIST_LONG": list_frame("KITS", (30, 105, 100), 40, 0, 6, ["KIT %02d" % i for i in range(7)]),
        "LIST_LONG2": list_frame("KITS", (30, 105, 100), 40, 7, 7, ["KIT %02d" % i for i in range(7, 14)]),
        "LIST_CLOSE": list_frame("", (0, 0, 0), 0, 0, 0, []),
        "LIST_BAD": frame("LIST", string("X") + [0, 0, 0, 2, 0, 5, 0]), "TAPE": tape(), "TOLF": tolf(),
        "LEDS": leds({"PLAY": "on", "REC": "blink", "EDIT": "dim"}, {0: "dim", 12: "on"}),
        "STATUS": status(1203, 12, 2, 1), "STATUS2": status(980, 3, 15, 3), "VALUE": value(1, 9000, "1.10x"),
        "TOAST": toast("SAVED", 12),
        "BAD_PAGE": HDR + [CMD["PAGE"], 1, 0x41, 0xF7],                    # truncated
        "SR_FRAME": [0xF0, 0x7D, 0x53, 0x52, 0x01, 0xF7],                  # fm1-reader's: not ours
        "HELLO": frame("HELLO", [1, 0, 8, 0, 5]), "SYNC": frame("SYNC_REQ"),
        "HELLO_LOCAL": frame("HELLO", [1, 0, 8, 1, 5]),
        "ACK12": frame("HELLO_ACK", [1, 2]), "WEAR": wear(), "WEAR_NOCUR": wear(NONE, 0),
        "FOCUS2": focus(2, 5, 12), "FOCUS_NONE": focus(NONE, 0, 0), "FOCUS_BAD": frame("FOCUS", [5, 0, 0, 0, 0]),
        "V1_FAIL": value(1, 5200, "32%"), "V1_FAIL2": value(1, 6200, "38%"), "V2_BIG": value(2, 12000, "0.73", 1),
        "V0": value(0, 7000, "43%"), "V1_BADFLAG": frame("VALUE", [1] + v14(1) + string("x") + [2]),
        "DX7_BANK": dx7_bank(), "DX7_ONE": dx7_single(), "DX7_BAD": dx7_bank()[:-2] + [0x00, 0xF7],
        "POT_8192": frame("POT", v14(8192)), "POT_MAX": frame("POT", v14(16383)),
    }
    table.update(v13_table())
    table.update(v14_table())
    table.update(v15_table())
    for i in range(14):
        table["BTN%d_1" % i] = frame("BTN", [i, 1])
        table["BTN%d_0" % i] = frame("BTN", [i, 0])
    for i in range(40):
        for a in range(3):
            table["PICK%d_%d" % (i, a)] = frame("LIST_PICK", [i, a])
    for e in range(7):
        for d in (1, -1, 3, 5, 63, -63, -37):
            table["ENC%d_%+d" % (e, d)] = frame("ENC", [e, 0x40 + d])
    out = []
    for line in open(name).read().splitlines():
        s = line.strip()
        if s.startswith("ble@") and s.endswith("@"):          # the same frame, from the computer over Bluetooth MIDI
            out.append("blesx " + hx(table[s[4:-1]]))
        elif s.startswith("@") and s.endswith("@"):
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
    elif a[1] == "desc":
        return desc_check(a[2], a[3] if len(a) > 3 else None)
    elif a[1] == "expect":
        k = a[2]
        f = {"btn": lambda: frame("BTN", [int(a[3]), int(a[4])]),
             "enc": lambda: frame("ENC", [int(a[3]), 0x40 + int(a[4])]),
             "pot": lambda: frame("POT", v14(int(a[3]))),
             "hello": lambda: frame("HELLO", [1, 0, 8, 0, 5]), "sync": lambda: frame("SYNC_REQ")}[k]()
        print(hx(f))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
