import Foundation
import XCTest
@testable import SentinelKit

final class ClientSessionTests: XCTestCase {
    private let date = Date(timeIntervalSince1970: 1_795_277_403.6)

    private func int(_ value: Any?) -> Int? {
        if let value = value as? Int { return value }
        return (value as? NSNumber)?.intValue
    }

    private func object(_ data: Data) throws -> [String: Any] {
        try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    func testHelloMatchesContractAndFirmwareRules() throws {
        var session = try ClientSession(sessionId: "ios-test-0001")
        let data = try session.hello(deviceId: "iphone-12-lab", at: date)
        XCTAssertLessThanOrEqual(data.count, firmwareMaxFrameBytes)
        let json = try object(data)
        XCTAssertEqual(json["protocolVersion"] as? String, "1.0.0")
        XCTAssertEqual(json["type"] as? String, "connection.hello")
        XCTAssertEqual(json["timestamp"] as? String, "2026-11-21T16:10:03.600Z")
        XCTAssertEqual(int(json["sequence"]), 1)
        XCTAssertEqual(json["source"] as? String, "ios-app")
        XCTAssertEqual(json["sessionId"] as? String, "ios-test-0001")
        let payload = try XCTUnwrap(json["payload"] as? [String: Any])
        XCTAssertEqual(payload["role"] as? String, "ios")
        XCTAssertEqual(payload["supportedVersion"] as? String, "1.0.0")
        XCTAssertEqual(Set(payload.keys), ["deviceId", "role", "supportedVersion", "capabilities"])

        // The frame we send must also pass our own strict decoder.
        let decoded = try ProtocolDecoder.decode(data)
        XCTAssertEqual(decoded.body, .hello(HelloPayload(deviceId: "iphone-12-lab", role: .ios,
                                                         supportedVersion: "1.0.0",
                                                         capabilities: ClientSession.capabilities)))
    }

    func testSequenceStrictlyIncreases() throws {
        var session = try ClientSession()
        _ = try session.hello(deviceId: "dev1", at: date)
        let heartbeat = try session.heartbeat(bootId: "app-boot-1", uptimeMs: 1_000, at: date)
        let goodbye = try session.goodbye(reason: .clientRequest, at: date)
        XCTAssertEqual(int(try object(heartbeat)["sequence"]), 2)
        XCTAssertEqual(int(try object(goodbye)["sequence"]), 3)
        XCTAssertEqual(session.sequence, 3)
        let decoded = try ProtocolDecoder.decode(goodbye)
        XCTAssertEqual(decoded.body, .goodbye(GoodbyePayload(reason: .clientRequest)))
    }

    func testRejectsInvalidIdentity() {
        XCTAssertThrowsError(try ClientSession(source: "iOS-App"))
        XCTAssertThrowsError(try ClientSession(sessionId: ""))
        XCTAssertThrowsError(try ClientSession(sessionId: String(repeating: "a", count: 65)))
        var session = try? ClientSession()
        XCTAssertThrowsError(try session?.hello(deviceId: "bad id", at: date))
        XCTAssertEqual(session?.sequence, 0, "failed frame must not consume a sequence number")
    }

    func testGeneratedSessionIdsAreValidAndUnique() throws {
        let first = ClientSession.makeSessionId()
        let second = ClientSession.makeSessionId()
        XCTAssertNotEqual(first, second)
        XCTAssertNoThrow(try ClientSession(sessionId: first))
        XCTAssertLessThanOrEqual(first.count, 64)
    }
}

final class BackoffTests: XCTestCase {
    func testNominalGrowthIsCapped() {
        let backoff = ExponentialBackoff(initial: 1, maximum: 30, multiplier: 2, jitter: 0)
        XCTAssertEqual((1...7).map { backoff.nominalDelay(attempt: $0) }, [1, 2, 4, 8, 16, 30, 30])
        XCTAssertEqual(backoff.nominalDelay(attempt: 10_000), 30)
    }

    func testJitterIsBoundedAndDeterministic() {
        let backoff = ExponentialBackoff(initial: 1, maximum: 30, multiplier: 2, jitter: 0.2)
        var a = SplitMix64(seed: 42)
        var b = SplitMix64(seed: 42)
        for attempt in 1...20 {
            let first = backoff.delay(attempt: attempt, using: &a)
            let second = backoff.delay(attempt: attempt, using: &b)
            XCTAssertEqual(first, second)
            let nominal = backoff.nominalDelay(attempt: attempt)
            XCTAssertGreaterThanOrEqual(first, nominal * 0.8 - 1e-9)
            XCTAssertLessThanOrEqual(first, min(30, nominal * 1.2) + 1e-9)
        }
    }
}

final class ConnectionStateMachineTests: XCTestCase {
    private func machine() -> ConnectionStateMachine {
        ConnectionStateMachine(backoff: ExponentialBackoff(initial: 1, maximum: 8, jitter: 0),
                               generator: SplitMix64(seed: 1))
    }

    func testHappyPath() {
        var sm = machine()
        XCTAssertEqual(sm.handle(.start), [.openTransport])
        XCTAssertEqual(sm.phase, .connecting(attempt: 1))
        XCTAssertEqual(sm.handle(.transportOpened), [])
        XCTAssertEqual(sm.phase, .awaitingData)
        sm.handle(.frameReceived)
        XCTAssertEqual(sm.phase, .streaming)
    }

    func testFailuresBackOffThenRecover() {
        var sm = machine()
        sm.handle(.start)
        XCTAssertEqual(sm.handle(.transportFailed("refused")), [.closeTransport, .scheduleRetry(1)])
        XCTAssertEqual(sm.phase, .waitingToRetry(attempt: 2, delay: 1))
        XCTAssertEqual(sm.handle(.retryTimerFired), [.openTransport])
        XCTAssertEqual(sm.handle(.transportFailed("refused")), [.closeTransport, .scheduleRetry(2)])
        sm.handle(.retryTimerFired)
        XCTAssertEqual(sm.handle(.transportFailed("refused")), [.closeTransport, .scheduleRetry(4)])
        sm.handle(.retryTimerFired)
        sm.handle(.transportOpened)
        sm.handle(.frameReceived)
        XCTAssertEqual(sm.phase, .streaming)
        XCTAssertEqual(sm.failures, 3)
        XCTAssertEqual(sm.lastFailure, "refused")
        // Attempt counter resets after a successful stream.
        XCTAssertEqual(sm.handle(.transportFailed("reset")), [.closeTransport, .scheduleRetry(1)])
    }

    func testReconnectCountedAfterStreamResumes() {
        var sm = machine()
        sm.handle(.start)
        sm.handle(.transportOpened)
        sm.handle(.frameReceived)
        sm.handle(.transportFailed("timeout"))
        sm.handle(.retryTimerFired)
        sm.handle(.transportOpened)
        sm.handle(.frameReceived)
        XCTAssertEqual(sm.reconnects, 1)
    }

    func testNetworkLossAndRestore() {
        var sm = machine()
        sm.handle(.start)
        sm.handle(.transportOpened)
        XCTAssertEqual(sm.handle(.networkLost), [.cancelRetry, .closeTransport])
        XCTAssertEqual(sm.phase, .offline)
        XCTAssertEqual(sm.handle(.retryTimerFired), [], "stale timer ignored while offline")
        XCTAssertEqual(sm.handle(.networkRestored), [.openTransport])
        XCTAssertEqual(sm.phase, .connecting(attempt: 1))
    }

    func testStopIgnoresLateCallbacks() {
        var sm = machine()
        sm.handle(.start)
        XCTAssertEqual(sm.handle(.stop), [.cancelRetry, .closeTransport])
        XCTAssertEqual(sm.handle(.transportFailed("late")), [])
        XCTAssertEqual(sm.handle(.frameReceived), [])
        XCTAssertEqual(sm.handle(.networkLost), [])
        XCTAssertEqual(sm.phase, .idle)
        XCTAssertEqual(sm.handle(.stop), [])
    }
}

final class HistoryTests: XCTestCase {
    func testRingBufferOverwritesOldest() {
        var ring = RingBuffer<Int>(capacity: 3)
        XCTAssertTrue(ring.isEmpty)
        for value in 1...5 { ring.append(value) }
        XCTAssertEqual(ring.elements, [3, 4, 5])
        XCTAssertEqual(ring.count, 3)
        XCTAssertEqual(ring.last, 5)
        XCTAssertEqual(Array(ring), [3, 4, 5])
        ring.removeAll()
        XCTAssertTrue(ring.isEmpty)
        XCTAssertNil(ring.last)
    }

    func testRssiHistoryIsBounded() {
        var history = RssiHistory(maxClients: 2, samplesPerClient: 4)
        let base = Date(timeIntervalSince1970: 0)
        for i in 0..<10 {
            history.record(clientId: 1, rssiDbm: -50 - i, filteredRssiDbm: nil, at: base.addingTimeInterval(Double(i)))
        }
        history.record(clientId: 2, rssiDbm: -60, filteredRssiDbm: -61, at: base.addingTimeInterval(20))
        XCTAssertEqual(history.points(for: 1).map(\.rssiDbm), [-56, -57, -58, -59])
        // A third client evicts the least recently updated one (client 1).
        history.record(clientId: 3, rssiDbm: -70, filteredRssiDbm: nil, at: base.addingTimeInterval(30))
        XCTAssertEqual(history.clientIds, [2, 3])
        XCTAssertLessThanOrEqual(history.totalSamples, 8)
    }

    func testTelemetryRecordsDisconnectedAsGap() throws {
        let message = try ProtocolDecoder.decode(Fixtures.data("firmware", "firmware-rf.json"))
        guard case .rfTelemetry(let rf) = message.body else { return XCTFail("not rf") }
        var history = RssiHistory()
        history.record(rf, at: message.timestamp)
        XCTAssertEqual(history.points(for: 3).first?.rssiDbm, -54)
        XCTAssertEqual(history.points(for: 4).count, 1)
        XCTAssertNil(history.points(for: 4).first?.rssiDbm)
    }
}

final class HealthTests: XCTestCase {
    private func status(_ configure: (inout RealtimeStatus) -> Void) -> RealtimeStatus {
        var value = RealtimeStatus()
        value.bootId = "abc"
        value.uptimeMs = 10_000
        value.clockSynced = true
        value.freeHeapBytes = 215_132
        value.droppedFrames = 0
        configure(&value)
        return value
    }

    func testHealthyDeviceHasNoFindings() {
        let findings = HealthEvaluator.evaluate(current: status { _ in }, previous: status { $0.uptimeMs = 5_000 })
        XCTAssertEqual(findings, [])
        XCTAssertEqual(HealthEvaluator.overall(findings), .ok)
    }

    func testCriticalConditions() {
        let current = status {
            $0.clockSynced = false
            $0.freeHeapBytes = 30_000
            $0.minLoopStackFreeBytes = 512
            $0.resetReason = "TASK_WDT"
        }
        let kinds = Set(HealthEvaluator.evaluate(current: current, previous: nil).map(\.kind))
        XCTAssertEqual(kinds, [.clockUnsynced, .heapLow, .stackLow, .abnormalReset])
        XCTAssertEqual(HealthEvaluator.overall(HealthEvaluator.evaluate(current: current, previous: nil)), .critical)
    }

    func testCounterDeltasAndReboot() {
        let previous = status { $0.droppedFrames = 2 }
        let grew = status { $0.droppedFrames = 5; $0.uptimeMs = 20_000 }
        XCTAssertTrue(HealthEvaluator.evaluate(current: grew, previous: previous).contains { $0.kind == .framesDropped })
        let same = status { $0.droppedFrames = 2; $0.uptimeMs = 20_000 }
        XCTAssertFalse(HealthEvaluator.evaluate(current: same, previous: previous).contains { $0.kind == .framesDropped })
        let rebooted = status { $0.bootId = "def"; $0.uptimeMs = 100 }
        let kinds = HealthEvaluator.evaluate(current: rebooted, previous: previous).map(\.kind)
        XCTAssertTrue(kinds.contains(.rebootDetected))
        XCTAssertFalse(kinds.contains(.framesDropped), "counters reset on reboot are not drops")
    }

    func testRealtimeStatusDecodesOldAndNewFirmware() throws {
        let old = #"{"fw":"0.3.1","realtimeVersion":"0.4.0","bootId":"a1","uptimeMs":5,"clockSynced":false,"clientCount":0,"freeHeapBytes":215132,"minFreeHeapBytes":214000,"heapSamples":3,"droppedFrames":0,"rejectedMessages":0,"droppedTelemetry":0,"skippedSimulatedEvents":0}"#
        let decodedOld = try JSONDecoder().decode(RealtimeStatus.self, from: Data(old.utf8))
        XCTAssertEqual(decodedOld.imageVersion, "0.4.0")
        XCTAssertNil(decodedOld.resetReason)
        let new = #"{"fw":"0.3.1","firmwareVersion":"0.4.1","httpApiVersion":"0.3.1","realtimeVersion":"0.4.1","clockSynced":true,"clockSource":"peer","clockSyncAgeMs":1200,"resetReason":"POWERON","largestFreeBlockBytes":110000,"minLoopStackFreeBytes":4096,"handshakeTimeouts":1,"idleTimeouts":0}"#
        let decodedNew = try JSONDecoder().decode(RealtimeStatus.self, from: Data(new.utf8))
        XCTAssertEqual(decodedNew.imageVersion, "0.4.1")
        XCTAssertEqual(decodedNew.clockSource, "peer")
        let nullAge = #"{"clockSynced":false,"clockSyncAgeMs":null}"#
        XCTAssertNil(try JSONDecoder().decode(RealtimeStatus.self, from: Data(nullAge.utf8)).clockSyncAgeMs)
    }
}

final class EvidenceTests: XCTestCase {
    func testExportNeverContainsToken() throws {
        let token = "s3cr3t-token-value-123456"
        var status = RealtimeStatus()
        status.bootId = "boot-1"
        let report = EvidenceReport(
            generatedAt: Date(timeIntervalSince1970: 0), appVersion: "1.0 (1)", demo: false, host: "192.168.4.1",
            realtimeStatus: status, deviceState: nil,
            connection: .init(phase: "streaming", failures: 1, reconnects: 0,
                              lastFailure: "HTTP 401 Authorization: Bearer \(token)", tokenConfigured: true),
            framesByType: ["rf.telemetry": 3], decodeFailures: 0, health: [], recentEvents: [])
        let data = try EvidenceExporter.export(report, secrets: [token])
        let text = String(decoding: data, as: UTF8.self)
        XCTAssertFalse(text.contains(token))
        XCTAssertTrue(text.contains(EvidenceExporter.redactedMarker))
        let decoded = try JSONDecoder().decode(EvidenceReport.self, from: data)
        XCTAssertEqual(decoded.realtimeStatus?.bootId, "boot-1")
        XCTAssertFalse(decoded.demo)
        XCTAssertTrue(decoded.connection.tokenConfigured)
    }

    func testRedactBearerWithoutKnownSecret() {
        let text = EvidenceExporter.redact("Authorization: bearer abc.def-ghi", secrets: [])
        XCTAssertEqual(text, "Authorization: Bearer [REDACTED]")
    }
}
