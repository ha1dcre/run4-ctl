// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM1Installer.swift: install an FM-1 package (.fwsc) over USB-MIDI, and back up what a
// firmware saves.
//
// A Swift port of fm1-base's tools/fm1_install.py (the two-step install, the fast install, the
// resume of a cut-off install, M-VAVE's FM1 Open arming, the verify), tools/fm1_backup.py (the
// backup: request, receive, check, the file's layout) and tools/fm1ports.py (finding the FM-1's
// ports), following felucca-bsp's chip/ota.c.
//   fm1-base     https://github.com/charlesvestal/fm1-base   (MPL-2.0)
//   felucca-bsp  https://github.com/hugelton/felucca-bsp     (MPL-2.0)
// This file is MPL-2.0 and stays so; its source is also published with the RUN4 CTL firmware
// (https://github.com/ha1dcre/run4-ctl, tools/ios/).
//
// Blocking by design, like the Python tool: run it on a background queue. The MIDI side is behind
// FM1Link / FM1Backend (Audio/FM1MIDIBackend.swift is CoreMIDI; scripts/fm1_installer_test has a
// simulated FM-1). Differences from the tool: no HELLO probing of other gear (only ports named
// like an FM-1 or given by the caller are ever sent anything), no restore, no resource region (RUN4
// CTL has none), and a cancel that is honoured only while the running firmware is still checking
// the package, before it stages the loader (after that the update goes on, and an interrupted one
// is finished by running it again).

import Foundation

// MARK: - MIDI behind a protocol

/// One MIDI in/out pair with a SysEx queue.
protocol FM1Link: AnyObject {
    /// The port vanished (unplugged or the device restarted).
    var lost: Bool { get }
    /// One whole SysEx message (F0 ... F7). False: the send failed.
    func send(_ pkt: [UInt8]) -> Bool
    /// The next whole SysEx message, or nil after `timeout` seconds.
    func read(timeout: TimeInterval) -> [UInt8]?
    func drain()
    func close()
}

protocol FM1Backend: AnyObject {
    func inputNames() -> [String]
    func outputNames() -> [String]
    func open(input: String, output: String) throws -> FM1Link
}

/// Seconds; the tests shorten them.
struct FM1Delay {
    var start = 2.0, startFast = 0.05, fastHello = 0.5, reply = 0.01
    var loader = 3.0, reboot = 3.0, retry = 1.0, poll = 1.0
    var hs = 1.0, hsFull = 0.6, idleCheck = 8.0, idleWrite = 180.0
    var waitLoader = 30.0, waitReboot = 40.0
    /// The backup (fm1_backup.py DELAY)
    var backupPoll = 0.5, backupIdle = 10.0, backupFirst = 3.0
}

/// What the install reports as it goes.
enum FM1Step: Equatable {
    case start(String)
    case check(Int)
    case loader
    case mode(fast: Bool)
    case write(Int)
    case reboot
    case done(String)
}

private func uptime() -> Double { Double(DispatchTime.now().uptimeNanoseconds) / 1e9 }

final class FM1Device {
    let link: FM1Link
    let id: FM1Identity
    let name: String
    init(link: FM1Link, id: FM1Identity, name: String) {
        self.link = link
        self.id = id
        self.name = name
    }
}

// MARK: - Ports

enum FM1Ports {
    /// Port names taken without asking (any case, part of the name): fm1ports.py PORT_RE. Never probe other gear.
    static let usualPattern = "fm-?1|felucca|ota|composite|sinco|usb-midi|optimist|sloop|melodee|loom-?8"

    static func isUsual(_ name: String) -> Bool { FM1Text.search(name, usualPattern) }

    /// "Name 2" -> "Name" (Windows numbers each port)
    static func bare(_ name: String) -> String {
        name.replacingOccurrences(of: "\\s+[0-9]+$", with: "", options: .regularExpression)
    }

    /// An input's output: the one with its name, else the only one with its name but for a number at the end,
    /// else the only output there is.
    static func pairOutput(_ name: String, _ outs: [String]) -> String? {
        if outs.contains(name) { return name }
        let same = outs.filter { bare($0) == bare(name) }
        if same.count == 1 { return same[0] }
        return outs.count == 1 ? outs[0] : nil
    }
}

// MARK: - Updater

final class FM1Updater {
    let backend: FM1Backend
    /// Part of a port's name (the only ones asked, unless `onlyPort` is false), or nil.
    let port: String?
    /// Port names asked besides the usual ones (the RUN4 CTL link's own port).
    var knownPorts: Set<String> = []
    var delay: FM1Delay
    /// Polled while the running firmware checks the package: true stops before anything is staged.
    var isCancelled: () -> Bool = { false }

    init(backend: FM1Backend, port: String? = nil, delay: FM1Delay = FM1Delay()) {
        self.backend = backend
        self.port = port
        self.delay = delay
    }

    func isUsual(_ name: String) -> Bool { FM1Ports.isUsual(name) || knownPorts.contains(name) }

    private func dedup(_ names: [String]) -> [String] {
        var seen = Set<String>()
        return names.filter { seen.insert($0).inserted }
    }

    func candidates(onlyPort: Bool) -> [String] {
        let names = dedup(backend.inputNames())
        if let port {
            let mine = names.filter { $0.lowercased().contains(port.lowercased()) }
            if onlyPort { return mine }
            return mine + names.filter { !mine.contains($0) && isUsual($0) }
        }
        return names.filter { isUsual($0) }
    }

    // MARK: Handshake

    private enum Asked { case identity(FM1Identity), none, failed }

    private func ask(_ link: FM1Link, _ query: [UInt8], secs: Double) -> Asked {
        if !link.send(query) { return .failed }
        let end = uptime() + secs
        while true {
            let left = end - uptime()
            if left <= 0 { break }
            guard let p = link.read(timeout: left) else { break }
            if let ident = FM1Identity.parse(p) { return .identity(ident) }
        }
        return .none
    }

    /// The identity: HS_QUERY, then HS_FULL when the answer is NAME_ and three digits (a firmware on chip/ota.c
    /// tells its whole identity so); the plain answer when nothing answers HS_FULL (M-VAVE's, an older build).
    func handshake(_ link: FM1Link, tries: Int = 3) -> FM1Identity? {
        link.drain()
        for _ in 0..<tries {
            switch ask(link, FM1Wire.hsQuery, secs: delay.hs) {
            case .failed: return nil
            case .none: continue
            case .identity(let ident):
                if ident.isLoader || !FM1Text.fullMatch(ident.text, "[^_]+_[0-9]{3}") { return ident }
                if case .identity(let full) = ask(link, FM1Wire.hsFull, secs: delay.hsFull),
                   full.model == ident.model, full.text.hasPrefix(ident.text) {
                    return full
                }
                return ident
            }
        }
        return nil
    }

    /// The first matching port that answers the handshake (and want(identity)).
    func find(want: ((FM1Identity) -> Bool)? = nil, onlyPort: Bool = true) -> FM1Device? {
        let outs = dedup(backend.outputNames())
        for name in candidates(onlyPort: onlyPort) {
            guard let out = FM1Ports.pairOutput(name, outs),
                  let link = try? backend.open(input: name, output: out) else { continue }
            if let ident = handshake(link, tries: 2), want?(ident) ?? true {
                return FM1Device(link: link, id: ident, name: name)
            }
            link.close()
        }
        return nil
    }

    func waitFor(secs: Double, _ want: @escaping (FM1Identity) -> Bool) -> FM1Device? {
        let end = uptime() + secs
        while uptime() < end {
            if let dev = find(want: want, onlyPort: false) { return dev }
            Thread.sleep(forTimeInterval: delay.retry)
        }
        return nil
    }

    func notFound() -> FM1InstallError {
        let names = backend.inputNames()
        let where_ = port.map { "no MIDI port matching \($0.debugDescription) answered" } ?? "FM-1 not found"
        return FM1InstallError(.notfound, "\(where_) (USB data cable? another app using it?) MIDI inputs: "
                               + (names.isEmpty ? "(none)" : names.joined(separator: ", ")))
    }

    // MARK: Serving the device's reads

    /// Answer read requests until the device asks for `finish` -> (served, finished, lost). The loader
    /// (loader == true) may also ask 4 KiB reads and the sector table (the fast install) and say how far it
    /// compared (cmd 0x32 -> checked(k, n)).
    private func serve(_ link: FM1Link, image: [UInt8], finish: UInt32, idle: Double, loader: Bool, fast: Bool,
                       progress: (Int, UInt32, Int) -> Void,
                       checked: ((Int, Int) -> Void)?) throws -> (served: Int, finished: Bool, lost: Bool) {
        var served = 0
        var last = uptime()
        while true {
            // Cancel: only while the running firmware is checking, before it is told "success".
            if finish == FM1Wire.finishCheck && isCancelled() {
                throw FM1InstallError(.cancelled, "cancelled; nothing was written")
            }
            guard let pkt = link.read(timeout: delay.poll) else {
                if link.lost { return (served, false, true) }
                if uptime() - last > idle { return (served, false, false) }
                continue
            }
            guard let r = FM1Wire.parseRequest(pkt) else {
                if loader, let checked, let f = FM1Wire.parseFrame(pkt), f.cmd == FM1Wire.checkCmd, f.body.count == 4 {
                    last = uptime()
                    checked(FM1Wire.rd(f.body, 0, 2), FM1Wire.rd(f.body, 2, 2))
                }
                continue
            }
            last = uptime()
            let addr = r.addr, n = r.length
            if addr == FM1Wire.finishCheck || addr == FM1Wire.finishWrite {
                _ = link.send(FM1Wire.response(addr: addr, data: Array("success\0".utf8)))
                if addr == finish { return (served, true, false) }
                continue
            }
            let data: [UInt8]
            if loader && addr >= FM1Wire.tableStart && addr < FM1Wire.tableEnd {
                let off = Int(addr - FM1Wire.tableStart)
                let k = n / 6
                if n == 0 || n % 6 != 0 || n > FM1Wire.fastChunk || off + k * FM1Wire.fastChunk > image.count {
                    throw FM1InstallError(.badreq, "the device asked for the table at \(String(off, radix: 16)) for \(n) B, outside the package")
                }
                data = FM1Wire.sectorTable(image, off: off, count: k)
            } else {
                if n > (loader ? FM1Wire.fastChunk : FM1Wire.maxData) || Int(addr) + n > image.count {
                    throw FM1InstallError(.badreq, "the device asked for \(String(addr, radix: 16))+\(n), outside the package")
                }
                data = Array(image[Int(addr)..<(Int(addr) + n)])
            }
            if !fast { Thread.sleep(forTimeInterval: delay.reply) }
            if !link.send(FM1Wire.response(addr: addr, data: data, flags: r.flags)) { return (served, false, true) }
            served += 1
            progress(served, addr, n)
        }
    }

    // MARK: Steps

    /// Step 1: the running firmware checks the package and starts the loader.
    func check(_ dev: FM1Device, image: [UInt8], step: @escaping (FM1Step) -> Void) throws -> FM1Device {
        step(.start(dev.id.text))
        _ = dev.link.send(FM1Wire.upgrade)
        Thread.sleep(forTimeInterval: delay.start)
        let res: (served: Int, finished: Bool, lost: Bool)
        do {
            res = try serve(dev.link, image: image, finish: FM1Wire.finishCheck, idle: delay.idleCheck, loader: false,
                            fast: false, progress: { k, _, _ in step(.check(k)) }, checked: nil)
        } catch {
            dev.link.close()
            throw error
        }
        dev.link.close()
        guard res.finished else {
            throw FM1InstallError(res.lost ? .lost : .stopped,
                                  "the FM-1 \(res.lost ? "was disconnected" : "stopped answering") after \(res.served) requests; nothing was written")
        }
        step(.loader)
        Thread.sleep(forTimeInterval: delay.loader)
        guard let ota = waitFor(secs: delay.waitLoader, { $0.isLoader }) else {
            throw FM1InstallError(.noloader, "the update loader did not appear. Replug the USB cable and run the install again: the FM-1 stays in update mode until it is done")
        }
        return ota
    }

    /// The fast install's word: true when the loader answers it within delay.fastHello (it holds for the next
    /// session); an older loader, or one still in a session it kept, says nothing.
    func fastHello(_ link: FM1Link) -> Bool {
        link.drain()
        if !link.send(FM1Wire.fastHello) { return false }
        let end = uptime() + delay.fastHello
        while true {
            let left = end - uptime()
            if left <= 0 { break }
            guard let p = link.read(timeout: left) else { break }
            if let f = FM1Wire.parseFrame(p), f.cmd == FM1Wire.fastCmd, f.body.count == 3, f.body[0] == 1,
               FM1Wire.rd(f.body, 1, 2) >= FM1Wire.fastChunk {
                return true
            }
        }
        return false
    }

    /// Step 2: the loader reads and writes the package (the fast install when it says yes), then restarts.
    func write(_ ota: FM1Device, image: [UInt8], step: @escaping (FM1Step) -> Void, classic: Bool) throws {
        step(.write(0))
        let fast = !classic && fastHello(ota.link)
        step(.mode(fast: fast))
        var barPct = 0
        var barOff: Int?
        var barN = 0

        func show(_ pct: Int) {                                  // the bar never goes back
            if pct > barPct {
                barPct = pct
                step(.write(pct))
            }
        }
        let progress: (Int, UInt32, Int) -> Void = { _, addr, n in
            if addr >= FM1Wire.tableStart && addr < FM1Wire.tableEnd {      // the table: flash.bin's offset and its sectors
                barOff = Int(addr - FM1Wire.tableStart)
                barN = n / 6
            } else if let off = barOff {                                     // a sector of the fast install
                if barN > 0 && Int(addr) >= off {
                    show(min(99, ((Int(addr) - off) / FM1Wire.fastChunk + 1) * 100 / barN))
                }
            } else {                                                         // the classic install: its two passes, as they come
                step(.write(min(99, (Int(addr) + n) * 100 / image.count)))
            }
        }
        let checked: (Int, Int) -> Void = { k, n in             // cmd 0x32: the app area's sectors compared, kept or written
            if n > 0 { show(min(99, k * 100 / n)) }
        }
        _ = ota.link.send(FM1Wire.upgrade)
        Thread.sleep(forTimeInterval: fast ? delay.startFast : delay.start)
        let res: (served: Int, finished: Bool, lost: Bool)
        do {
            res = try serve(ota.link, image: image, finish: FM1Wire.finishWrite, idle: delay.idleWrite, loader: true,
                            fast: fast, progress: progress, checked: checked)
        } catch {
            ota.link.close()
            throw error
        }
        ota.link.close()
        guard res.finished else {
            throw FM1InstallError(res.lost ? .lost : .stopped,
                                  "the loader \(res.lost ? "was disconnected" : "stopped answering") after \(res.served) requests. Replug the USB cable and run the install again to finish")
        }
        step(.write(100))
    }

    /// After the write: the FM-1 restarts and its installed identity is checked -> the identity it reports.
    func verify(product: String, step: @escaping (FM1Step) -> Void) throws -> String {
        step(.reboot)
        Thread.sleep(forTimeInterval: delay.reboot)
        guard var back = waitFor(secs: delay.waitReboot, { !$0.isLoader }) else {
            throw FM1InstallError(.noreturn, "the FM-1 did not come back: power-cycle it")
        }
        back.link.close()
        for _ in 0..<3 {                       // (the short answer, NAME_ddd, while the firmware still starts: it tells its whole one a moment later)
            if back.id.text == product
                || !(FM1Text.fullMatch(back.id.text, "[^_]+_[0-9]{3}") && product.hasPrefix(back.id.text)) { break }
            Thread.sleep(forTimeInterval: 1.0)
            guard let again = waitFor(secs: delay.waitReboot, { !$0.isLoader }) else { break }
            again.link.close()
            back = again
        }
        if back.id.text != product
            && !(FM1Text.fullMatch(back.id.text, "[^_]+_[0-9]{3}") && product.hasPrefix(back.id.text)) {
            throw FM1InstallError(.mismatch, "written, but the FM-1 reports \(back.id.text) (expected \(product))")
        }
        step(.done(back.id.text))
        return back.id.text
    }

    /// M-VAVE's Open firmware: allow the update of M-VAVE's own firmware.
    private func query(_ link: FM1Link, _ cmd: UInt8, _ payload: [UInt8] = []) -> [UInt8]? {
        link.drain()
        if !link.send(FM1Wire.frame(cmd, payload)) { return nil }
        let end = uptime() + delay.hs * 1.5
        while true {
            let left = end - uptime()
            if left <= 0 { break }
            guard let p = link.read(timeout: left) else { break }
            guard p.count >= 2, p.first == 0xF0, p.last == 0xF7 else { continue }
            let u = FM1Wire.unpack7(p[1..<(p.count - 1)])
            var sum: UInt32 = 0
            for b in u.dropFirst(6) { sum &+= UInt32(b) }
            if u.count >= 7, Array(u[0..<3]) == [0, 0x59, cmd], sum & 0xFF == 0xFF {
                let n = FM1Wire.rd(u, 3, 2)
                return Array(u[6..<min(u.count, 6 + n)])
            }
        }
        return nil
    }

    func arm(_ dev: FM1Device, package: FM1Package) throws {
        guard FM1Package.isStock(package.product) else {
            dev.link.close()
            throw FM1InstallError(.open, "the FM-1 runs M-VAVE's \(dev.id.text) firmware: put the stock firmware back first, then install this")
        }
        guard let cap = query(dev.link, 49), cap.count == 47, Array(cap[0..<4]) == Array("FMC1".utf8), cap[6] == 0 else {
            dev.link.close()
            throw FM1InstallError(.stopped, "\(dev.id.text) did not accept the update; nothing was written")
        }
        let ack = query(dev.link, 50, [1, cap[5]] + FM1Wire.le32(UInt32(package.image.count)) + package.digest)
        guard ack == [0] else {
            dev.link.close()
            throw FM1InstallError(.stopped, "\(dev.id.text) refused the package; nothing was written")
        }
    }

    static func model(of text: String) -> String {
        var m = text.components(separatedBy: "_").first ?? text
        if m.hasPrefix("ota-") { m.removeFirst(4) }
        return m
    }

    /// Running firmware -> loader -> new firmware; `dev` from find(). A device already in update mode only
    /// needs the write. The fast install's hello goes only to a loader of this family (the package's: its
    /// marker); M-VAVE's loader, which its own firmware brings, never hears it.
    func install(_ dev: FM1Device, package: FM1Package, step: @escaping (FM1Step) -> Void = { _ in }) throws -> String {
        if Self.model(of: dev.id.text) != Self.model(of: package.product) && !dev.id.isOpen {
            dev.link.close()
            throw FM1InstallError(.model, "the device is \(dev.id.text), the package is for \(package.product)")
        }
        if dev.id.isOpen { try arm(dev, package: package) }
        let ota = dev.id.isLoader ? dev : try check(dev, image: package.image, step: step)
        try write(ota, image: package.image, step: step, classic: !FM1Package.containsLoaderMark(package.image))
        return try verify(product: package.product, step: step)
    }
}

// MARK: - Backup

struct FM1BackupKind {
    /// 4 bytes (APP_BACKUP_MAGIC), nil: any (taken from the file).
    var magic: [UInt8]?
    var version: Int?
    var objs: [Int]?
    var names: (Int) -> String = { "object \($0)" }
    /// The package identity's prefix (fm1.toml id_prefix), nil: any FM-1.
    var identity: String?
    var name: String?

    var who: String { name ?? "the FM-1" }
    var named: Bool { name != nil }
    var printable: String { name ?? "the firmware" }

    func summary(_ objs: [(Int, [UInt8])]) -> String {
        let used = objs.filter { !$0.1.isEmpty }.count
        return "\(objs.reduce(0) { $0 + $1.1.count }) bytes: \(used) of \(objs.count) parts hold something"
    }
}

struct FM1BackupError: Error, CustomStringConvertible {
    let message: String
    init(_ m: String) { message = m }
    var description: String { message }
}

enum FM1Backup {
    static let reqCmd: UInt8 = 0x42, endCmd: UInt8 = 0x43
    static let head = 16, rec = 8, payloadMax = 3840

    /// Firmwares whose magic need not be given: an identity prefix -> (magic, name) (fm1_backup.py KNOWN).
    static let known: [(String, [UInt8], String)] = [("FM-1_3", Array("PLYB".utf8), "Polly")]

    static func knownFirmware(_ identity: String) -> (magic: [UInt8], name: String)? {
        for (prefix, magic, name) in known where identity.hasPrefix(prefix) { return (magic, name) }
        return nil
    }

    private static func printable(_ b: ArraySlice<UInt8>) -> Bool { b.allSatisfy { $0 > 0x20 && $0 < 0x7F } }

    /// Checks a backup file whole -> (flags, [(obj, payload)]).
    static func parse(_ image: [UInt8], kind: FM1BackupKind) throws -> (flags: UInt32, objs: [(Int, [UInt8])]) {
        let what = kind.magic != nil ? "a \(kind.printable) backup" : "a backup"
        guard image.count >= head + 4 else { throw FM1BackupError("too short for \(what)") }
        let magic = Array(image[0..<4])
        let ver = FM1Wire.rd(image, 4, 2), n = FM1Wire.rd(image, 6, 2)
        let flags = UInt32(FM1Wire.rd(image, 8, 4)), hcrc = UInt32(FM1Wire.rd(image, 12, 4))
        if let m = kind.magic, magic != m { throw FM1BackupError("not \(what)") }
        guard printable(magic[...]), hcrc == FM1Wire.crc32(image[0..<12]) else {
            throw FM1BackupError(printable(magic[...]) ? "its head is damaged" : "not a backup")
        }
        if let v = kind.version, ver != v {
            throw FM1BackupError("a backup of another version of \(kind.printable) (version \(ver); this one makes version \(v)): it cannot be restored")
        }
        if let o = kind.objs, n != o.count { throw FM1BackupError("a backup of another version (\(ver), \(n) parts)") }
        guard image.count >= head + n * rec + 4 else { throw FM1BackupError("cut short") }
        var objs: [(Int, [UInt8])] = []
        var pos = head
        for i in 0..<n {
            let want = kind.objs?[i]
            guard pos + rec <= image.count else { throw FM1BackupError("cut short") }
            let obj = FM1Wire.rd(image, pos, 2), ln = FM1Wire.rd(image, pos + 2, 2)
            let crc = UInt32(FM1Wire.rd(image, pos + 4, 4))
            pos += rec
            let part = kind.names(want ?? obj)
            if (want != nil && obj != want) || ln > payloadMax || pos + ln + 4 > image.count {
                throw FM1BackupError("part \(part): a wrong header")
            }
            let data = Array(image[pos..<(pos + ln)])
            if FM1Wire.crc32(data) != crc { throw FM1BackupError("part \(part): damaged (CRC-32)") }
            objs.append((obj, data))
            pos += ln
        }
        guard pos + 4 == image.count, UInt32(FM1Wire.rd(image, pos, 4)) == FM1Wire.crc32(image[0..<pos]) else {
            throw FM1BackupError("damaged (the whole file's CRC-32)")
        }
        return (flags, objs)
    }

    /// A file -> (the backup itself, the firmware's sections after the backup's own CRC-32).
    static func split(_ data: [UInt8]) throws -> ([UInt8], [(String, [UInt8])]) {
        var pos = head
        if data.count >= head {
            let n = FM1Wire.rd(data, 6, 2)
            for _ in 0..<n {
                if pos + rec > data.count { break }
                pos += rec + FM1Wire.rd(data, pos + 2, 2)
            }
        }
        pos += 4
        if pos >= data.count { return (data, []) }
        var out: [(String, [UInt8])] = []
        var at = pos
        while at < data.count {
            guard at + 8 <= data.count else { throw FM1BackupError("a section after the backup is cut short") }
            let tag = String(data[at..<(at + 4)].map { Character(UnicodeScalar($0)) })
            let ln = FM1Wire.rd(data, at + 4, 4)
            guard at + 8 + ln + 4 <= data.count else { throw FM1BackupError("its section \(tag) is cut short") }
            let body = Array(data[(at + 8)..<(at + 8 + ln)])
            guard UInt32(FM1Wire.rd(data, at + 8 + ln, 4)) == FM1Wire.crc32(body) else {
                throw FM1BackupError("its section \(tag) is damaged (CRC-32)")
            }
            out.append((tag, body))
            at += 8 + ln + 4
        }
        return (Array(data[0..<pos]), out)
    }

    /// A backup file from [(obj, payload)] in the app's order (the tests' simulated FM-1).
    static func build(objs: [(Int, [UInt8])], flags: UInt32 = 0, magic: [UInt8], version: Int) -> [UInt8] {
        var out = magic + [UInt8(version & 0xFF), UInt8(version >> 8), UInt8(objs.count & 0xFF), UInt8(objs.count >> 8)]
            + FM1Wire.le32(flags)
        out += FM1Wire.le32(FM1Wire.crc32(out))
        for (obj, data) in objs {
            out += [UInt8(obj & 0xFF), UInt8(obj >> 8), UInt8(data.count & 0xFF), UInt8(data.count >> 8)]
            out += FM1Wire.le32(FM1Wire.crc32(data)) + data
        }
        out += FM1Wire.le32(FM1Wire.crc32(out))
        return out
    }

    /// The device's backup frames until its end frame -> the image (checked).
    static func receive(_ link: FM1Link, kind: FM1BackupKind, delay: FM1Delay,
                        progress: (Int) -> Void) throws -> [UInt8] {
        var got: [UInt8] = []
        var last = uptime()
        var first = true
        while true {
            guard let pkt = link.read(timeout: delay.backupPoll) else {
                if link.lost { throw FM1InstallError(.lost, "the FM-1 was disconnected: run this again") }
                if uptime() - last > (first ? delay.backupFirst : delay.backupIdle) {
                    throw FM1InstallError(.stopped, first
                        ? "the FM-1 does not answer (" + (kind.named ? "an older \(kind.printable), without backup?)"
                                                                      : "a firmware without backup, or a magic that is not its app's?)")
                        : "the FM-1 stopped sending")
                }
                continue
            }
            guard let f = FM1Wire.parseFrame(pkt) else { continue }
            last = uptime()
            if f.cmd == reqCmd && f.body.count >= 4 {
                first = false
                let off = FM1Wire.rd(f.body, 0, 4)
                if off != got.count {
                    throw FM1InstallError(.lost, "a piece of the backup went missing at \(got.count): run this again")
                }
                got += f.body[4...]
                progress(got.count)
            } else if f.cmd == endCmd && f.body.count >= 10 {
                let n = FM1Wire.rd(f.body, 0, 4), crc = UInt32(FM1Wire.rd(f.body, 4, 4)), status = f.body[8]
                if status != 0 { throw FM1InstallError(.badreq, "the FM-1 could not read its flash (status \(status))") }
                if n != got.count || crc != FM1Wire.crc32(got) {
                    throw FM1InstallError(.lost, "the backup arrived damaged: run this again")
                }
                return got
            }
        }
    }

    struct Result {
        let image: [UInt8]
        let flags: UInt32
        let summary: String
        let identity: String
        /// The FM-1 was playing, so what it had not saved yet is not in it.
        var tookWhilePlaying: Bool { flags & 1 != 0 }
    }

    /// Asks the FM-1 for its backup (the firmware must have been built with backup = true: RUN4 CTL is).
    static func backup(_ up: FM1Updater, kind: FM1BackupKind, magic: [UInt8]? = nil,
                       progress: (Int) -> Void = { _ in }) throws -> Result {
        guard let dev = up.find(onlyPort: up.port != nil) else { throw up.notFound() }
        if let id = kind.identity, !dev.id.text.hasPrefix(id) {
            dev.link.close()
            throw FM1InstallError(.model, "the FM-1 runs \(dev.id.text), not \(kind.printable) (\(id)xx)")
        }
        guard let magic = magic ?? kind.magic ?? knownFirmware(dev.id.text)?.magic else {
            dev.link.close()
            throw FM1InstallError(.usage, "\(dev.id.text): the firmware's backup magic is not known")
        }
        let image: [UInt8]
        dev.link.drain()
        _ = dev.link.send(FM1Wire.frame(reqCmd, magic))
        do {
            image = try receive(dev.link, kind: kind, delay: up.delay, progress: progress)
        } catch {
            dev.link.close()
            throw error
        }
        dev.link.close()
        do {
            let (flags, objs) = try parse(image, kind: kind)
            guard Array(image[0..<4]) == magic else { throw FM1BackupError("another magic") }
            return Result(image: image, flags: flags, summary: kind.summary(objs), identity: dev.id.text)
        } catch {
            throw FM1InstallError(.lost, "the backup arrived damaged (\(error)): run this again")
        }
    }
}
