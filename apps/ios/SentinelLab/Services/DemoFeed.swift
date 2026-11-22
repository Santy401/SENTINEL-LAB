import Foundation
import SentinelKit

/// Synthetic data for demo mode only. Frames are generated locally, use the
/// `demo-generator` source, and every screen shows a DEMO label while this
/// feed is active. Nothing here is ever presented as a measurement.
struct DemoFeed {
    static let source = "demo-generator"
    private(set) var sequence: UInt64 = 0
    private let bootId = "demo-boot"
    private let started = Date()

    mutating func frames(at date: Date) -> [Data] {
        let uptime = UInt64(max(0, date.timeIntervalSince(started)) * 1_000)
        let phase = date.timeIntervalSince1970
        let rssiA = Int(-55 + 6 * sin(phase / 7))
        let rssiB = Int(-72 + 8 * sin(phase / 11 + 1))
        var frames = [frame("rf.telemetry", date, """
        {"bootId":"\(bootId)","firmwareVersion":"0.4.1","channel":1,"clientCount":2,"clients":[\
        {"clientId":1,"connected":true,"rssiDbm":\(rssiA),"filteredRssiDbm":\(rssiA - 1),"lastSeen":"\(ProtocolTimestamp.format(date))"},\
        {"clientId":2,"connected":true,"rssiDbm":\(rssiB),"filteredRssiDbm":\(rssiB),"lastSeen":"\(ProtocolTimestamp.format(date))"}],\
        "simulated":false}
        """)]
        if sequence % 5 == 1 {
            frames.append(frame("system.status", date, """
            {"firmwareVersion":"0.4.1","bootId":"\(bootId)","uptimeMs":\(uptime),"operationalState":"MONITORING",\
            "transports":["http","websocket"],"capabilities":["rf.monitor","laboratory"],"freeHeapBytes":214000,"simulated":false}
            """))
        }
        if rssiB < -78, sequence % 7 == 0 {
            frames.append(frame("event", date, """
            {"eventId":"demo-\(sequence)","name":"SIGNAL_WEAK","occurredAt":"\(ProtocolTimestamp.format(date))",\
            "details":{"clientId":2,"rssiDbm":\(rssiB)},"simulated":false}
            """))
        }
        return frames
    }

    static func status(at date: Date, started: Date) -> RealtimeStatus {
        var status = RealtimeStatus()
        status.fw = "0.3.1"
        status.firmwareVersion = "0.4.1"
        status.httpApiVersion = "0.3.1"
        status.realtimeVersion = "0.4.1"
        status.bootId = "demo-boot"
        status.uptimeMs = UInt64(max(0, date.timeIntervalSince(started)) * 1_000)
        status.clockSynced = true
        status.clockSource = "peer"
        status.clientCount = 1
        status.freeHeapBytes = 214_000
        status.minFreeHeapBytes = 209_500
        status.largestFreeBlockBytes = 110_592
        status.minLoopStackFreeBytes = 4_200
        status.resetReason = "POWERON"
        status.droppedFrames = 0
        status.rejectedMessages = 0
        status.droppedTelemetry = 0
        status.handshakeTimeouts = 0
        status.idleTimeouts = 0
        return status
    }

    private mutating func frame(_ type: String, _ date: Date, _ payload: String) -> Data {
        sequence += 1
        return Data("""
        {"protocolVersion":"1.0.0","type":"\(type)","timestamp":"\(ProtocolTimestamp.format(date))",\
        "sequence":\(sequence),"source":"\(Self.source)","sessionId":"demo-session","payload":\(payload)}
        """.utf8)
    }
}
