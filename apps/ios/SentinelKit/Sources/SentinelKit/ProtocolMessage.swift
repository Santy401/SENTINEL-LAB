import Foundation

public let protocolVersion = "1.0.0"
public let websocketSubprotocol = "sentinel.v1"
/// Firmware WebSocket frame cap (src/realtime/frame_codec.h).
public let firmwareMaxFrameBytes = 1024
/// Contract-level envelope cap (packages/protocol).
public let protocolMaxEnvelopeBytes = 16 * 1024

/// Decoded payload. Unknown message types are kept as an explicit case so a
/// newer firmware never crashes the app; they are counted, not displayed.
public enum MessageBody: Equatable, Sendable {
    case rfTelemetry(RfTelemetry)
    case systemStatus(SystemStatus)
    case motionStatus(MotionStatus)
    case motionSample(MotionSample)
    case event(ProtocolEvent)
    case hello(HelloPayload)
    case heartbeat(HeartbeatPayload)
    case goodbye(GoodbyePayload)
    case error(ErrorPayload)
    case unknown(type: String)
}

public struct ProtocolMessage: Decodable, Equatable, Sendable {
    public let protocolVersion: String
    public let type: String
    public let timestamp: Date
    public let sequence: UInt64
    public let source: String
    public let sessionId: String
    public let body: MessageBody

    enum CodingKeys: String, CodingKey {
        case protocolVersion, type, timestamp, sequence, source, sessionId, payload
    }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["protocolVersion", "type", "timestamp", "sequence", "source",
                                                "sessionId", "payload"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        protocolVersion = try c.decode(String.self, forKey: .protocolVersion)
        guard Patterns.major(of: protocolVersion) == 1 else {
            throw ProtocolDecodingError.unsupportedVersion(protocolVersion)
        }
        type = try c.decode(String.self, forKey: .type)
        timestamp = try c.timestamp(.timestamp, field: "timestamp")
        sequence = try c.safeInteger(.sequence, field: "sequence")
        source = try c.decode(String.self, forKey: .source)
        guard Patterns.isSource(source) else { throw ProtocolDecodingError.invalidField("source") }
        sessionId = try c.identifier(.sessionId, field: "sessionId")
        try c.requireKey(.payload, field: "payload")

        switch type {
        case "rf.telemetry": body = .rfTelemetry(try c.decode(RfTelemetry.self, forKey: .payload))
        case "system.status": body = .systemStatus(try c.decode(SystemStatus.self, forKey: .payload))
        case "motion.status": body = .motionStatus(try c.decode(MotionStatus.self, forKey: .payload))
        case "motion.sample": body = .motionSample(try c.decode(MotionSample.self, forKey: .payload))
        case "event": body = .event(try c.decode(ProtocolEvent.self, forKey: .payload))
        case "connection.hello": body = .hello(try c.decode(HelloPayload.self, forKey: .payload))
        case "connection.heartbeat": body = .heartbeat(try c.decode(HeartbeatPayload.self, forKey: .payload))
        case "connection.goodbye": body = .goodbye(try c.decode(GoodbyePayload.self, forKey: .payload))
        case "error": body = .error(try c.decode(ErrorPayload.self, forKey: .payload))
        default: body = .unknown(type: type)
        }
    }
}

public enum ProtocolDecoder {
    /// Decodes one inbound frame with every contract check applied.
    public static func decode(_ data: Data, maxBytes: Int = protocolMaxEnvelopeBytes) throws -> ProtocolMessage {
        guard data.count <= maxBytes else { throw ProtocolDecodingError.tooLarge(data.count) }
        if let key = JSONDuplicateKeyScanner.firstDuplicateKey(in: data) {
            throw ProtocolDecodingError.duplicateKey(key)
        }
        return try JSONDecoder().decode(ProtocolMessage.self, from: data)
    }

    public static func decode(_ text: String, maxBytes: Int = protocolMaxEnvelopeBytes) throws -> ProtocolMessage {
        try decode(Data(text.utf8), maxBytes: maxBytes)
    }
}

/// Outbound envelope written by this client.
struct OutboundEnvelope<Payload: Encodable>: Encodable {
    let protocolVersion: String
    let type: String
    let timestamp: String
    let sequence: UInt64
    let source: String
    let sessionId: String
    let payload: Payload
}

public enum ClientSessionError: Error, Equatable, Sendable {
    case invalidSource
    case invalidSessionId
    case invalidDeviceId
    case sequenceExhausted
    case frameTooLarge(Int)
    case invalidMotionSample
}

/// Outbound `motion.sample` payload (packages/protocol/schemas/motion.sample.schema.json).
public struct OutboundMotionSample: Encodable, Equatable, Sendable {
    public let deviceId: String
    public let sourceType = "CoreMotion"
    public let sampleIndex: UInt64
    public let sensorTimestamp: String
    public let acceleration: Vec3
    public let gravity: Vec3
    public let linearAcceleration: Vec3
    public let state: String
    public let simulated = false

    enum CodingKeys: String, CodingKey {
        case deviceId, sourceType, sampleIndex, sensorTimestamp, acceleration, gravity, linearAcceleration, state,
             simulated
    }

    /// - Parameters:
    ///   - reading: real reading in m/s².
    ///   - filteredMs2: filtered |linear| used for the state, consistent with the UI.
    public init(deviceId: String, sampleIndex: UInt64, reading: MotionReading, filteredMs2: Double) {
        self.deviceId = deviceId
        self.sampleIndex = sampleIndex
        sensorTimestamp = ProtocolTimestamp.format(reading.date)
        acceleration = reading.total.rounded()
        gravity = reading.gravity.rounded()
        linearAcceleration = reading.linear.rounded()
        state = ShakeClassifier.state(filteredMs2: filteredMs2).rawValue
    }

    var isValid: Bool {
        sampleIndex <= maxSafeInteger && [acceleration, gravity, linearAcceleration].allSatisfy { vector in
            vector.isFinite && [vector.x, vector.y, vector.z].allSatisfy { (-500...500).contains($0) }
        } && state != MotionState.sensorOffline.rawValue
    }
}

/// Client-owned identity for one WebSocket connection. The firmware binds the
/// first accepted `source` and `sessionId`; a reconnect must create a new
/// session so sequence numbers restart at 1 without colliding.
public struct ClientSession: Sendable {
    public static let defaultSource = "ios-app"
    public static let capabilities = ["telemetry.view", "clock.sync"]

    public let source: String
    public let sessionId: String
    public private(set) var sequence: UInt64 = 0

    public init(source: String = ClientSession.defaultSource, sessionId: String = ClientSession.makeSessionId()) throws {
        guard Patterns.isSource(source) else { throw ClientSessionError.invalidSource }
        guard Patterns.isIdentifier(sessionId) else { throw ClientSessionError.invalidSessionId }
        self.source = source
        self.sessionId = sessionId
    }

    public static func makeSessionId() -> String {
        "ios-" + UUID().uuidString.lowercased()
    }

    public mutating func hello(deviceId: String, role: PeerRole = .ios, at date: Date) throws -> Data {
        guard Patterns.isIdentifier(deviceId) else { throw ClientSessionError.invalidDeviceId }
        return try envelope(type: "connection.hello",
                            payload: HelloPayload(deviceId: deviceId, role: role, supportedVersion: protocolVersion,
                                                  capabilities: Self.capabilities),
                            at: date)
    }

    public mutating func heartbeat(bootId: String, uptimeMs: UInt64, at date: Date) throws -> Data {
        guard Patterns.isIdentifier(bootId) else { throw ClientSessionError.invalidDeviceId }
        return try envelope(type: "connection.heartbeat",
                            payload: HeartbeatPayload(bootId: bootId, uptimeMs: min(uptimeMs, maxSafeInteger)),
                            at: date)
    }

    /// `motion.sample` built from a real CoreMotion reading (SI units, device
    /// frame). The caller chooses which reading to send (see PeakHoldWindow).
    public mutating func motionSample(_ sample: OutboundMotionSample, at date: Date) throws -> Data {
        guard Patterns.isIdentifier(sample.deviceId) else { throw ClientSessionError.invalidDeviceId }
        guard sample.isValid else { throw ClientSessionError.invalidMotionSample }
        return try envelope(type: "motion.sample", payload: sample, at: date)
    }

    public mutating func goodbye(reason: GoodbyeReason, at date: Date) throws -> Data {
        try envelope(type: "connection.goodbye", payload: GoodbyePayload(reason: reason), at: date)
    }

    private mutating func envelope<P: Encodable>(type: String, payload: P, at date: Date) throws -> Data {
        guard sequence < maxSafeInteger else { throw ClientSessionError.sequenceExhausted }
        let next = sequence + 1
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        let data = try encoder.encode(OutboundEnvelope(protocolVersion: protocolVersion, type: type,
                                                       timestamp: ProtocolTimestamp.format(date), sequence: next,
                                                       source: source, sessionId: sessionId, payload: payload))
        guard data.count <= firmwareMaxFrameBytes else { throw ClientSessionError.frameTooLarge(data.count) }
        sequence = next
        return data
    }
}
