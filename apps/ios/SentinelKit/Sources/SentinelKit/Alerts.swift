import Foundation

/// Local alert rules evaluated by the app from real device data. Each kind
/// is rate-limited (cooldown) and edge-triggered: a condition that stays true
/// raises one alert, not one per poll. Shared thresholds are documented in
/// docs/ios/README.md and mirror the gateway rules.
public enum AlertKind: String, CaseIterable, Codable, Sendable {
    case strongShake
    case outOfRange
    case deviceReboot
    case lowHeap
    case clockUnsynced

    public var title: String {
        switch self {
        case .strongShake: return "Agitación fuerte"
        case .outOfRange: return "iPhone fuera de alcance"
        case .deviceReboot: return "ESP32 reiniciado"
        case .lowHeap: return "Memoria baja en el ESP32"
        case .clockUnsynced: return "Reloj sin sincronizar"
        }
    }

    public var severity: AlertSeverity {
        switch self {
        case .strongShake, .deviceReboot: return .critical
        case .outOfRange, .lowHeap: return .warning
        case .clockUnsynced: return .info
        }
    }
}

public enum AlertSeverity: String, Codable, Sendable, Comparable {
    case info, warning, critical

    private var rank: Int { self == .info ? 0 : (self == .warning ? 1 : 2) }
    public static func < (lhs: AlertSeverity, rhs: AlertSeverity) -> Bool { lhs.rank < rhs.rank }
}

public struct LabAlert: Identifiable, Equatable, Codable, Sendable {
    public let id: String
    public let kind: AlertKind
    public let date: Date
    public let message: String

    public init(id: String, kind: AlertKind, date: Date, message: String) {
        self.id = id
        self.kind = kind
        self.date = date
        self.message = message
    }
}

/// One evaluation input. Every field is optional: unknown data never raises.
public struct AlertInputs: Sendable {
    public var now: Date
    /// Filtered (RMS) shake of this iPhone, m/s².
    public var shakeMs2: Double?
    /// Filtered RSSI of this iPhone as measured by the ESP32; nil = unknown.
    public var rssiDbm: Double?
    /// True when this iPhone was identified but is no longer in the RF list.
    public var phoneMissing: Bool
    public var bootId: String?
    public var resetReason: String?
    public var freeHeapBytes: UInt64?
    /// nil when no status has been read yet.
    public var clockSynced: Bool?
    /// True while the app talks to the device (HTTP or WebSocket succeeding).
    public var deviceReachable: Bool

    public init(now: Date, shakeMs2: Double? = nil, rssiDbm: Double? = nil, phoneMissing: Bool = false,
                bootId: String? = nil, resetReason: String? = nil, freeHeapBytes: UInt64? = nil,
                clockSynced: Bool? = nil, deviceReachable: Bool = true) {
        self.now = now
        self.shakeMs2 = shakeMs2
        self.rssiDbm = rssiDbm
        self.phoneMissing = phoneMissing
        self.bootId = bootId
        self.resetReason = resetReason
        self.freeHeapBytes = freeHeapBytes
        self.clockSynced = clockSynced
        self.deviceReachable = deviceReachable
    }
}

public struct AlertEngine: Sendable {
    public static let shakeThresholdMs2 = ShakeClassifier.alertThreshold  // 4 m/s²
    public static let outOfRangeRssiDbm = -85.0
    public static let lowHeapBytes: UInt64 = 40_000
    public static let unsyncedGrace: TimeInterval = 120
    public static let defaultCooldown: TimeInterval = 60
    /// Keep the most recent alerts only.
    public static let maxAlerts = 50

    public var cooldown: TimeInterval
    public private(set) var alerts: [LabAlert] = []

    private var active: Set<AlertKind> = []
    private var lastRaised: [AlertKind: Date] = [:]
    private var lastBootId: String?
    private var unsyncedSince: Date?
    private var counter = 0

    public init(cooldown: TimeInterval = AlertEngine.defaultCooldown) {
        self.cooldown = cooldown
    }

    /// Evaluates the rules and returns the alerts raised by this call.
    @discardableResult
    public mutating func evaluate(_ input: AlertInputs) -> [LabAlert] {
        var raised: [LabAlert] = []

        if let shake = input.shakeMs2, shake.isFinite {
            edge(.strongShake, condition: shake >= Self.shakeThresholdMs2, input.now,
                 message: String(format: "Agitación de %.1f m/s² (%.2f g), umbral %.0f m/s².",
                                 shake, shake / Physics.standardGravity, Self.shakeThresholdMs2), into: &raised)
        }

        let weak = input.rssiDbm.map { $0 < Self.outOfRangeRssiDbm } ?? false
        let outOfRange = input.phoneMissing || weak
        let rangeMessage = input.phoneMissing
            ? "El ESP32 ya no ve a este iPhone en su lista de clientes."
            : String(format: "RSSI %.0f dBm, por debajo de %.0f dBm.", input.rssiDbm ?? 0, Self.outOfRangeRssiDbm)
        edge(.outOfRange, condition: outOfRange, input.now, message: rangeMessage, into: &raised)

        if let boot = input.bootId, !boot.isEmpty {
            if let previous = lastBootId, previous != boot {
                let reason = input.resetReason.map { " Motivo: \($0)." } ?? ""
                raise(.deviceReboot, input.now, message: "Nuevo arranque (bootId \(boot.prefix(8))).\(reason)",
                      bypassCooldown: true, into: &raised)
            }
            lastBootId = boot
        }

        if let heap = input.freeHeapBytes {
            edge(.lowHeap, condition: heap < Self.lowHeapBytes, input.now,
                 message: "Heap libre \(heap / 1024) KiB (< \(Self.lowHeapBytes / 1024) KiB).", into: &raised)
        }

        if let synced = input.clockSynced, input.deviceReachable {
            if synced {
                unsyncedSince = nil
                active.remove(.clockUnsynced)
            } else {
                let since = unsyncedSince ?? input.now
                unsyncedSince = since
                edge(.clockUnsynced, condition: input.now.timeIntervalSince(since) >= Self.unsyncedGrace, input.now,
                     message: "Más de 2 min sin hora UTC: no hay stream en tiempo real. Guarda el token en Ajustes.",
                     into: &raised)
            }
        }
        return raised
    }

    public mutating func clear() {
        alerts.removeAll()
    }

    private mutating func edge(_ kind: AlertKind, condition: Bool, _ now: Date, message: String,
                               into raised: inout [LabAlert]) {
        if condition {
            if !active.contains(kind) {
                active.insert(kind)
                raise(kind, now, message: message, bypassCooldown: false, into: &raised)
            }
        } else {
            active.remove(kind)
        }
    }

    private mutating func raise(_ kind: AlertKind, _ now: Date, message: String, bypassCooldown: Bool,
                                into raised: inout [LabAlert]) {
        if !bypassCooldown, let last = lastRaised[kind], now.timeIntervalSince(last) < cooldown { return }
        lastRaised[kind] = now
        counter += 1
        let alert = LabAlert(id: "\(kind.rawValue)-\(Int(now.timeIntervalSince1970 * 1000))-\(counter)",
                             kind: kind, date: now, message: message)
        alerts.insert(alert, at: 0)
        if alerts.count > Self.maxAlerts { alerts.removeLast(alerts.count - Self.maxAlerts) }
        raised.append(alert)
    }
}

/// Content of the Live Activity / widget. Lives in SentinelKit so the app
/// and the widget extension share one Codable type (tested on Linux).
public struct SentinelActivityState: Codable, Hashable, Sendable {
    public var connected: Bool
    public var clockSynced: Bool
    /// 0..1, |linear acceleration| / 8 m/s² (firmware scale).
    public var shakeIntensity: Double
    public var shakeMs2: Double
    public var ledRed: Int
    public var ledGreen: Int
    public var ledBlue: Int
    public var ledMode: String
    public var lastAlert: String?
    public var updatedAt: Date

    public init(connected: Bool = false, clockSynced: Bool = false, shakeIntensity: Double = 0, shakeMs2: Double = 0,
                ledRed: Int = 0, ledGreen: Int = 0, ledBlue: Int = 0, ledMode: String = "offline",
                lastAlert: String? = nil, updatedAt: Date = Date()) {
        self.connected = connected
        self.clockSynced = clockSynced
        self.shakeIntensity = min(max(shakeIntensity, 0), 1)
        self.shakeMs2 = shakeMs2
        self.ledRed = ledRed
        self.ledGreen = ledGreen
        self.ledBlue = ledBlue
        self.ledMode = ledMode
        self.lastAlert = lastAlert
        self.updatedAt = updatedAt
    }

    /// True when the change is worth an ActivityKit update (updates are
    /// budgeted by iOS; tiny intensity jitter is ignored).
    public func differsMeaningfully(from other: SentinelActivityState) -> Bool {
        connected != other.connected || clockSynced != other.clockSynced || ledMode != other.ledMode ||
            lastAlert != other.lastAlert || abs(shakeIntensity - other.shakeIntensity) >= 0.08 ||
            abs(ledRed - other.ledRed) + abs(ledGreen - other.ledGreen) + abs(ledBlue - other.ledBlue) >= 12
    }
}

/// Haptic intensity for a shake value: same 0..1 scale as the firmware LED,
/// with a dead zone so a phone resting in the hand stays silent.
public enum HapticMapping {
    public static let deadZone = 0.06

    public static func intensity(shakeMs2: Double) -> Double {
        let value = Physics.intensity(linearMs2: shakeMs2)
        guard value > deadZone else { return 0 }
        return min(1, (value - deadZone) / (1 - deadZone))
    }
}
