import Foundation

// HTTP v0.3.1 responses (src/v02/web_dashboard.cpp, src/rf/web_rf.cpp) and
// the additive `/api/realtime/status` health route of the realtime image.
// HTTP decoding is deliberately lenient: every field is optional so an older
// or newer firmware still renders whatever it actually reports.

public struct RealtimeStatus: Codable, Equatable, Sendable {
    public var fw: String?
    public var firmwareVersion: String?
    public var httpApiVersion: String?
    public var realtimeVersion: String?
    public var bootId: String?
    public var uptimeMs: UInt64?
    public var clockSynced: Bool?
    public var clockSource: String?
    public var clockSyncAgeMs: UInt64?
    public var clientCount: Int?
    public var freeHeapBytes: UInt64?
    public var minFreeHeapBytes: UInt64?
    public var largestFreeBlockBytes: UInt64?
    public var minLoopStackFreeBytes: UInt64?
    public var heapSamples: UInt64?
    public var droppedFrames: UInt64?
    public var rejectedMessages: UInt64?
    public var droppedTelemetry: UInt64?
    public var skippedSimulatedEvents: UInt64?
    public var handshakeTimeouts: UInt64?
    public var idleTimeouts: UInt64?
    public var resetReason: String?
    /// Firmware ≥ 0.5.0: physical RGB LED state (WS2812 on the DevKit).
    public var rgb: RgbStatus?
    /// Firmware ≥ 0.5.0: phone motion as received by the ESP32.
    public var phoneMotion: PhoneMotionStatus?

    public init() {}

    /// Version of the flashed image; older realtime builds only report the
    /// realtime core version next to the HTTP compatibility `fw` value.
    public var imageVersion: String? { firmwareVersion ?? realtimeVersion ?? fw }
}

public struct DeviceState: Codable, Equatable, Sendable {
    public struct WiFi: Codable, Equatable, Sendable {
        public var mode: String?
        public var ssid: String?
        public var ip: String?
        public var clients: Int?
    }

    public struct MotionEventEntry: Codable, Equatable, Sendable {
        public var t: UInt64?
        public var type: String?
        public var from: String?
        public var to: String?
        public var mag: Double?
    }

    public var fw: String?
    public var uptimeMs: UInt64?
    public var state: String?
    public var fresh: Bool?
    public var source: String?
    public var simulation: Bool?
    public var wifi: WiFi?
    public var sampleCount: UInt64?
    public var lastSampleMs: UInt64?
    public var events: [MotionEventEntry]?
}

public struct RfSnapshot: Codable, Equatable, Sendable {
    public struct HistoryPoint: Codable, Equatable, Sendable {
        public var t: UInt64
        public var rssi: Double
    }

    public struct Device: Codable, Equatable, Sendable, Identifiable {
        public var id: UInt64
        public var connected: Bool?
        public var lastSeenMs: UInt64?
        public var rssiDbm: Double?
        public var filteredDbm: Double?
        public var history: [HistoryPoint]?
    }

    public struct LinkEvent: Codable, Equatable, Sendable {
        public var t: UInt64?
        public var clientId: UInt64?
        public var type: String?
    }

    public var bootId: String?
    public var fw: String?
    public var uptimeMs: UInt64?
    public var mode: String?
    public var staConnected: Bool?
    public var notificationsEnabled: Bool?
    public var signalWeak: Bool?
    public var available: Bool?
    public var ssid: String?
    public var ip: String?
    public var clients: Int?
    public var channel: Int?
    public var frequencyMHz: Double?
    public var wavelengthM: Double?
    public var devices: [Device]?
    public var events: [LinkEvent]?
}

public struct EventLog: Codable, Equatable, Sendable {
    public struct Entry: Codable, Equatable, Sendable, Identifiable {
        public var bootId: String?
        public var sequence: UInt64
        public var timestampMs: UInt64?
        public var event: String?
        public var clientId: UInt64?
        public var simulated: Bool?
        public var notify: Bool?
        public var rssiDbm: Double?
        public var distanceEstimatedM: Double?

        public var id: UInt64 { sequence }

        enum CodingKeys: String, CodingKey {
            case bootId = "boot_id", sequence, timestampMs = "timestamp_ms", event, clientId = "client_id",
                 simulated, notify, rssiDbm = "rssi_dbm", distanceEstimatedM = "distance_estimated_m"
        }
    }

    public var bootId: String?
    public var notificationsEnabled: Bool?
    public var events: [Entry]?
}

/// State of the ESP32 RGB engine as reported by `/api/realtime/status`.
public struct RgbStatus: Codable, Equatable, Sendable {
    public var enabled: Bool?
    public var pin: Int?
    public var mode: String?
    public var r: Int?
    public var g: Int?
    public var b: Int?

    public init(enabled: Bool? = nil, pin: Int? = nil, mode: String? = nil, r: Int? = nil, g: Int? = nil, b: Int? = nil) {
        self.enabled = enabled
        self.pin = pin
        self.mode = mode
        self.r = r
        self.g = g
        self.b = b
    }

    /// Color components in 0...1, clamped; nil when the firmware did not report them.
    public var unitColor: (red: Double, green: Double, blue: Double)? {
        guard let r, let g, let b else { return nil }
        func unit(_ value: Int) -> Double { Double(min(max(value, 0), 255)) / 255 }
        return (unit(r), unit(g), unit(b))
    }

    /// Spanish description of the firmware LED mode.
    public var modeLabel: String {
        switch mode {
        case "boot": return "Arranque"
        case "offline": return "Sin clientes"
        case "client": return "Cliente conectado"
        case "synced": return "Reloj sincronizado"
        case "motion": return "Movimiento del iPhone"
        case "event": return "Evento"
        case nil: return "—"
        case let other?: return other
        }
    }
}

/// Phone motion summary computed by the ESP32 from received `motion.sample`.
public struct PhoneMotionStatus: Codable, Equatable, Sendable {
    public var active: Bool?
    public var deviceId: String?
    public var linearAccelMs2: Double?
    public var intensity: Double?
    public var peakMs2: Double?
    public var samples: UInt64?
    public var sampleAgeMs: UInt64?
}

/// `GET /api/lab`: distance experiments stored in ESP32 RAM.
public struct LabSnapshot: Codable, Equatable, Sendable {
    public struct Experiment: Codable, Equatable, Sendable, Identifiable {
        public var id: Int
        public var name: String?
        public var clientId: UInt64?
        public var environment: Int?
        public var calibrated: Bool?
        public var n: Double?
        public var referenceRssi: Double?
        public var currentEstimateM: Double?

        /// Model fitted on the ESP32, when calibrated.
        public var model: LogDistanceModel? {
            guard calibrated == true, let n, let referenceRssi else { return nil }
            let model = LogDistanceModel(referenceRssiAt1m: referenceRssi, exponent: n)
            return model.isPlausible ? model : nil
        }
    }

    public struct Measurement: Codable, Equatable, Sendable, Identifiable {
        public var id: Int
        public var experimentId: Int?
        public var clientId: UInt64?
        public var actualM: Double?
        public var connected: Bool?
        public var calibration: Bool?
        public var rssiDbm: Double?
        public var filteredDbm: Double?
        public var estimatedM: Double?
        public var errorM: Double?
    }

    public var bootId: String?
    public var experiments: [Experiment]?
    public var measurements: [Measurement]?

    /// Most recent calibrated experiment for a client.
    public func calibratedExperiment(clientId: UInt64) -> Experiment? {
        experiments?.last { $0.clientId == clientId && $0.model != nil }
    }
}

/// Error body of every lab POST: `{"ok":false,"error":"..."}`.
public struct LabErrorBody: Codable, Equatable, Sendable {
    public var ok: Bool?
    public var error: String?
}
