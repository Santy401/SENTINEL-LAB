import Foundation

// Payload models for Protocol v1 (packages/protocol/schemas). Every decoder
// enforces required keys, `additionalProperties: false`, enums, ranges and the
// `simulated: false` constant, so a frame that passes is safe to present as a
// real measurement.

public struct EstimatedDistance: Decodable, Equatable, Sendable {
    public let valueM: Double
    public let calibrated: Bool

    enum CodingKeys: String, CodingKey { case valueM, calibrated, method }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["valueM", "calibrated", "method"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        valueM = try c.decode(Double.self, forKey: .valueM)
        guard valueM.isFinite, (0...10_000).contains(valueM) else {
            throw ProtocolDecodingError.invalidField("estimatedDistance.valueM")
        }
        calibrated = try c.decode(Bool.self, forKey: .calibrated)
        guard try c.decode(String.self, forKey: .method) == "log-distance" else {
            throw ProtocolDecodingError.invalidField("estimatedDistance.method")
        }
    }
}

public struct RfClient: Decodable, Equatable, Sendable, Identifiable {
    public let clientId: UInt64
    public let connected: Bool
    public let rssiDbm: Int?
    public let filteredRssiDbm: Int?
    public let lastSeen: Date
    public let estimatedDistance: EstimatedDistance?

    public var id: UInt64 { clientId }

    enum CodingKeys: String, CodingKey {
        case clientId, connected, rssiDbm, filteredRssiDbm, lastSeen, estimatedDistance
    }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: Set(CodingKeys.allStrings))
        let c = try decoder.container(keyedBy: CodingKeys.self)
        clientId = try c.safeInteger(.clientId, field: "clientId")
        connected = try c.decode(Bool.self, forKey: .connected)
        try c.requireKey(.rssiDbm, field: "rssiDbm")
        rssiDbm = try c.rssi(.rssiDbm, field: "rssiDbm")
        filteredRssiDbm = try c.rssi(.filteredRssiDbm, field: "filteredRssiDbm")
        lastSeen = try c.timestamp(.lastSeen, field: "lastSeen")
        estimatedDistance = try c.decodeIfPresent(EstimatedDistance.self, forKey: .estimatedDistance)
    }
}

extension RfClient.CodingKeys {
    static var allStrings: [String] {
        ["clientId", "connected", "rssiDbm", "filteredRssiDbm", "lastSeen", "estimatedDistance"]
    }
}

public struct RfTelemetry: Decodable, Equatable, Sendable {
    public let bootId: String
    public let firmwareVersion: String
    public let channel: Int
    public let clientCount: Int
    public let clients: [RfClient]

    enum CodingKeys: String, CodingKey { case bootId, firmwareVersion, channel, clientCount, clients, simulated }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["bootId", "firmwareVersion", "channel", "clientCount", "clients", "simulated"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        bootId = try c.identifier(.bootId, field: "bootId")
        firmwareVersion = try c.decode(String.self, forKey: .firmwareVersion)
        guard Patterns.isSemver(firmwareVersion) else { throw ProtocolDecodingError.invalidField("firmwareVersion") }
        channel = try c.decode(Int.self, forKey: .channel)
        guard (1...14).contains(channel) else { throw ProtocolDecodingError.invalidField("channel") }
        clientCount = try c.decode(Int.self, forKey: .clientCount)
        clients = try c.decode([RfClient].self, forKey: .clients)
        guard (0...8).contains(clientCount), clients.count <= 8 else {
            throw ProtocolDecodingError.invalidField("clientCount")
        }
        guard clients.count == clientCount else { throw ProtocolDecodingError.invalidField("clientCount") }
        try c.requireNotSimulated(.simulated)
    }
}

public enum OperationalState: String, Decodable, Sendable, CaseIterable {
    case booting = "BOOTING", idle = "IDLE", monitoring = "MONITORING", error = "ERROR"
}

public struct SystemStatus: Decodable, Equatable, Sendable {
    public let firmwareVersion: String
    public let bootId: String
    public let uptimeMs: UInt64
    public let operationalState: OperationalState
    public let transports: [String]
    public let capabilities: [String]
    public let freeHeapBytes: UInt64?

    enum CodingKeys: String, CodingKey {
        case firmwareVersion, bootId, uptimeMs, operationalState, transports, capabilities, freeHeapBytes, simulated
    }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["firmwareVersion", "bootId", "uptimeMs", "operationalState",
                                                "transports", "capabilities", "freeHeapBytes", "simulated"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        firmwareVersion = try c.decode(String.self, forKey: .firmwareVersion)
        guard Patterns.isSemver(firmwareVersion) else { throw ProtocolDecodingError.invalidField("firmwareVersion") }
        bootId = try c.identifier(.bootId, field: "bootId")
        uptimeMs = try c.safeInteger(.uptimeMs, field: "uptimeMs")
        operationalState = try c.decode(OperationalState.self, forKey: .operationalState)
        transports = try c.decode([String].self, forKey: .transports)
        guard (1...2).contains(transports.count), Set(transports).count == transports.count,
              transports.allSatisfy({ ["http", "websocket"].contains($0) }) else {
            throw ProtocolDecodingError.invalidField("transports")
        }
        capabilities = try c.decodeIfPresent([String].self, forKey: .capabilities) ?? []
        let allowed: Set<String> = ["motion.ingest", "rf.monitor", "laboratory", "webhooks"]
        guard capabilities.count <= 8, Set(capabilities).count == capabilities.count,
              capabilities.allSatisfy(allowed.contains) else {
            throw ProtocolDecodingError.invalidField("capabilities")
        }
        if c.contains(.freeHeapBytes) {
            freeHeapBytes = try c.safeInteger(.freeHeapBytes, field: "freeHeapBytes")
        } else {
            freeHeapBytes = nil
        }
        try c.requireNotSimulated(.simulated)
    }
}

public enum MotionSourceType: String, Decodable, Sendable {
    case none = "None", browserDeviceMotion = "BrowserDeviceMotion", nativeClient = "NativeClient"
}

public struct MotionStatus: Decodable, Equatable, Sendable {
    public let deviceId: String
    public let sourceType: MotionSourceType
    public let paired: Bool
    public let acceptedSamples: UInt64
    public let lastSampleAgeMs: UInt64
    public let lastHttpCode: Int?
    public let signalLost: Bool
    public let simulationRejected: UInt64

    enum CodingKeys: String, CodingKey {
        case deviceId, sourceType, paired, acceptedSamples, lastSampleAgeMs, lastHttpCode, signalLost,
             simulationRejected, simulated
    }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["deviceId", "sourceType", "paired", "acceptedSamples",
                                                "lastSampleAgeMs", "lastHttpCode", "signalLost",
                                                "simulationRejected", "simulated"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        deviceId = try c.identifier(.deviceId, field: "deviceId")
        guard let source = MotionSourceType(rawValue: try c.decode(String.self, forKey: .sourceType)) else {
            throw ProtocolDecodingError.invalidField("sourceType")
        }
        sourceType = source
        paired = try c.decode(Bool.self, forKey: .paired)
        acceptedSamples = try c.safeInteger(.acceptedSamples, field: "acceptedSamples")
        lastSampleAgeMs = try c.safeInteger(.lastSampleAgeMs, field: "lastSampleAgeMs")
        lastHttpCode = try c.decodeIfPresent(Int.self, forKey: .lastHttpCode)
        if let code = lastHttpCode, !(100...599).contains(code) {
            throw ProtocolDecodingError.invalidField("lastHttpCode")
        }
        signalLost = try c.decode(Bool.self, forKey: .signalLost)
        simulationRejected = try c.safeInteger(.simulationRejected, field: "simulationRejected")
        try c.requireNotSimulated(.simulated)
    }
}

public enum MotionState: String, Decodable, Sendable {
    case stable = "STABLE", movement = "MOVEMENT", alert = "ALERT", sensorOffline = "SENSOR_OFFLINE"
}

public struct Vector3: Decodable, Equatable, Sendable {
    public let x: Double
    public let y: Double
    public let z: Double

    enum CodingKeys: String, CodingKey { case x, y, z }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["x", "y", "z"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        x = try c.decode(Double.self, forKey: .x)
        y = try c.decode(Double.self, forKey: .y)
        z = try c.decode(Double.self, forKey: .z)
        guard [x, y, z].allSatisfy({ $0.isFinite && (-500...500).contains($0) }) else {
            throw ProtocolDecodingError.invalidField("vector")
        }
    }
}

public struct MotionSample: Decodable, Equatable, Sendable {
    public enum SourceType: String, Decodable, Sendable {
        case nativeClient = "NativeClient", sensorLogger = "SensorLogger", coreMotion = "CoreMotion"
    }

    public let deviceId: String
    public let sourceType: SourceType
    public let sampleIndex: UInt64
    public let sensorTimestamp: Date?
    public let acceleration: Vector3
    public let gravity: Vector3
    public let linearAcceleration: Vector3
    public let state: MotionState

    enum CodingKeys: String, CodingKey {
        case deviceId, sourceType, sampleIndex, sensorTimestamp, acceleration, gravity, linearAcceleration, state,
             simulated
    }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["deviceId", "sourceType", "sampleIndex", "sensorTimestamp",
                                                "acceleration", "gravity", "linearAcceleration", "state",
                                                "simulated"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        deviceId = try c.identifier(.deviceId, field: "deviceId")
        guard let source = SourceType(rawValue: try c.decode(String.self, forKey: .sourceType)) else {
            throw ProtocolDecodingError.invalidField("sourceType")
        }
        sourceType = source
        sampleIndex = try c.safeInteger(.sampleIndex, field: "sampleIndex")
        sensorTimestamp = try c.contains(.sensorTimestamp) ? c.timestamp(.sensorTimestamp, field: "sensorTimestamp") : nil
        acceleration = try c.decode(Vector3.self, forKey: .acceleration)
        gravity = try c.decode(Vector3.self, forKey: .gravity)
        linearAcceleration = try c.decode(Vector3.self, forKey: .linearAcceleration)
        state = try c.decode(MotionState.self, forKey: .state)
        try c.requireNotSimulated(.simulated)
    }
}

public enum EventName: String, Decodable, Sendable, CaseIterable {
    case deviceConnected = "DEVICE_CONNECTED"
    case deviceDisconnected = "DEVICE_DISCONNECTED"
    case signalWeak = "SIGNAL_WEAK"
    case signalRecovered = "SIGNAL_RECOVERED"
    case coverageLimitReached = "COVERAGE_LIMIT_REACHED"
    case movementDetected = "MOVEMENT_DETECTED"
    case motionAlert = "MOTION_ALERT"
    case sensorOffline = "SENSOR_OFFLINE"
    case sensorRecovered = "SENSOR_RECOVERED"
    case heartbeat = "HEARTBEAT"
}

public struct EventDetails: Decodable, Equatable, Sendable {
    public let previousState: MotionState?
    public let newState: MotionState?
    public let rssiDbm: Int?
    public let clientId: UInt64?
    public let deviceId: String?
    public let message: String?

    enum CodingKeys: String, CodingKey { case previousState, newState, rssiDbm, clientId, deviceId, message }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["previousState", "newState", "rssiDbm", "clientId", "deviceId", "message"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        previousState = try c.decodeIfPresent(MotionState.self, forKey: .previousState)
        newState = try c.decodeIfPresent(MotionState.self, forKey: .newState)
        rssiDbm = try c.rssi(.rssiDbm, field: "details.rssiDbm")
        clientId = try c.contains(.clientId) ? c.safeInteger(.clientId, field: "details.clientId") : nil
        deviceId = try c.contains(.deviceId) ? c.identifier(.deviceId, field: "details.deviceId") : nil
        message = try c.decodeIfPresent(String.self, forKey: .message)
        if let message, !(1...256).contains(message.count) {
            throw ProtocolDecodingError.invalidField("details.message")
        }
    }
}

public struct ProtocolEvent: Decodable, Equatable, Sendable {
    public let eventId: String
    public let name: EventName
    public let occurredAt: Date
    public let details: EventDetails?

    enum CodingKeys: String, CodingKey { case eventId, name, occurredAt, details, simulated }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["eventId", "name", "occurredAt", "details", "simulated"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        eventId = try c.identifier(.eventId, field: "eventId")
        name = try c.decode(EventName.self, forKey: .name)
        occurredAt = try c.timestamp(.occurredAt, field: "occurredAt")
        details = try c.decodeIfPresent(EventDetails.self, forKey: .details)
        try c.requireNotSimulated(.simulated)
    }
}

public enum PeerRole: String, Codable, Sendable {
    case firmware, web, gateway, ios, sensorClient = "sensor-client"
}

public struct HelloPayload: Codable, Equatable, Sendable {
    public let deviceId: String
    public let role: PeerRole
    public let supportedVersion: String
    public let capabilities: [String]

    public init(deviceId: String, role: PeerRole, supportedVersion: String, capabilities: [String]) {
        self.deviceId = deviceId
        self.role = role
        self.supportedVersion = supportedVersion
        self.capabilities = capabilities
    }

    enum CodingKeys: String, CodingKey { case deviceId, role, supportedVersion, capabilities }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["deviceId", "role", "supportedVersion", "capabilities"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        deviceId = try c.identifier(.deviceId, field: "deviceId")
        role = try c.decode(PeerRole.self, forKey: .role)
        supportedVersion = try c.decode(String.self, forKey: .supportedVersion)
        // A peer advertising another major version cannot talk Protocol v1.
        guard Patterns.major(of: supportedVersion) == 1 else {
            throw ProtocolDecodingError.unsupportedVersion(supportedVersion)
        }
        capabilities = try c.decode([String].self, forKey: .capabilities)
        guard capabilities.count <= 16, Set(capabilities).count == capabilities.count,
              capabilities.allSatisfy(Patterns.isSource) else {
            throw ProtocolDecodingError.invalidField("capabilities")
        }
    }
}

public struct HeartbeatPayload: Codable, Equatable, Sendable {
    public let bootId: String
    public let uptimeMs: UInt64

    public init(bootId: String, uptimeMs: UInt64) {
        self.bootId = bootId
        self.uptimeMs = uptimeMs
    }

    enum CodingKeys: String, CodingKey { case bootId, uptimeMs }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["bootId", "uptimeMs"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        bootId = try c.identifier(.bootId, field: "bootId")
        uptimeMs = try c.safeInteger(.uptimeMs, field: "uptimeMs")
    }
}

public enum GoodbyeReason: String, Codable, Sendable {
    case shutdown, reconnect, timeout, clientRequest = "client-request"
}

public struct GoodbyePayload: Codable, Equatable, Sendable {
    public let reason: GoodbyeReason

    public init(reason: GoodbyeReason) { self.reason = reason }

    enum CodingKeys: String, CodingKey { case reason }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["reason"])
        reason = try decoder.container(keyedBy: CodingKeys.self).decode(GoodbyeReason.self, forKey: .reason)
    }
}

public enum ProtocolErrorCode: String, Decodable, Sendable {
    case invalidEnvelope = "INVALID_ENVELOPE"
    case unsupportedProtocolVersion = "UNSUPPORTED_PROTOCOL_VERSION"
    case unknownMessageType = "UNKNOWN_MESSAGE_TYPE"
    case payloadTooLarge = "PAYLOAD_TOO_LARGE"
    case rateLimited = "RATE_LIMITED"
    case unauthorized = "UNAUTHORIZED"
    case internalError = "INTERNAL_ERROR"
}

public struct ErrorPayload: Decodable, Equatable, Sendable {
    public let code: ProtocolErrorCode
    public let message: String
    public let retryable: Bool
    public let relatedSource: String?
    public let relatedSequence: UInt64?

    enum CodingKeys: String, CodingKey { case code, message, retryable, relatedSource, relatedSequence }

    public init(from decoder: Decoder) throws {
        try decoder.rejectUnknownKeys(allowed: ["code", "message", "retryable", "relatedSource", "relatedSequence"])
        let c = try decoder.container(keyedBy: CodingKeys.self)
        code = try c.decode(ProtocolErrorCode.self, forKey: .code)
        message = try c.decode(String.self, forKey: .message)
        guard (1...256).contains(message.count) else { throw ProtocolDecodingError.invalidField("message") }
        retryable = try c.decode(Bool.self, forKey: .retryable)
        relatedSource = try c.decodeIfPresent(String.self, forKey: .relatedSource)
        if let relatedSource, !Patterns.isSource(relatedSource) {
            throw ProtocolDecodingError.invalidField("relatedSource")
        }
        relatedSequence = try c.contains(.relatedSequence) ? c.safeInteger(.relatedSequence, field: "relatedSequence") : nil
    }
}
