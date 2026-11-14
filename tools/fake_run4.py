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
CMD = dict(HELLO=0x01, BTN=0x02, ENC=0x03, POT=0x04, SYNC_REQ=0x05, LIST_PICK=0x06,
           HV_STATE=0x07, VOICE_NAMES=0x08, VOICE_DATA=0x09, BK_PART=0x0A, BK_END=0x0B, BK_GET=0x0C,     # 1.3
           HELLO_ACK=0x41, PAGE=0x42, VALUE=0x43, LEDS=0x44, STATUS=0x45, TOAST=0x46, LIST=0x47, FOCUS=0x48,
           HV_ON=0x49, HV_VOICE=0x4A, HV_PARAM=0x4B, NAMES_REQ=0x4C, VOICE_GET=0x4D,                     # 1.3
           TAPE_WAVE=0x4E, TAPE_POS=0x4F, METERS=0x50, BK_ACK=0x51, BK_INFO=0x52, BK_DATA=0x53)
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
        "HELLO": frame("HELLO", [1, 0, 7, 0, 3]), "SYNC": frame("SYNC_REQ"),
        "ACK12": frame("HELLO_ACK", [1, 2]), "WEAR": wear(), "WEAR_NOCUR": wear(NONE, 0),
        "FOCUS2": focus(2, 5, 12), "FOCUS_NONE": focus(NONE, 0, 0), "FOCUS_BAD": frame("FOCUS", [5, 0, 0, 0, 0]),
        "V1_FAIL": value(1, 5200, "32%"), "V1_FAIL2": value(1, 6200, "38%"), "V2_BIG": value(2, 12000, "0.73", 1),
        "V0": value(0, 7000, "43%"), "V1_BADFLAG": frame("VALUE", [1] + v14(1) + string("x") + [2]),
        "DX7_BANK": dx7_bank(), "DX7_ONE": dx7_single(), "DX7_BAD": dx7_bank()[:-2] + [0x00, 0xF7],
        "POT_8192": frame("POT", v14(8192)), "POT_MAX": frame("POT", v14(16383)),
    }
    table.update(v13_table())
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
    elif a[1] == "expect":
        k = a[2]
        f = {"btn": lambda: frame("BTN", [int(a[3]), int(a[4])]),
             "enc": lambda: frame("ENC", [int(a[3]), 0x40 + int(a[4])]),
             "pot": lambda: frame("POT", v14(int(a[3]))),
             "hello": lambda: frame("HELLO", [1, 0, 7, 0, 3]), "sync": lambda: frame("SYNC_REQ")}[k]()
        print(hx(f))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
