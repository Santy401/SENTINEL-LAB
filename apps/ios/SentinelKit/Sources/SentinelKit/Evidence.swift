import Foundation

/// Non-sensitive diagnostic snapshot that the user can share. It never holds
/// the bearer token, Wi-Fi SSID/password, or raw frames from the device.
public struct EvidenceReport: Codable, Equatable, Sendable {
    public struct Connection: Codable, Equatable, Sendable {
        public var phase: String
        public var failures: Int
        public var reconnects: Int
        public var lastFailure: String?
        public var tokenConfigured: Bool

        public init(phase: String, failures: Int, reconnects: Int, lastFailure: String?, tokenConfigured: Bool) {
            self.phase = phase
            self.failures = failures
            self.reconnects = reconnects
            self.lastFailure = lastFailure
            self.tokenConfigured = tokenConfigured
        }
    }

    public struct EventRecord: Codable, Equatable, Sendable {
        public var name: String
        public var occurredAt: String
        public var clientId: UInt64?

        public init(name: String, occurredAt: String, clientId: UInt64?) {
            self.name = name
            self.occurredAt = occurredAt
            self.clientId = clientId
        }
    }

    public var schema = "sentinel-lab.ios-evidence.v1"
    public var generatedAt: String
    public var appVersion: String
    /// True when the app was in demo mode; demo evidence is never a measurement.
    public var demo: Bool
    public var host: String
    public var realtimeStatus: RealtimeStatus?
    public var firmwareFw: String?
    public var firmwareUptimeMs: UInt64?
    public var connection: Connection
    public var framesByType: [String: Int]
    public var decodeFailures: Int
    public var health: [HealthFinding]
    public var recentEvents: [EventRecord]

    public init(generatedAt: Date, appVersion: String, demo: Bool, host: String, realtimeStatus: RealtimeStatus?,
                deviceState: DeviceState?, connection: Connection, framesByType: [String: Int],
                decodeFailures: Int, health: [HealthFinding], recentEvents: [EventRecord]) {
        self.generatedAt = ProtocolTimestamp.format(generatedAt)
        self.appVersion = appVersion
        self.demo = demo
        self.host = host
        self.realtimeStatus = realtimeStatus
        firmwareFw = deviceState?.fw
        firmwareUptimeMs = deviceState?.uptimeMs
        self.connection = connection
        self.framesByType = framesByType
        self.decodeFailures = decodeFailures
        self.health = health
        self.recentEvents = Array(recentEvents.suffix(50))
    }
}

public enum EvidenceExporter {
    public static let redactedMarker = "[REDACTED]"

    /// Removes `Bearer <value>` fragments and any literal secret from free text.
    public static func redact(_ text: String, secrets: [String]) -> String {
        var result = text
        for secret in secrets where secret.count >= 4 {
            result = result.replacingOccurrences(of: secret, with: redactedMarker)
        }
        if let regex = try? NSRegularExpression(pattern: "(?i)bearer\\s+[^\\s\"]+") {
            let range = NSRange(result.startIndex..<result.endIndex, in: result)
            result = regex.stringByReplacingMatches(in: result, range: range, withTemplate: "Bearer " + redactedMarker)
        }
        return result
    }

    /// Encodes the report and guarantees no secret survives in the output.
    public static func export(_ report: EvidenceReport, secrets: [String]) throws -> Data {
        var sanitized = report
        sanitized.connection.lastFailure = report.connection.lastFailure.map { redact($0, secrets: secrets) }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        let data = try encoder.encode(sanitized)
        let text = redact(String(decoding: data, as: UTF8.self), secrets: secrets)
        return Data(text.utf8)
    }
}
