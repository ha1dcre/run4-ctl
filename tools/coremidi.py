# SPDX-License-Identifier: GPL-3.0-only
"""A very small CoreMIDI binding (macOS only, standard library only: ctypes). It exists because python-rtmidi does not
build on this Mac's Python (sem_timedwait is missing in its bundled RtMidi). It offers what tools/fake_fm1.py needs:
a virtual source and a virtual destination with the same name, raw bytes in (SysEx included) and raw bytes out."""
import ctypes
import ctypes.util
import queue
import struct

cm = ctypes.cdll.LoadLibrary("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
cf = ctypes.cdll.LoadLibrary("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
cf.CFStringCreateWithCString.restype = ctypes.c_void_p
cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
cf.CFRelease.argtypes = [ctypes.c_void_p]
K_UTF8 = 0x08000100

U32 = ctypes.c_uint32
READPROC = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
cm.MIDIClientCreate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(U32)]
cm.MIDISourceCreate.argtypes = [U32, ctypes.c_void_p, ctypes.POINTER(U32)]
cm.MIDIDestinationCreate.argtypes = [U32, ctypes.c_void_p, READPROC, ctypes.c_void_p, ctypes.POINTER(U32)]
cm.MIDIInputPortCreate.argtypes = [U32, ctypes.c_void_p, READPROC, ctypes.c_void_p, ctypes.POINTER(U32)]
cm.MIDIOutputPortCreate.argtypes = [U32, ctypes.c_void_p, ctypes.POINTER(U32)]
cm.MIDIPortConnectSource.argtypes = [U32, U32, ctypes.c_void_p]
cm.MIDIPacketListInit.restype = ctypes.c_void_p
cm.MIDIPacketListInit.argtypes = [ctypes.c_void_p]
cm.MIDIPacketListAdd.restype = ctypes.c_void_p
cm.MIDIPacketListAdd.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_ulong,
                                 ctypes.c_char_p]
cm.MIDIReceived.argtypes = [U32, ctypes.c_void_p]
cm.MIDISend.argtypes = [U32, U32, ctypes.c_void_p]
cm.MIDIEndpointDispose.argtypes = [U32]


def _cfstr(s):
    return cf.CFStringCreateWithCString(None, s.encode(), K_UTF8)


def _check(rc, what):
    if rc:
        raise OSError(f"CoreMIDI {what}: error {rc}")


def parse_packets(ptr):
    """the byte strings of a MIDIPacketList (UInt32 count, then packets: UInt64 time, UInt16 length, bytes; 4-aligned)"""
    base = ctypes.cast(ptr, ctypes.c_void_p).value
    n = struct.unpack("<I", ctypes.string_at(base, 4))[0]
    off, out = 4, []
    for _ in range(n):
        length = struct.unpack("<H", ctypes.string_at(base + off + 8, 2))[0]
        out.append(ctypes.string_at(base + off + 10, length))
        off = (off + 10 + length + 3) & ~3
    return out


def _packet_list(data):
    buf = ctypes.create_string_buffer(len(data) + 64)
    pkt = cm.MIDIPacketListInit(buf)
    pkt = cm.MIDIPacketListAdd(buf, len(buf), pkt, 0, len(data), bytes(data))
    if not pkt:
        raise OSError("MIDIPacketListAdd: message too big")
    return buf


class Client:
    def __init__(self, name):
        self.ref = U32()
        s = _cfstr(name)
        _check(cm.MIDIClientCreate(s, None, None, ctypes.byref(self.ref)), "client")
        cf.CFRelease(s)
        self.rx = queue.Queue()                  # byte strings from the destination
        self._keep = []

    def _reader(self, q):
        def proc(plist, _ref, _src):
            for p in parse_packets(plist):
                q.put(p)
        cb = READPROC(proc)
        self._keep.append(cb)
        return cb

    def virtual_source(self, name):
        """what a host sees as an input: we send through it (send)"""
        ep, s = U32(), _cfstr(name)
        _check(cm.MIDISourceCreate(self.ref, s, ctypes.byref(ep)), "source")
        cf.CFRelease(s)
        self.src = ep.value
        return ep.value

    def virtual_destination(self, name):
        """what a host sees as an output: it sends to us (self.rx)"""
        ep, s = U32(), _cfstr(name)
        _check(cm.MIDIDestinationCreate(self.ref, s, self._reader(self.rx), None, ctypes.byref(ep)), "destination")
        cf.CFRelease(s)
        self.dst = ep.value
        return ep.value

    def send(self, data):
        """bytes out through the virtual source (whole messages: a SysEx frame in one call)"""
        _check(cm.MIDIReceived(self.src, _packet_list(data)), "send")

    # test helpers: a second port in the same process, as a host would act
    def listen_to(self, source_ref, q):
        port, s = U32(), _cfstr("in")
        _check(cm.MIDIInputPortCreate(self.ref, s, self._reader(q), None, ctypes.byref(port)), "input port")
        _check(cm.MIDIPortConnectSource(port.value, source_ref, None), "connect")
        cf.CFRelease(s)

    def send_to(self, dest_ref, data):
        port, s = U32(), _cfstr("out")
        _check(cm.MIDIOutputPortCreate(self.ref, s, ctypes.byref(port)), "output port")
        cf.CFRelease(s)
        _check(cm.MIDISend(port.value, dest_ref, _packet_list(data)), "midisend")


# ---- finding other endpoints by name (the Network MIDI session's port, "Session 1") ----
cm.MIDIGetNumberOfSources.restype = ctypes.c_ulong
cm.MIDIGetNumberOfDestinations.restype = ctypes.c_ulong
cm.MIDIGetSource.restype = U32
cm.MIDIGetSource.argtypes = [ctypes.c_ulong]
cm.MIDIGetDestination.restype = U32
cm.MIDIGetDestination.argtypes = [ctypes.c_ulong]
cm.MIDIObjectGetStringProperty.argtypes = [U32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
cf.CFStringGetCString.restype = ctypes.c_bool


def endpoint_name(ref):
    s = ctypes.c_void_p()
    key = _cfstr("name")
    rc = cm.MIDIObjectGetStringProperty(ref, key, ctypes.byref(s))
    cf.CFRelease(key)
    if rc or not s.value:
        return ""
    buf = ctypes.create_string_buffer(256)
    cf.CFStringGetCString(s, buf, 256, K_UTF8)
    cf.CFRelease(s)
    return buf.value.decode("utf-8", "replace")


def find(name, destinations):
    """[(ref, name)] of the sources (destinations=False) or destinations whose name contains `name`"""
    n = cm.MIDIGetNumberOfDestinations() if destinations else cm.MIDIGetNumberOfSources()
    out = []
    for i in range(n):
        ref = cm.MIDIGetDestination(i) if destinations else cm.MIDIGetSource(i)
        nm = endpoint_name(ref)
        if name in nm:
            out.append((ref, nm))
    return out
