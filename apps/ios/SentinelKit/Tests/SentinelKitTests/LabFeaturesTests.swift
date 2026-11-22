import Foundation
import XCTest
@testable import SentinelKit

final class BoardModelTests: XCTestCase {
    func testPartsMatchWebIds() {
        XCTAssertEqual(ESP32S3DevKitC1.parts.map(\.id),
                       ["module", "antenna", "usb", "uart", "rgb", "boot", "reset", "ldo", "bridge", "headers"])
        XCTAssertEqual(ESP32S3DevKitC1.part("rgb")?.name, "LED RGB WS2812 (GPIO48)")
        XCTAssertNil(ESP32S3DevKitC1.part("nope"))
    }

    func testEveryPartHasGeometry() {
        let parts = Set(ESP32S3DevKitC1.primitives.compactMap(\.part))
        XCTAssertEqual(parts, Set(ESP32S3DevKitC1.parts.map(\.id)))
    }

    func testHeadersAndDimensions() {
        let pins = ESP32S3DevKitC1.primitives.filter { $0.part == "headers" && $0.material == .gold }
        XCTAssertEqual(pins.count, 44)
        let pcb = ESP32S3DevKitC1.primitives[0]
        XCTAssertEqual(pcb.shape, .box(width: 63, height: 1.6, length: 25.4))
        // Everything sits within the board footprint (USB-C overhangs < 1 mm).
        for p in ESP32S3DevKitC1.primitives {
            XCTAssertLessThanOrEqual(abs(p.x), 32.3, "\(p)")
            XCTAssertLessThanOrEqual(abs(p.z), 12.7, "\(p)")
        }
        let led = ESP32S3DevKitC1.primitives.first { $0.material == .led }
        XCTAssertEqual(led?.x ?? 0, ESP32S3DevKitC1.ledCenter.x, accuracy: 1e-9)
        XCTAssertEqual(led?.y ?? 0, ESP32S3DevKitC1.ledCenter.y, accuracy: 1e-9)
    }

    func testLedLevelAndLegend() {
        XCTAssertEqual(ESP32S3DevKitC1.ledLevel(r: 255, g: 255, b: 255), 1, accuracy: 1e-9)
        XCTAssertEqual(ESP32S3DevKitC1.ledLevel(r: 0, g: 0, b: 0), 0)
        XCTAssertEqual(Set(ESP32S3DevKitC1.rgbModeLegend.keys), ["boot", "offline", "client", "synced", "motion", "event"])
    }
}

final class AlertEngineTests: XCTestCase {
    let t0 = Date(timeIntervalSince1970: 1_800_000_000)

    func testStrongShakeIsEdgeTriggeredAndRateLimited() {
        var engine = AlertEngine(cooldown: 60)
        XCTAssertEqual(engine.evaluate(AlertInputs(now: t0, shakeMs2: 5)).map(\.kind), [.strongShake])
        // Still shaking: no repeat.
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0 + 1, shakeMs2: 6)).isEmpty)
        // Calm, then shake again within cooldown: suppressed.
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0 + 2, shakeMs2: 0.2)).isEmpty)
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0 + 10, shakeMs2: 5)).isEmpty)
        // After cooldown and a calm period it fires again.
        _ = engine.evaluate(AlertInputs(now: t0 + 70, shakeMs2: 0.1))
        XCTAssertEqual(engine.evaluate(AlertInputs(now: t0 + 71, shakeMs2: 4.5)).count, 1)
        XCTAssertEqual(engine.alerts.count, 2)
        XCTAssertEqual(engine.alerts.first?.kind, .strongShake)
    }

    func testOutOfRangeByRssiOrMissing() {
        var engine = AlertEngine()
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0, rssiDbm: -60)).isEmpty)
        XCTAssertEqual(engine.evaluate(AlertInputs(now: t0 + 1, rssiDbm: -90)).map(\.kind), [.outOfRange])
        var other = AlertEngine()
        let raised = other.evaluate(AlertInputs(now: t0, phoneMissing: true))
        XCTAssertEqual(raised.first?.kind, .outOfRange)
        XCTAssertTrue(raised.first?.message.contains("ya no ve") ?? false)
        // Unknown RSSI never raises.
        var unknown = AlertEngine()
        XCTAssertTrue(unknown.evaluate(AlertInputs(now: t0, rssiDbm: nil)).isEmpty)
    }

    func testRebootOnBootIdChangeIncludesReason() {
        var engine = AlertEngine()
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0, bootId: "aaaa1111")).isEmpty)
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0 + 2, bootId: "aaaa1111")).isEmpty)
        let raised = engine.evaluate(AlertInputs(now: t0 + 4, bootId: "bbbb2222", resetReason: "PANIC"))
        XCTAssertEqual(raised.map(\.kind), [.deviceReboot])
        XCTAssertTrue(raised[0].message.contains("PANIC"))
        XCTAssertEqual(raised[0].kind.severity, .critical)
    }

    func testLowHeap() {
        var engine = AlertEngine()
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0, freeHeapBytes: 200_000)).isEmpty)
        XCTAssertEqual(engine.evaluate(AlertInputs(now: t0 + 1, freeHeapBytes: 30_000)).map(\.kind), [.lowHeap])
    }

    func testClockUnsyncedNeedsGraceAndReachability() {
        var engine = AlertEngine()
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0, clockSynced: false)).isEmpty)
        XCTAssertTrue(engine.evaluate(AlertInputs(now: t0 + 60, clockSynced: false)).isEmpty)
        XCTAssertEqual(engine.evaluate(AlertInputs(now: t0 + 121, clockSynced: false)).map(\.kind), [.clockUnsynced])
        var offline = AlertEngine()
        _ = offline.evaluate(AlertInputs(now: t0, clockSynced: false, deviceReachable: false))
        XCTAssertTrue(offline.evaluate(AlertInputs(now: t0 + 500, clockSynced: false, deviceReachable: false)).isEmpty)
        // Synced resets the timer.
        var reset = AlertEngine()
        _ = reset.evaluate(AlertInputs(now: t0, clockSynced: false))
        _ = reset.evaluate(AlertInputs(now: t0 + 100, clockSynced: true))
        XCTAssertTrue(reset.evaluate(AlertInputs(now: t0 + 150, clockSynced: false)).isEmpty)
    }

    func testAlertListIsBounded() {
        var engine = AlertEngine(cooldown: 0)
        for i in 0..<120 {
            _ = engine.evaluate(AlertInputs(now: t0 + Double(i * 2), shakeMs2: 6))
            _ = engine.evaluate(AlertInputs(now: t0 + Double(i * 2 + 1), shakeMs2: 0))
        }
        XCTAssertEqual(engine.alerts.count, AlertEngine.maxAlerts)
    }
}

final class ActivityAndHapticsTests: XCTestCase {
    func testActivityStateCodableAndClamped() throws {
        let state = SentinelActivityState(connected: true, clockSynced: true, shakeIntensity: 1.7, shakeMs2: 9,
                                          ledRed: 62, ledGreen: 0, ledBlue: 0, ledMode: "motion",
                                          lastAlert: "Agitación fuerte", updatedAt: Date(timeIntervalSince1970: 1))
        XCTAssertEqual(state.shakeIntensity, 1)
        let data = try JSONEncoder().encode(state)
        XCTAssertEqual(try JSONDecoder().decode(SentinelActivityState.self, from: data), state)
    }

    func testActivityUpdateThrottle() {
        let a = SentinelActivityState(shakeIntensity: 0.30)
        XCTAssertFalse(a.differsMeaningfully(from: SentinelActivityState(shakeIntensity: 0.33, updatedAt: a.updatedAt)))
        XCTAssertTrue(a.differsMeaningfully(from: SentinelActivityState(shakeIntensity: 0.45)))
        XCTAssertTrue(a.differsMeaningfully(from: SentinelActivityState(connected: true, shakeIntensity: 0.30)))
    }

    func testHapticMappingFollowsFirmwareScale() {
        XCTAssertEqual(HapticMapping.intensity(shakeMs2: 0.2), 0)          // dead zone
        XCTAssertEqual(HapticMapping.intensity(shakeMs2: 8), 1, accuracy: 1e-9)
        XCTAssertEqual(HapticMapping.intensity(shakeMs2: 50), 1, accuracy: 1e-9)
        let mid = HapticMapping.intensity(shakeMs2: 4)
        XCTAssertGreaterThan(mid, 0.4)
        XCTAssertLessThan(mid, 0.5)
    }
}

final class AlertWebhookTests: XCTestCase {
    func testBodyMatchesGatewayShape() throws {
        let alert = LabAlert(id: "abc", kind: .strongShake, date: Date(timeIntervalSince1970: 1_795_373_386),
                             message: "Agitación de 9,4 m/s²")
        let data = AlertWebhookPayload.body(for: alert, bootId: "b00t", firmwareVersion: "0.5.0",
                                            metrics: ["shakeMs2": 9.4, "bad": .nan])
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["id"] as? String, "ios-abc")
        XCTAssertEqual(json["rule"] as? String, "strongShake")
        XCTAssertEqual(json["severity"] as? String, "critical")
        XCTAssertEqual(json["title"] as? String, "Agitación fuerte")
        XCTAssertEqual((json["occurredAt"] as? String)?.hasPrefix("2026-11-22T"), true)
        let metrics = try XCTUnwrap(json["metrics"] as? [String: Double])
        XCTAssertEqual(metrics, ["shakeMs2": 9.4])
        let device = try XCTUnwrap(json["device"] as? [String: Any])
        XCTAssertEqual(device["bootId"] as? String, "b00t")
        XCTAssertEqual(device["source"] as? String, "ios")
    }

    func testSignedMessageAndPolicy() {
        let body = Data("{\"a\":1}".utf8)
        XCTAssertEqual(String(decoding: AlertWebhookPayload.signedMessage(timestamp: 42, body: body), as: UTF8.self),
                       "42.{\"a\":1}")
        XCTAssertTrue(AlertWebhookPayload.isPermanentFailure(401))
        XCTAssertFalse(AlertWebhookPayload.isPermanentFailure(503))
        XCTAssertEqual(AlertWebhookPayload.backoff(attempt: 0), 5)
        XCTAssertEqual(AlertWebhookPayload.backoff(attempt: 20), 600)
    }
}
