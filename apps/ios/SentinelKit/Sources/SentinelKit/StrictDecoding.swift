import Foundation

/// Reasons a frame is rejected. Messages are fixed strings and never echo the
/// rejected payload, mirroring the firmware validator.
public enum ProtocolDecodingError: Error, Equatable, Sendable, CustomStringConvertible {
    case tooLarge(Int)
    case duplicateKey(String)
    case unsupportedVersion(String)
    case unknownField(String)
    case invalidField(String)
    case simulatedData

    public var description: String {
        switch self {
        case .tooLarge(let size): return "frame too large (\(size) bytes)"
        case .duplicateKey(let key): return "duplicate key '\(key)'"
        case .unsupportedVersion(let version): return "unsupported protocol version \(version)"
        case .unknownField(let field): return "unknown field '\(field)'"
        case .invalidField(let field): return "invalid field '\(field)'"
        case .simulatedData: return "simulated data is not accepted"
        }
    }
}

struct DynamicKey: CodingKey {
    var stringValue: String
    var intValue: Int? { nil }
    init(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { nil }
}

extension Decoder {
    /// Enforces `additionalProperties: false` from the JSON Schemas.
    func rejectUnknownKeys(allowed: Set<String>) throws {
        let keyed = try self.container(keyedBy: DynamicKey.self)
        if let extra = keyed.allKeys.map(\.stringValue).sorted().first(where: { !allowed.contains($0) }) {
            throw ProtocolDecodingError.unknownField(extra)
        }
    }
}

enum Patterns {
    private static func matches(_ text: String, first: (UInt8) -> Bool, rest: (UInt8) -> Bool) -> Bool {
        let bytes = Array(text.utf8)
        guard (1...64).contains(bytes.count), first(bytes[0]) else { return false }
        return bytes.dropFirst().allSatisfy(rest)
    }

    private static func lowerOrDigit(_ b: UInt8) -> Bool { (b >= 0x61 && b <= 0x7A) || (b >= 0x30 && b <= 0x39) }
    private static func alnum(_ b: UInt8) -> Bool { lowerOrDigit(b) || (b >= 0x41 && b <= 0x5A) }
    private static func punctuation(_ b: UInt8) -> Bool { b == 0x2E || b == 0x5F || b == 0x3A || b == 0x2D }

    /// `^[a-z0-9][a-z0-9._:-]{0,63}$`
    static func isSource(_ text: String) -> Bool {
        matches(text, first: lowerOrDigit, rest: { lowerOrDigit($0) || punctuation($0) })
    }

    /// `^[A-Za-z0-9][A-Za-z0-9._:-]{0,63}$`
    static func isIdentifier(_ text: String) -> Bool {
        matches(text, first: alnum, rest: { alnum($0) || punctuation($0) })
    }

    /// `^\d+\.\d+\.\d+$`
    static func isSemver(_ text: String) -> Bool {
        let parts = text.split(separator: ".", omittingEmptySubsequences: false)
        return parts.count == 3 && parts.allSatisfy { !$0.isEmpty && $0.allSatisfy(\.isASCIIDigit) }
    }

    static func major(of version: String) -> Int? {
        guard isSemver(version) else { return nil }
        return Int(version.split(separator: ".")[0])
    }
}

extension Character {
    var isASCIIDigit: Bool { isASCII && isNumber }
}

/// Upper bound for JSON safe integers used by every Protocol v1 counter.
let maxSafeInteger: UInt64 = 9_007_199_254_740_991

extension KeyedDecodingContainer {
    func safeInteger(_ key: Key, field: String) throws -> UInt64 {
        let value = try decode(UInt64.self, forKey: key)
        guard value <= maxSafeInteger else { throw ProtocolDecodingError.invalidField(field) }
        return value
    }

    func identifier(_ key: Key, field: String) throws -> String {
        let value = try decode(String.self, forKey: key)
        guard Patterns.isIdentifier(value) else { throw ProtocolDecodingError.invalidField(field) }
        return value
    }

    func timestamp(_ key: Key, field: String) throws -> Date {
        let value = try decode(String.self, forKey: key)
        guard let date = ProtocolTimestamp.parse(value) else { throw ProtocolDecodingError.invalidField(field) }
        return date
    }

    /// RSSI must be an integral dBm value in -100...-1. Zero is a driver
    /// placeholder, never a real reading, and is rejected.
    func rssi(_ key: Key, field: String) throws -> Int? {
        guard let value = try decodeIfPresent(Double.self, forKey: key) else { return nil }
        guard value.isFinite, value == value.rounded(), (-100...(-1)).contains(value) else {
            throw ProtocolDecodingError.invalidField(field)
        }
        return Int(value)
    }

    func requireNotSimulated(_ key: Key) throws {
        guard try decode(Bool.self, forKey: key) == false else { throw ProtocolDecodingError.simulatedData }
    }

    func requireKey(_ key: Key, field: String) throws {
        guard contains(key) else { throw ProtocolDecodingError.invalidField(field) }
    }
}

/// Minimal JSON walker that reports the first duplicated object key.
/// Foundation decoders silently keep one value; the protocol forbids that.
/// Malformed input returns nil so the real decoder reports the syntax error.
enum JSONDuplicateKeyScanner {
    static func firstDuplicateKey(in data: Data) -> String? {
        var walker = Walker(bytes: Array(data))
        return walker.run()
    }

    private struct Walker {
        let bytes: [UInt8]
        var index = 0
        var duplicate: String?

        init(bytes: [UInt8]) { self.bytes = bytes }

        mutating func run() -> String? {
            _ = value(depth: 0)
            return duplicate
        }

        mutating func skipWhitespace() {
            while index < bytes.count, [0x20, 0x09, 0x0A, 0x0D].contains(bytes[index]) { index += 1 }
        }

        mutating func value(depth: Int) -> Bool {
            guard depth < 64, duplicate == nil else { return false }
            skipWhitespace()
            guard index < bytes.count else { return false }
            switch bytes[index] {
            case 0x7B: return object(depth: depth)
            case 0x5B: return array(depth: depth)
            case 0x22: return string() != nil
            default:
                while index < bytes.count, ![0x2C, 0x5D, 0x7D, 0x20, 0x09, 0x0A, 0x0D].contains(bytes[index]) {
                    index += 1
                }
                return true
            }
        }

        mutating func string() -> String? {
            guard index < bytes.count, bytes[index] == 0x22 else { return nil }
            index += 1
            let start = index
            while index < bytes.count {
                if bytes[index] == 0x5C { index += 2; continue }
                if bytes[index] == 0x22 {
                    let text = String(decoding: bytes[start..<index], as: UTF8.self)
                    index += 1
                    return text
                }
                index += 1
            }
            return nil
        }

        mutating func object(depth: Int) -> Bool {
            index += 1
            var keys = Set<String>()
            skipWhitespace()
            if index < bytes.count, bytes[index] == 0x7D { index += 1; return true }
            while index < bytes.count {
                skipWhitespace()
                guard let key = string() else { return false }
                if !keys.insert(key).inserted {
                    duplicate = key
                    return false
                }
                skipWhitespace()
                guard index < bytes.count, bytes[index] == 0x3A else { return false }
                index += 1
                guard value(depth: depth + 1) else { return false }
                skipWhitespace()
                guard index < bytes.count else { return false }
                if bytes[index] == 0x2C { index += 1; continue }
                if bytes[index] == 0x7D { index += 1; return true }
                return false
            }
            return false
        }

        mutating func array(depth: Int) -> Bool {
            index += 1
            skipWhitespace()
            if index < bytes.count, bytes[index] == 0x5D { index += 1; return true }
            while index < bytes.count {
                guard value(depth: depth + 1) else { return false }
                skipWhitespace()
                guard index < bytes.count else { return false }
                if bytes[index] == 0x2C { index += 1; continue }
                if bytes[index] == 0x5D { index += 1; return true }
                return false
            }
            return false
        }
    }
}
