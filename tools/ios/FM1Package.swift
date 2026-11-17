// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM1Package.swift: the FM-1's update wire format and the .fwsc package.
//
// A Swift port of parts of fm1-base's tools/fm1_install.py (wire format: pack7, the
// M-UPGRADE framing, read requests and responses, the identity handshake, the package's
// identity and logical image) and tools/fm1pkg_make.py (the package format: one marker
// byte after each of the first 20 blocks), following felucca-bsp's chip/ota.c.
//   fm1-base     https://github.com/charlesvestal/fm1-base   (MPL-2.0)
//   felucca-bsp  https://github.com/hugelton/felucca-bsp     (MPL-2.0)
// This file is MPL-2.0 and stays so; its source is also published with the RUN4 CTL
// firmware (https://github.com/ha1dcre/run4-ctl, tools/ios/). Pure Foundation and
// CryptoKit: scripts/fm1_installer_test runs it on the Mac.

import Foundation
import CryptoKit

// MARK: - Wire format

enum FM1Wire {
    /// HS_QUERY: the plain handshake. A firmware on felucca-bsp's chip/ota.c answers its identity.
    static let hsQuery: [UInt8] = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7]
    /// The handshake with the one-byte body 01 (00 59 11, length 1, 01, checksum FE): a firmware on
    /// chip/ota.c answers with its whole identity ("FM-1_6010000"). M-VAVE's firmware and the update
    /// loaders do not answer it.
    static let hsFull: [UInt8] = [0xF0, 0x00, 0x32, 0x45, 0x08, 0x00, 0x00, 0x40, 0x00, 0x7E, 0x01, 0xF7]
    static let upgrade: [UInt8] = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7]
    static let finishCheck: UInt32 = 0xE000_0000
    static let finishWrite: UInt32 = 0xF000_0000
    static let maxData = 512
    /// The fast install (loader/ldr_core.c): hello, progress, 4 KiB sector reads, the sector table.
    static let fastCmd: UInt8 = 0x31
    static let checkCmd: UInt8 = 0x32
    static let fastChunk = 4096
    static let tableStart: UInt32 = 0xD000_0000
    static let tableEnd: UInt32 = 0xE000_0000
    static let blocks = 20
    static let blockSize = 0x30
    static let keep = 0x2F
    static let loaderMark = Array("FELUCCA-LOADER-1".utf8)

    /// Seven bits per byte, the lowest first.
    static func pack7(_ data: [UInt8]) -> [UInt8] {
        var out: [UInt8] = []
        var acc: UInt32 = 0
        var nb = 0
        for b in data {
            acc |= UInt32(b) << UInt32(nb)
            nb += 8
            while nb >= 7 {
                out.append(UInt8(acc & 0x7F))
                acc >>= 7
                nb -= 7
            }
        }
        if nb > 0 { out.append(UInt8(acc & 0x7F)) }
        return out
    }

    static func unpack7<S: Sequence>(_ s: S) -> [UInt8] where S.Element == UInt8 {
        var out: [UInt8] = []
        var acc: UInt32 = 0
        var nb = 0
        for b in s {
            acc |= UInt32(b & 0x7F) << UInt32(nb)
            nb += 7
            while nb >= 8 {
                out.append(UInt8(acc & 0xFF))
                acc >>= 8
                nb -= 8
            }
        }
        return out
    }

    /// ~sum(bytes) & 0xFF
    static func checksum<S: Sequence>(_ bytes: S) -> UInt8 where S.Element == UInt8 {
        var sum: UInt32 = 0
        for b in bytes { sum &+= UInt32(b) }
        return ~UInt8(truncatingIfNeeded: sum)
    }

    static func le32(_ v: UInt32) -> [UInt8] {
        [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF), UInt8((v >> 16) & 0xFF), UInt8(v >> 24)]
    }

    static func rd(_ b: [UInt8], _ at: Int, _ n: Int) -> Int {
        var v = 0
        for i in 0..<n { v |= Int(b[at + i]) << (8 * i) }
        return v
    }

    /// The device's read response: F0 pack7(00 59 30 len24 00 fl addr32 len24 data chk) F7
    static func response(addr: UInt32, data: [UInt8], flags: UInt8 = 0) -> [UInt8] {
        let n = data.count
        var body: [UInt8] = [0x00, 0x59, 0x30, UInt8((n + 8) & 0xFF), UInt8(((n + 8) >> 8) & 0xFF), 0, flags]
        body += le32(addr)
        body += [UInt8(n & 0xFF), UInt8((n >> 8) & 0xFF), 0]
        body += data
        body.append(checksum(body[6...]))
        return [0xF0] + pack7(body) + [0xF7]
    }

    /// A device read request -> (flags, addr, length), or nil.
    static func parseRequest(_ pkt: [UInt8]) -> (flags: UInt8, addr: UInt32, length: Int)? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let u = unpack7(pkt[1..<(pkt.count - 1)])
        guard u.count == 15, Array(u[0..<3]) == [0x00, 0x59, 0x30], checksum(u[6..<14]) == u[14] else { return nil }
        return (u[6], UInt32(rd(u, 7, 4)), rd(u, 11, 3))
    }

    /// An M-UPGRADE command: F0 pack7(00 59 cmd len16 00 payload ~sum) F7
    static func frame(_ cmd: UInt8, _ payload: [UInt8] = []) -> [UInt8] {
        let n = payload.count
        var body: [UInt8] = [0, 0x59, cmd, UInt8(n & 0xFF), UInt8(n >> 8), 0]
        body += payload
        body.append(checksum(payload))
        return [0xF0] + pack7(body) + [0xF7]
    }

    /// (cmd, body) of any frame in the update protocol's framing, checksum checked.
    static func parseFrame(_ pkt: [UInt8]) -> (cmd: UInt8, body: [UInt8])? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let u = unpack7(pkt[1..<(pkt.count - 1)])
        guard u.count >= 7, u[0] == 0x00, u[1] == 0x59 else { return nil }
        let n = rd(u, 3, 3)
        guard u.count == 7 + n, checksum(u[6..<(6 + n)]) == u[6 + n] else { return nil }
        return (u[2], Array(u[6..<(6 + n)]))
    }

    /// The fast install's word: cmd 0x31 [version 1, read size 16-bit LE].
    static let fastHello = frame(fastCmd, [1, UInt8(fastChunk & 0xFF), UInt8(fastChunk >> 8)])

    // MARK: Checksums

    /// The UFW's CRC16 (XMODEM: poly 0x1021, init 0), as chip/ota.c ota_crc16.
    static func crc16<S: Sequence>(_ data: S, _ start: UInt16 = 0) -> UInt16 where S.Element == UInt8 {
        var c = start
        for b in data {
            c ^= UInt16(b) << 8
            for _ in 0..<8 { c = (c & 0x8000) != 0 ? (c << 1) ^ 0x1021 : c << 1 }
        }
        return c
    }

    private static let crc32Table: [UInt32] = (0..<256).map { i -> UInt32 in
        var c = UInt32(i)
        for _ in 0..<8 { c = (c & 1) != 0 ? 0xEDB8_8320 ^ (c >> 1) : c >> 1 }
        return c
    }

    /// zlib's CRC-32.
    static func crc32<S: Sequence>(_ data: S, _ start: UInt32 = 0) -> UInt32 where S.Element == UInt8 {
        var c = ~start
        for b in data { c = crc32Table[Int((c ^ UInt32(b)) & 0xFF)] ^ (c >> 8) }
        return ~c
    }

    /// The fast install's table: n entries, one per 4 KiB sector of the image from off, [CRC16, CRC-32] LE.
    static func sectorTable(_ image: [UInt8], off: Int, count n: Int) -> [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(n * 6)
        for k in 0..<n {
            let a = min(off + k * fastChunk, image.count)
            let b = min(off + (k + 1) * fastChunk, image.count)
            let sec = image[a..<b]
            let c16 = crc16(sec)
            let c32 = crc32(sec)
            out += [UInt8(c16 & 0xFF), UInt8(c16 >> 8)] + le32(c32)
        }
        return out
    }
}

// MARK: - Identity

/// What an FM-1 answers to the handshake (cmd 0x11, 27 bytes: the text, then zeros).
struct FM1Identity: Equatable {
    var text: String
    var model: String
    var version: Int
    /// The flash's JEDEC ID from bytes 24..26 (felucca-bsp#29), 0: not sent.
    var flash: Int = 0

    /// M-VAVE's FM1 Open firmware (V10, 2026-10).
    var isOpen: Bool { FM1Text.fullMatch(text, "FM1_(OFFICIAL_OPEN|COMMUNITY_[0-9]+)") }
    /// The update loader (ota-FM-1_ddd): an install that was not finished.
    var isLoader: Bool { model.lowercased().hasPrefix("ota-") }

    static func parse(_ pkt: [UInt8]) -> FM1Identity? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let d = FM1Wire.unpack7(pkt[1..<(pkt.count - 1)])
        guard d.count == 34, Array(d[0..<3]) == [0x00, 0x59, 0x11] else { return nil }
        // the text up to its first zero (bytes after it may carry data)
        let raw = d[6..<33]
        let end = raw.firstIndex(of: 0) ?? raw.endIndex
        let latin = String(raw[raw.startIndex..<end].map { Character(UnicodeScalar($0)) })   // Latin-1
        let flash = latin.count < 24 ? (Int(d[30]) << 16 | Int(d[31]) << 8 | Int(d[32])) : 0
        if FM1Text.fullMatch(latin, "FM1_(OFFICIAL_OPEN|COMMUNITY_[0-9]+)") {
            return FM1Identity(text: latin, model: "FM-1", version: 0, flash: flash)
        }
        guard let m = FM1Text.captures(latin, "([^_]+)_([0-9]+)"), m.count == 2 else { return nil }
        return FM1Identity(text: latin, model: m[0], version: Int(m[1]) ?? 0, flash: flash)
    }

    static let flashParts: [Int: String] = [0x856014: "Puya P25Q80", 0xB36014: "UC25HQ80"]
}

enum FM1Text {
    static func fullMatch(_ s: String, _ pattern: String) -> Bool {
        s.range(of: "^(?:" + pattern + ")$", options: .regularExpression) != nil
    }

    /// The capture groups of a full match, or nil.
    static func captures(_ s: String, _ pattern: String) -> [String]? {
        guard let re = try? NSRegularExpression(pattern: "^(?:" + pattern + ")$") else { return nil }
        let ns = s as NSString
        guard let m = re.firstMatch(in: s, range: NSRange(location: 0, length: ns.length)) else { return nil }
        return (1..<m.numberOfRanges).map { ns.substring(with: m.range(at: $0)) }
    }

    static func search(_ s: String, _ pattern: String) -> Bool {
        s.range(of: pattern, options: [.regularExpression, .caseInsensitive]) != nil
    }
}

// MARK: - Package

/// A Felucca package (.fwsc): the image the device reads during an update, with 20 marker bytes
/// (the package identity) after the first 20 blocks of 0x2F bytes.
struct FM1Package {
    let product: String
    /// The logical image: the package without the 20 marker bytes.
    let image: [UInt8]
    /// SHA-256 of the whole file (M-VAVE's Open firmware arms its updater with it).
    let digest: [UInt8]
    let hasLoaderMark: Bool

    /// M-VAVE's FM-1 firmware, any version: FM-1_0 and two digits (FM-1_016; fm1-base's dev builds are FM-1_x00,
    /// its releases longer); m-bootloader's installer is FM-1_099. Whether a package is safe to write is the
    /// FM-1's to check (felucca-bsp chip/ota.c), not a list of files.
    static func isStock(_ product: String) -> Bool {
        FM1Text.fullMatch(product, "FM-1_0[0-9][0-9]") && product != "FM-1_099"
    }

    /// The package identity ("FM-1_906"): one marker byte after each of the first 20 blocks.
    static func product(of raw: [UInt8]) throws -> String {
        guard raw.count >= FM1Wire.blocks * FM1Wire.blockSize else {
            throw FM1InstallError(.badpkg, "not an FM-1 package (too short)")
        }
        var s = ""
        for i in 0..<FM1Wire.blocks {
            let m = raw[i * FM1Wire.blockSize + FM1Wire.keep]
            if m != 0x7D { s.append(Character(UnicodeScalar((m &- UInt8(i) &- 1) & 0xFF))) }
        }
        return s
    }

    static func logicalImage(_ raw: [UInt8]) -> [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(raw.count)
        for i in 0..<FM1Wire.blocks {
            out += raw[(i * FM1Wire.blockSize)..<(i * FM1Wire.blockSize + FM1Wire.keep)]
        }
        out += raw[(FM1Wire.blocks * FM1Wire.blockSize)...]
        return out
    }

    /// A package from a logical image and an identity (fm1pkg_make.py ufw's last step; the tests' packages).
    static func make(product: String, logical: [UInt8]) -> [UInt8]? {
        let chars = Array(product.unicodeScalars)
        guard chars.count <= FM1Wire.blocks, logical.count >= FM1Wire.blocks * FM1Wire.keep else { return nil }
        var raw: [UInt8] = []
        for i in 0..<FM1Wire.blocks {
            let m: UInt8 = i < chars.count ? UInt8(truncatingIfNeeded: Int(chars[i].value) + i + 1) : 0x7D
            if i < chars.count && m == 0x7D { return nil }
            raw += logical[(i * FM1Wire.keep)..<((i + 1) * FM1Wire.keep)]
            raw.append(m)
        }
        raw += logical[(FM1Wire.blocks * FM1Wire.keep)...]
        return raw
    }

    /// Reads a package file's bytes. A package without a Felucca loader that is not M-VAVE's firmware is
    /// refused unless `force` (the FM-1 still checks it before writing).
    static func load(_ raw: [UInt8], force: Bool = false) throws -> FM1Package {
        let product = try product(of: raw)
        guard FM1Text.fullMatch(product, "[^_]+_[0-9]+") else {
            throw FM1InstallError(.badpkg, "not an FM-1 package (identity \(product.debugDescription))")
        }
        let digest = Array(SHA256.hash(data: raw))
        let image = logicalImage(raw)
        let mark = containsLoaderMark(raw)
        if !mark && !force && !isStock(product) {
            throw FM1InstallError(.badpkg, "no Felucca update loader in this package and not M-VAVE's firmware")
        }
        return FM1Package(product: product, image: image, digest: digest, hasLoaderMark: mark)
    }

    static func containsLoaderMark(_ bytes: [UInt8]) -> Bool {
        let m = FM1Wire.loaderMark
        guard bytes.count >= m.count else { return false }
        let first = m[0]
        var i = 0
        let last = bytes.count - m.count
        while i <= last {
            if bytes[i] == first, Array(bytes[i..<(i + m.count)]) == m { return true }
            i += 1
        }
        return false
    }

    static func sha256Hex(_ bytes: [UInt8]) -> String {
        SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
    }
}

// MARK: - Errors

struct FM1InstallError: Error, CustomStringConvertible {
    enum Code: Equatable {
        case usage, badpkg, notfound, model, open, lost, stopped, badreq, noloader, noreturn, mismatch, cancelled
        /// RUN4's own (the package's download and pinned checksum; FM1Flow)
        case download, checksum, refused
    }
    let code: Code
    let message: String
    init(_ code: Code, _ message: String) {
        self.code = code
        self.message = message
    }
    var description: String { message }
}
