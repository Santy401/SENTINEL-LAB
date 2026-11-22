import Foundation
import XCTest
@testable import SentinelKit

final class PhysicsTests: XCTestCase {
    private func reading(_ t: TimeInterval, lin: Vec3, gravity: Vec3 = Vec3(x: 0, y: 0, z: -9.80665)) -> MotionReading {
        MotionReading(time: t, date: Date(timeIntervalSince1970: 1_795_277_402 + t), linear: lin, gravity: gravity)
    }

    func testUnitConversionAndForce() {
        let r = MotionReading.fromG(time: 0, date: Date(), userAccelerationG: Vec3(x: 1, y: 0, z: 0),
                                    gravityG: Vec3(x: 0, y: 0, z: -1), rotationRate: .zero)
        XCTAssertEqual(r.linear.x, 9.80665, accuracy: 1e-9)
        XCTAssertEqual(r.total.magnitude, (2 * 9.80665 * 9.80665).squareRoot(), accuracy: 1e-9)
        // F = m·a: 164 g at 10 m/s² → 1.64 N.
        XCTAssertEqual(Physics.newtons(accelerationMs2: 10), 1.64, accuracy: 1e-12)
        XCTAssertEqual(Physics.intensity(linearMs2: 4), 0.5, accuracy: 1e-12)
        XCTAssertEqual(Physics.intensity(linearMs2: 80), 1)
        XCTAssertEqual(Physics.intensity(linearMs2: .nan), 0)
    }

    func testClassifierThresholds() {
        XCTAssertEqual(ShakeClassifier.state(filteredMs2: 0.2), .stable)
        XCTAssertEqual(ShakeClassifier.state(filteredMs2: 0.5), .movement)
        XCTAssertEqual(ShakeClassifier.state(filteredMs2: 3.99), .movement)
        XCTAssertEqual(ShakeClassifier.state(filteredMs2: 4.0), .alert)
        XCTAssertEqual(ShakeClassifier.state(filteredMs2: .nan), .sensorOffline)
    }

    func testRmsWindowStatsAndRate() {
        var analyzer = ShakeAnalyzer(windowSeconds: 0.5)
        // 100 Hz for 1 s at rest, then a 3 m/s² constant shake for 1 s.
        for i in 0..<100 { analyzer.feed(reading(Double(i) * 0.01, lin: Vec3(x: 0.01, y: 0, z: 0))) }
        XCTAssertEqual(analyzer.metrics.state, .stable)
        XCTAssertEqual(analyzer.metrics.sampleRateHz, 100, accuracy: 1)
        for i in 100..<200 { analyzer.feed(reading(Double(i) * 0.01, lin: Vec3(x: 0, y: 3, z: 0))) }
        let m = analyzer.metrics
        XCTAssertEqual(m.rawMs2, 3, accuracy: 1e-9)
        XCTAssertEqual(m.rmsMs2, 3, accuracy: 1e-9) // window fully inside the shake
        XCTAssertEqual(m.state, .movement)
        XCTAssertEqual(m.sessionMaxMs2, 3, accuracy: 1e-9)
        XCTAssertEqual(m.sessionMinMs2, 0.01, accuracy: 1e-9)
        XCTAssertGreaterThan(m.sessionAverageMs2, 0.01)
        XCTAssertLessThan(m.sessionAverageMs2, 3)
        XCTAssertEqual(m.sampleCount, 200)
        XCTAssertEqual(m.forceNewtons, 0.164 * 3, accuracy: 1e-9)
        analyzer.reset()
        XCTAssertEqual(analyzer.metrics, ShakeMetrics())
    }

    func testSingleSpikeIsSmoothedButPeakKept() {
        var analyzer = ShakeAnalyzer(windowSeconds: 0.5)
        for i in 0..<50 { analyzer.feed(reading(Double(i) * 0.01, lin: .zero)) }
        analyzer.feed(reading(0.5, lin: Vec3(x: 20, y: 0, z: 0)))
        XCTAssertEqual(analyzer.metrics.rawMs2, 20, accuracy: 1e-9)
        XCTAssertLessThan(analyzer.metrics.rmsMs2, 4) // one sample in ~51 → RMS ≈ 2.8
        XCTAssertEqual(analyzer.metrics.windowPeakMs2, 20, accuracy: 1e-9)
        XCTAssertEqual(analyzer.metrics.sessionPeakMs2, 20, accuracy: 1e-9)
    }

    func testPeakHoldSendsRealLargestSamplePerWindow() {
        var window = PeakHoldWindow(period: 0.2)
        var sent: [MotionReading] = []
        let magnitudes: [Double] = [0.1, 5.0, 0.2, 0.3, 0.1, 0.1, 7.0, 0.1, 0.1, 0.1, 0.1, 0.1]
        for (i, value) in magnitudes.enumerated() {
            if let out = window.offer(reading(Double(i) * 0.05, lin: Vec3(x: value, y: 0, z: 0))) { sent.append(out) }
        }
        // Windows: [0,0.2) → peak 5.0, [0.2,0.4) → 7.0, [0.4,0.6) → 0.1
        XCTAssertEqual(sent.map { $0.linear.x }, [5.0, 7.0])
        XCTAssertTrue(sent.allSatisfy { s in magnitudes.contains(s.linear.x) })
    }

    func testMotionSampleEnvelopeRoundTripsThroughStrictDecoder() throws {
        var session = try ClientSession(sessionId: "ios-test-session")
        let r = MotionReading.fromG(time: 1, date: Date(timeIntervalSince1970: 1_795_277_402.123),
                                    userAccelerationG: Vec3(x: 0.123456, y: -0.5, z: 0.01),
                                    gravityG: Vec3(x: 0, y: -0.7071, z: -0.7071), rotationRate: .zero)
        let payload = OutboundMotionSample(deviceId: "iphone-abcdef01", sampleIndex: 42, reading: r, filteredMs2: 4.5)
        let data = try session.motionSample(payload, at: Date(timeIntervalSince1970: 1_795_277_402.2))
        XCTAssertLessThanOrEqual(data.count, firmwareMaxFrameBytes)
        let message = try ProtocolDecoder.decode(data)
        XCTAssertEqual(message.type, "motion.sample")
        XCTAssertEqual(message.sequence, 1)
        guard case .motionSample(let sample) = message.body else { return XCTFail("not a motion sample") }
        XCTAssertEqual(sample.deviceId, "iphone-abcdef01")
        XCTAssertEqual(sample.sourceType, .coreMotion)
        XCTAssertEqual(sample.sampleIndex, 42)
        XCTAssertEqual(sample.state, .alert)
        XCTAssertEqual(sample.linearAcceleration.x, (0.123456 * 9.80665 * 10_000).rounded() / 10_000, accuracy: 1e-9)
        XCTAssertEqual(sample.acceleration.y, ((-0.5 - 0.7071) * 9.80665 * 10_000).rounded() / 10_000, accuracy: 1e-9)
        XCTAssertNotNil(sample.sensorTimestamp)

        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let body = try XCTUnwrap(json["payload"] as? [String: Any])
        XCTAssertEqual(Set(body.keys), ["deviceId", "sourceType", "sampleIndex", "sensorTimestamp", "acceleration",
                                        "gravity", "linearAcceleration", "state", "simulated"])
        XCTAssertEqual(body["simulated"] as? Bool, false)
    }

    func testMotionSampleRejectsOutOfRangeVectors() throws {
        var session = try ClientSession(sessionId: "ios-test-session")
        let bad = MotionReading(time: 0, date: Date(), linear: Vec3(x: 900, y: 0, z: 0), gravity: .zero)
        let payload = OutboundMotionSample(deviceId: "iphone-abcdef01", sampleIndex: 1, reading: bad, filteredMs2: 1)
        XCTAssertThrowsError(try session.motionSample(payload, at: Date()))
        XCTAssertEqual(session.sequence, 0)
    }
}

final class DistanceModelTests: XCTestCase {
    func testEstimateInverseAndMaxRange() throws {
        let model = LogDistanceModel.uncalibrated // A = −40, n = 2.7
        XCTAssertEqual(try XCTUnwrap(model.distance(rssiDbm: -40)), 1, accuracy: 1e-9)
        XCTAssertEqual(try XCTUnwrap(model.distance(rssiDbm: -67)), 10, accuracy: 1e-9)
        XCTAssertEqual(try XCTUnwrap(model.rssi(atMeters: 10)), -67, accuracy: 1e-9)
        // −90 dBm: 10^(50/27) ≈ 71.2 m (theoretical, open space assumptions).
        XCTAssertEqual(try XCTUnwrap(model.theoreticalMaxRange()), pow(10, 50.0 / 27.0), accuracy: 1e-9)
        XCTAssertNil(model.distance(rssiDbm: 0))
        XCTAssertNil(model.distance(rssiDbm: -120))
        XCTAssertNil(LogDistanceModel(referenceRssiAt1m: -40, exponent: 0.1).distance(rssiDbm: -60))
    }

    func testUncertaintyBandContainsEstimate() throws {
        let model = LogDistanceModel.uncalibrated
        let band = try XCTUnwrap(model.distanceBand(rssiDbm: -70))
        let estimate = try XCTUnwrap(model.distance(rssiDbm: -70))
        XCTAssertTrue(band.contains(estimate))
        XCTAssertEqual(band.lowerBound, pow(10, 30.0 / 35.0), accuracy: 1e-9)
        XCTAssertEqual(band.upperBound, pow(10, 30.0 / 20.0), accuracy: 1e-9)
    }

    func testFitRecoversModelLikeFirmware() throws {
        let truth = LogDistanceModel(referenceRssiAt1m: -45, exponent: 3.0)
        let points = [1.0, 2.0, 4.0].map { (meters: $0, rssiDbm: truth.rssi(atMeters: $0)!) }
        let fitted = try XCTUnwrap(LogDistanceModel.fit(points))
        XCTAssertEqual(fitted.referenceRssiAt1m, -45, accuracy: 1e-9)
        XCTAssertEqual(fitted.exponent, 3.0, accuracy: 1e-9)
        XCTAssertNil(LogDistanceModel.fit([(1, -40), (1, -42)]))
        XCTAssertNil(LogDistanceModel.fit([(1, -40)]))
    }

    func testSessionTrackerKeepsKindsApart() {
        var tracker = SessionDistanceTracker()
        tracker.record(meters: 3, calibrated: false)
        tracker.record(meters: 2, calibrated: true)
        tracker.record(meters: 9, calibrated: false)
        tracker.record(meters: .nan, calibrated: true)
        XCTAssertEqual(tracker.maxOrientativeM, 9)
        XCTAssertEqual(tracker.maxCalibratedM, 2)
        tracker.reset()
        XCTAssertNil(tracker.maxOrientativeM)
    }

    func testThisDeviceResolver() {
        XCTAssertEqual(ThisDeviceResolver.resolve(remembered: nil, connectedClientIds: [7]), 7)
        XCTAssertNil(ThisDeviceResolver.resolve(remembered: nil, connectedClientIds: [7, 8]))
        XCTAssertEqual(ThisDeviceResolver.resolve(remembered: 8, connectedClientIds: [7, 8]), 8)
        XCTAssertEqual(ThisDeviceResolver.resolve(remembered: 9, connectedClientIds: [7]), 7)
    }
}

final class LabAndStatusModelTests: XCTestCase {
    func testRealtimeStatusDecodesRgbAndPhoneMotionLeniently() throws {
        let json = """
        {"fw":"0.3.1","firmwareVersion":"0.5.0","rgb":{"enabled":true,"pin":48,"mode":"motion","r":255,"g":120,"b":0},
         "phoneMotion":{"active":true,"deviceId":"iphone-abcdef01","linearAccelMs2":3.2,"intensity":0.4,"peakMs2":9.1,
         "samples":120,"sampleAgeMs":180},"futureField":1}
        """
        let status = try JSONDecoder().decode(RealtimeStatus.self, from: Data(json.utf8))
        XCTAssertEqual(status.rgb?.pin, 48)
        XCTAssertEqual(status.rgb?.modeLabel, "Movimiento del iPhone")
        XCTAssertEqual(status.rgb?.unitColor?.red, 1)
        XCTAssertEqual(status.phoneMotion?.peakMs2, 9.1)
        let old = try JSONDecoder().decode(RealtimeStatus.self, from: Data(#"{"fw":"0.3.1"}"#.utf8))
        XCTAssertNil(old.rgb)
        XCTAssertNil(old.phoneMotion)
    }

    func testLabSnapshotModel() throws {
        let json = """
        {"bootId":"b","storage":"RAM; export before restart","experiments":[
          {"id":1,"name":"a","clientId":77,"environment":1,"calibrated":false,"n":null,"referenceRssi":null,"currentEstimateM":null},
          {"id":2,"name":"b","clientId":77,"environment":1,"calibrated":true,"n":2.9,"referenceRssi":-43.5,"currentEstimateM":3.1}],
         "measurements":[{"id":1,"experimentId":2,"clientId":77,"timeMs":10,"actualM":1,"connected":true,"calibration":true,
          "notes":"","rssiDbm":-44,"filteredDbm":-43.6,"estimatedM":null,"errorM":null,"x":null,"y":null}]}
        """
        let lab = try JSONDecoder().decode(LabSnapshot.self, from: Data(json.utf8))
        let experiment = try XCTUnwrap(lab.calibratedExperiment(clientId: 77))
        XCTAssertEqual(experiment.id, 2)
        XCTAssertEqual(try XCTUnwrap(experiment.model?.exponent), 2.9)
        XCTAssertNil(lab.calibratedExperiment(clientId: 5))
        XCTAssertEqual(lab.measurements?.first?.filteredDbm, -43.6)
        let error = try JSONDecoder().decode(LabErrorBody.self,
                                             from: Data(#"{"ok":false,"error":"RSSI unavailable"}"#.utf8))
        XCTAssertEqual(error.error, "RSSI unavailable")
    }
}
