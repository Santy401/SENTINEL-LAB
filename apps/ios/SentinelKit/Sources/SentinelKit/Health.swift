import Foundation

public enum HealthLevel: Int, Comparable, Codable, Sendable {
    case ok = 0, info, warning, critical

    public static func < (lhs: HealthLevel, rhs: HealthLevel) -> Bool { lhs.rawValue < rhs.rawValue }
}

public struct HealthFinding: Equatable, Codable, Sendable, Identifiable {
    public enum Kind: String, Codable, Sendable {
        case clockUnsynced, heapLow, heapFragmented, stackLow, framesDropped, messagesRejected,
             telemetryDropped, rebootDetected, abnormalReset, handshakeTimeouts, clientsSaturated
    }

    public let kind: Kind
    public let level: HealthLevel
    /// User-facing Spanish explanation; never contains secrets.
    public let message: String

    public var id: Kind { kind }
}

public struct HealthThresholds: Equatable, Sendable {
    public var heapWarningBytes: UInt64 = 80_000
    public var heapCriticalBytes: UInt64 = 40_000
    public var largestBlockWarningBytes: UInt64 = 16_384
    public var stackWarningBytes: UInt64 = 2_048
    public var stackCriticalBytes: UInt64 = 1_024
    public var maxClients: Int = 4

    public init() {}
}

/// Turns two consecutive `/api/realtime/status` readings into findings.
/// Counter checks use deltas so a historical drop does not stay red forever.
public enum HealthEvaluator {
    public static let abnormalResetReasons: Set<String> = ["PANIC", "INT_WDT", "TASK_WDT", "WDT", "BROWNOUT"]

    public static func evaluate(current: RealtimeStatus, previous: RealtimeStatus?,
                                thresholds: HealthThresholds = HealthThresholds()) -> [HealthFinding] {
        var findings: [HealthFinding] = []

        if current.clockSynced == false {
            findings.append(.init(kind: .clockUnsynced, level: .warning,
                                  message: "Reloj UTC sin sincronizar: el ESP32 no emite tramas sentinel.v1 hasta que un gateway o esta app con token lo sincronice."))
        }

        if let heap = current.freeHeapBytes {
            if heap < thresholds.heapCriticalBytes {
                findings.append(.init(kind: .heapLow, level: .critical, message: "Heap libre crítico: \(heap) B."))
            } else if heap < thresholds.heapWarningBytes {
                findings.append(.init(kind: .heapLow, level: .warning, message: "Heap libre bajo: \(heap) B."))
            }
        }

        if let block = current.largestFreeBlockBytes, block < thresholds.largestBlockWarningBytes {
            findings.append(.init(kind: .heapFragmented, level: .warning,
                                  message: "Bloque libre máximo pequeño (\(block) B): posible fragmentación."))
        }

        if let stack = current.minLoopStackFreeBytes {
            if stack < thresholds.stackCriticalBytes {
                findings.append(.init(kind: .stackLow, level: .critical, message: "Pila del loop casi agotada: \(stack) B libres."))
            } else if stack < thresholds.stackWarningBytes {
                findings.append(.init(kind: .stackLow, level: .warning, message: "Margen de pila del loop bajo: \(stack) B."))
            }
        }

        if let reason = current.resetReason, abnormalResetReasons.contains(reason) {
            findings.append(.init(kind: .abnormalReset, level: .critical,
                                  message: "El último reinicio fue anómalo (\(reason))."))
        }

        if let clients = current.clientCount, clients >= thresholds.maxClients {
            findings.append(.init(kind: .clientsSaturated, level: .info,
                                  message: "Todos los slots WebSocket (\(clients)) están ocupados."))
        }

        if let previous {
            let rebooted = (previous.bootId != nil && current.bootId != nil && previous.bootId != current.bootId)
                || (previous.uptimeMs ?? 0) > (current.uptimeMs ?? UInt64.max)
            if rebooted {
                findings.append(.init(kind: .rebootDetected, level: .warning,
                                      message: "El ESP32 se reinició (bootId o uptime cambió)."))
            } else {
                if delta(current.droppedFrames, previous.droppedFrames) > 0 {
                    findings.append(.init(kind: .framesDropped, level: .warning,
                                          message: "Se descartaron tramas WebSocket desde la última lectura."))
                }
                if delta(current.rejectedMessages, previous.rejectedMessages) > 0 {
                    findings.append(.init(kind: .messagesRejected, level: .info,
                                          message: "El firmware rechazó mensajes entrantes."))
                }
                if delta(current.droppedTelemetry, previous.droppedTelemetry) > 0 {
                    findings.append(.init(kind: .telemetryDropped, level: .warning,
                                          message: "El firmware no pudo construir telemetría."))
                }
                if delta(current.handshakeTimeouts, previous.handshakeTimeouts) > 0 {
                    findings.append(.init(kind: .handshakeTimeouts, level: .info,
                                          message: "Conexiones sin handshake cerradas por timeout."))
                }
            }
        }
        return findings
    }

    public static func overall(_ findings: [HealthFinding]) -> HealthLevel {
        findings.map(\.level).max() ?? .ok
    }

    private static func delta(_ current: UInt64?, _ previous: UInt64?) -> UInt64 {
        guard let current, let previous, current >= previous else { return 0 }
        return current - previous
    }
}
