// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FakeFM1.swift
// A Swift port of the simulated FM-1 on a MIDI backend, after fm1-base's tests/install_test.py FakeFM1 and
// felucca-bsp's chip/ota.c (the running firmware: handshake, UPGRADE, staging reads,
// "success") and loader/ldr_core.c (the update loader: the fast install's hello, the sector
// table, the sectors that differ, progress, FINISH_WRITE). Test harness only.

import Foundation

final class FakeQueue {
    private let cond = NSCondition()
    private var items: [[UInt8]] = []
    func put(_ p: [UInt8]) { cond.lock(); items.append(p); cond.signal(); cond.unlock() }
    func get(timeout: TimeInterval) -> [UInt8]? {
        cond.lock(); defer { cond.unlock() }
        let end = Date().addingTimeInterval(timeout)
        while items.isEmpty {
            if !cond.wait(until: end) { break }
        }
        return items.isEmpty ? nil : items.removeFirst()
    }
    func drain() { cond.lock(); items.removeAll(); cond.unlock() }
}

final class FakeLink: FM1Link {
    unowned let dev: FakeFM1
    let gen: Int
    let q = FakeQueue()
    var closed = false
    init(dev: FakeFM1, gen: Int) { self.dev = dev; self.gen = gen }
    var lost: Bool { gen != dev.gen || !dev.connected }
    func send(_ pkt: [UInt8]) -> Bool {
        if lost { return false }
        dev.lock.lock(); dev.sent.append(pkt); dev.lock.unlock()
        dev.rx(pkt)
        return true
    }
    func read(timeout: TimeInterval) -> [UInt8]? { q.get(timeout: timeout) }
    func drain() { q.drain() }
    func close() { closed = true }
}

final class FakeFM1: FM1Backend {
    enum Mode { case scripted, realistic }
    struct OpenError: Error {}
    let lock = NSRecursiveLock()
    var mode: Mode
    var image: [UInt8]
    var identity: String
    var name: String
    var afterWrite: String
    var newOta: Set<String> = []
    var jedec = 0
    var flashOff = 0x400
    /// Realistic: the flash.bin sectors the device holds now (nil = a whole fresh image's worth of other bytes).
    var flash: [[UInt8]] = []
    var supportsFast = true
    var unplugAfterServed: Int?
    var stallAfterServed: Int?
    var cutAfterSectors: Int?
    /// The running firmware refuses an image that starts like this (V15 on a V16 boot head).
    var refusePrefix: [UInt8]?
    var refuseArm = false

    var sent: [[UInt8]] = []
    var links: [FakeLink] = []
    var gen = 0
    var connected = true
    var armed = false
    var arms: [[UInt8]] = []
    var upgrades = 0, served = 0, bad = 0, hsFull = 0
    var sectorsRequested = 0, sectorsWritten = 0
    var tableReads = 0
    var fastSeen = false
    var fast = false
    var waiting: (UInt32, Int)?
    var queue: [(UInt32, Int)] = []
    var todo: [Int] = []
    var backupBytes: [UInt8]?
    var backupCut = false
    var backupBadCRC = false

    init(image: [UInt8], identity: String = "FM-1_015", name: String = "FM-1", afterWrite: String = "FM-1_900",
         mode: Mode = .scripted) {
        self.image = image
        self.identity = identity
        self.name = name
        self.afterWrite = afterWrite
        self.mode = mode
        boot(identity, name)
    }

    var nsec: Int { (image.count - flashOff) / 4096 }

    func boot(_ identity: String, _ name: String) {
        lock.lock(); defer { lock.unlock() }
        gen += 1
        self.identity = identity
        self.name = name
        connected = true
        waiting = nil
        queue = []
        fastSeen = false
        fast = false
    }

    func inputNames() -> [String] { connected ? [name] : [] }
    func outputNames() -> [String] { inputNames() }
    func open(input: String, output: String) throws -> FM1Link {
        guard input == name, connected else { throw OpenError() }
        let l = FakeLink(dev: self, gen: gen)
        links.append(l)
        return l
    }

    func unplug() { lock.lock(); connected = false; lock.unlock() }
    /// Plugged in again: the unit comes back as what its flash says (an interrupted install: the loader).
    func replug(_ identity: String, _ name: String) { boot(identity, name) }

    func tx(_ pkt: [UInt8]) {
        for l in links where l.gen == gen && !l.closed { l.q.put(pkt) }
    }

    func ident(_ text: String) {
        var b = Array(text.utf8) + [UInt8](repeating: 0, count: 27 - text.utf8.count)
        b[24] = UInt8((jedec >> 16) & 0xFF); b[25] = UInt8((jedec >> 8) & 0xFF); b[26] = UInt8(jedec & 0xFF)
        tx([0xF0] + FM1Wire.pack7([0, 0x59, 0x11, 27, 0, 0] + b + [FM1Wire.checksum(b)]) + [0xF7])
    }

    /// chip/ota.c: the name, '_' and the first three digits ("FM-1_6010000" -> "FM-1_601")
    func shortIdentity() -> String {
        guard let i = identity.firstIndex(of: "_") else { return identity }
        return String(identity.prefix(identity.distance(from: identity.startIndex, to: i) + 4))
    }

    func request(_ addr: UInt32, _ n: Int) {
        waiting = (addr, n)
        var u: [UInt8] = [0, 0x59, 0x30, 0, 0, 0, 0] + FM1Wire.le32(addr) + [UInt8(n & 0xFF), UInt8(n >> 8), 0]
        u.append(FM1Wire.checksum(u[6..<14]))
        tx([0xF0] + FM1Wire.pack7(u) + [0xF7])
    }

    func next() {
        if queue.isEmpty { return }
        let (a, n) = queue.removeFirst()
        request(a, n)
    }

    func rx(_ d: [UInt8]) {
        lock.lock(); defer { lock.unlock() }
        let u = FM1Wire.unpack7(d[1..<(d.count - 1)])
        if identity.hasPrefix("FM1_"), u.count >= 7, u[0] == 0, u[1] == 0x59, u[2] == 49 || u[2] == 50 {
            openCmd(u[2], Array(u[6..<(u.count - 1)]))
            return
        }
        if let f = FM1Wire.parseFrame(d), f.cmd == 0x42, backupBytes != nil, !identity.hasPrefix("ota-") {
            sendBackup()
            return
        }
        let new = newOta.contains(identity)
        if d == FM1Wire.hsQuery {
            ident(new ? shortIdentity() : identity)
        } else if d == FM1Wire.hsFull {
            hsFull += 1
            if new { ident(identity) }
        } else if d == FM1Wire.upgrade {
            if identity.hasPrefix("FM1_") && !armed { return }
            upgrades += 1
            if mode == .scripted {
                if identity.hasPrefix("ota-") {
                    queue = (0..<6).map { (UInt32($0 * 512), 512) } + [(FM1Wire.finishWrite, 8)]
                } else {
                    queue = [(0, 64), (0x40, 160), (0x1000, 512), (FM1Wire.finishCheck, 8)]
                }
                next()
            } else if identity.hasPrefix("ota-") {
                fast = fastSeen
                if fast {
                    tableReads += 1
                    request(FM1Wire.tableStart + UInt32(flashOff), min(nsec, 682) * 6)
                } else {
                    queue = (0..<(nsec * 8)).map { (UInt32(flashOff + $0 * 512), 512) } + [(FM1Wire.finishWrite, 8)]
                    next()
                }
            } else {
                queue = [(0, 64), (0x40, 160), (0x1000, 512), (FM1Wire.finishCheck, 8)]
                next()
            }
        } else if let f = FM1Wire.parseFrame(d), f.cmd == FM1Wire.fastCmd, identity.hasPrefix("ota-"), mode == .realistic {
            fastSeen = supportsFast && f.body.first == 1
            if fastSeen { tx(FM1Wire.frame(FM1Wire.fastCmd, [1, 0x00, 0x10])) }
        } else if let (addr, n) = waiting {
            guard u.count >= 15 else { return }
            waiting = nil
            let fin = addr >= FM1Wire.finishCheck
            let isTable = addr >= FM1Wire.tableStart && addr < FM1Wire.tableEnd
            let data = Array(u[14..<(u.count - 1)])
            var want: [UInt8]
            if fin { want = Array("success\0".utf8) }
            else if isTable { want = FM1Wire.sectorTable(image, off: Int(addr - FM1Wire.tableStart), count: n / 6) }
            else { want = Array(image[Int(addr)..<min(image.count, Int(addr) + n)]) }
            if data != want { bad += 1 }
            served += 1
            if let k = unplugAfterServed, served >= k { connected = false; return }
            if let k = stallAfterServed, served >= k { return }
            if let p = refusePrefix, addr == 0, !fin, data.count >= p.count, Array(data[0..<p.count]) == p {
                return                                   // the running firmware's check says no: it stops asking
            }
            if mode == .scripted {
                if addr == FM1Wire.finishCheck {
                    DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { self.boot("ota-FM-1_900", "Felucca Update") }
                } else if addr == FM1Wire.finishWrite {
                    let after = afterWrite
                    DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { self.boot(after, "Felucca") }
                } else { next() }
                return
            }
            // realistic
            if isTable {
                var theirs: [Int] = []
                for k in 0..<(n / 6) {
                    let c16 = FM1Wire.crc16(flash[k]), c32 = FM1Wire.crc32(flash[k])
                    let mine = Array(want[(k * 6)..<(k * 6 + 6)])
                    let have = [UInt8(c16 & 0xFF), UInt8(c16 >> 8)] + FM1Wire.le32(c32)
                    if mine != have { theirs.append(k) }
                }
                todo = theirs
                tx(FM1Wire.frame(FM1Wire.checkCmd, [UInt8(nsec & 0xFF), UInt8(nsec >> 8), UInt8(nsec & 0xFF), UInt8(nsec >> 8)]))
                nextSector()
            } else if addr == FM1Wire.finishCheck {
                DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { self.boot("ota-FM-1_900", "Felucca Update") }
            } else if addr == FM1Wire.finishWrite {
                let after = afterWrite
                DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { self.boot(after, "Felucca") }
            } else if identity.hasPrefix("ota-") && fast {
                let k = (Int(addr) - flashOff) / 4096
                flash[k] = data
                sectorsWritten += 1
                if let c = cutAfterSectors, sectorsWritten >= c { connected = false; return }
                nextSector()
            } else if identity.hasPrefix("ota-") {
                let k = (Int(addr) - flashOff) / 4096
                let part = ((Int(addr) - flashOff) % 4096) / 512
                if part == 0 { flash[k] = [] }
                flash[k] += data
                if part == 7 { sectorsWritten += 1 }
                if let c = cutAfterSectors, sectorsWritten >= c { connected = false; return }
                next()
            } else { next() }
        }
    }

    func nextSector() {
        if todo.isEmpty { request(FM1Wire.finishWrite, 8); return }
        let k = todo.removeFirst()
        sectorsRequested += 1
        request(UInt32(flashOff + k * 4096), 4096)
    }

    func openCmd(_ cmd: UInt8, _ data: [UInt8]) {
        var out: [UInt8]
        if cmd == 49 {
            out = Array("FMC1".utf8) + [1, 7, 0, 1] + [0x00, 0x02] + FM1Wire.le32(1 << 20) + [UInt8](repeating: 0, count: 33)
        } else {
            arms.append(data)
            let ok = data.count == 38 && data[0] == 1 && data[1] == 7 && FM1Wire.rd(data, 2, 4) == image.count
            armed = ok && !refuseArm
            out = armed ? [0] : [1]
        }
        tx([0xF0] + FM1Wire.pack7([0, 0x59, cmd, UInt8(out.count), 0, 0] + out + [FM1Wire.checksum(out)]) + [0xF7])
    }

    /// RUN4 CTL's backup: BU_REQ pieces (0x42: offset32 + bytes), BU_END (0x43: n32, crc32, status, flags).
    func sendBackup() {
        guard let b = backupBytes else { return }
        var off = 0
        let limit = backupCut ? b.count / 2 : b.count
        while off < limit {
            let n = min(96, limit - off)
            tx(FM1Wire.frame(0x42, FM1Wire.le32(UInt32(off)) + Array(b[off..<(off + n)])))
            off += n
        }
        if backupCut { return }
        let crc = backupBadCRC ? FM1Wire.crc32(b) ^ 1 : FM1Wire.crc32(b)
        tx(FM1Wire.frame(0x43, FM1Wire.le32(UInt32(b.count)) + FM1Wire.le32(crc) + [0, 0]))
    }
}

/// The tests' package: after install_test.py package(): bytes i*7, the identity markers, the loader mark at 0x1800.
func testPackage(_ product: String = "FM-1_900", marker: Bool = true, size: Int = 0x2000) -> [UInt8] {
    var raw = (0..<(size + FM1Wire.blocks)).map { UInt8(truncatingIfNeeded: $0 * 7) }
    let chars = Array(product.utf8)
    for i in 0..<FM1Wire.blocks {
        raw[i * FM1Wire.blockSize + FM1Wire.keep] = i < chars.count ? UInt8(truncatingIfNeeded: Int(chars[i]) + i + 1) : 0x7D
    }
    let m = marker ? FM1Wire.loaderMark : [UInt8](repeating: 0, count: 16)
    for (i, b) in m.enumerated() { raw[0x1800 + i] = b }
    return raw
}
