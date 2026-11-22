import Foundation
import XCTest
@testable import SentinelKit

enum Fixtures {
    static func directory(_ name: String) throws -> URL {
        guard let root = Bundle.module.url(forResource: "Fixtures", withExtension: nil) else {
            throw XCTSkip("Fixtures resource missing; run apps/ios/scripts/sync-fixtures.sh")
        }
        return root.appendingPathComponent(name, isDirectory: true)
    }

    static func files(_ name: String) throws -> [URL] {
        let directory = try directory(name)
        return try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension == "json" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    static func data(_ name: String, _ file: String) throws -> Data {
        try Data(contentsOf: directory(name).appendingPathComponent(file))
    }
}

final class ProtocolDecodingTests: XCTestCase {
    func testEveryValidContractExampleDecodes() throws {
        let files = try Fixtures.files("valid")
        XCTAssertEqual(files.count, 9)
        for url in files {
            let message = try ProtocolDecoder.decode(Data(contentsOf: url))
            XCTAssertNotEqual(message.body, .unknown(type: message.type), url.lastPathComponent)
            XCTAssertEqual(url.deletingPathExtension().lastPathComponent, message.type)
        }
    }

    func testEveryFirmwareGeneratedFrameDecodes() throws {
        let files = try Fixtures.files("firmware")
        guard !files.isEmpty else { throw XCTSkip("firmware fixtures not generated") }
        for url in files {
            let data = try Data(contentsOf: url)
            XCTAssertLessThanOrEqual(data.count, firmwareMaxFrameBytes, url.lastPathComponent)
            let message = try ProtocolDecoder.decode(data)
            XCTAssertEqual(message.source, "esp32-s3-realtime")
            if case .unknown = message.body { XCTFail("unknown body for \(url.lastPathComponent)") }
        }
    }

    func testInvalidExamplesAreRejectedOrFlaggedUnknown() throws {
        let files = try Fixtures.files("invalid")
        XCTAssertEqual(files.count, 16)
        for url in files {
            let data = try Data(contentsOf: url)
            let name = url.lastPathComponent
            if name == "unknown-message-type.json" {
                // Forward compatibility: an unknown type is surfaced, never trusted.
                let message = try ProtocolDecoder.decode(data)
                XCTAssertEqual(message.body, .unknown(type: "motion.quantum"))
                continue
            }
            XCTAssertThrowsError(try ProtocolDecoder.decode(data), name)
        }
    }

    func testSpecificRejectionReasons() throws {
        func reason(_ file: String) throws -> ProtocolDecodingError? {
            do {
                _ = try ProtocolDecoder.decode(Fixtures.data("invalid", file))
                return nil
            } catch let error as ProtocolDecodingError {
                return error
            } catch {
                return nil
            }
        }
        XCTAssertEqual(try reason("duplicate-key.json"), .duplicateKey("sequence"))
        XCTAssertEqual(try reason("extra-envelope-field.json"), .unknownField("traceId"))
        XCTAssertEqual(try reason("unsupported-version.json"), .unsupportedVersion("2.0.0"))
        XCTAssertEqual(try reason("hello-version-mismatch.json"), .unsupportedVersion("2.0.0"))
        XCTAssertEqual(try reason("simulated-motion.json"), .simulatedData)
        XCTAssertEqual(try reason("zero-rssi.json"), .invalidField("rssiDbm"))
        XCTAssertEqual(try reason("client-count-mismatch.json"), .invalidField("clientCount"))
        XCTAssertEqual(try reason("bad-timestamp.json"), .invalidField("timestamp"))
    }

    func testRfTelemetryFieldsSurvive() throws {
        let message = try ProtocolDecoder.decode(Fixtures.data("firmware", "firmware-rf.json"))
        guard case .rfTelemetry(let rf) = message.body else { return XCTFail("not rf.telemetry") }
        XCTAssertEqual(rf.channel, 1)
        XCTAssertEqual(rf.clients.count, 2)
        XCTAssertEqual(rf.clients[0].rssiDbm, -54)
        XCTAssertEqual(rf.clients[0].estimatedDistance?.calibrated, true)
        XCTAssertNil(rf.clients[1].rssiDbm)
        XCTAssertEqual(message.sequence, 7)
    }

    func testOversizedFrameRejected() {
        let big = Data(repeating: 0x20, count: 2048)
        XCTAssertThrowsError(try ProtocolDecoder.decode(big, maxBytes: firmwareMaxFrameBytes)) { error in
            XCTAssertEqual(error as? ProtocolDecodingError, .tooLarge(2048))
        }
    }

    func testMalformedJsonRejectedWithoutCrash() {
        for text in ["", "{", "[]", "null", "{\"protocolVersion\":", "{\"a\":{\"b\":[1,2,{\"c\":\"\\\"\"}]}}"] {
            XCTAssertThrowsError(try ProtocolDecoder.decode(text), text)
        }
    }

    func testDuplicateKeyScanner() {
        XCTAssertNil(JSONDuplicateKeyScanner.firstDuplicateKey(in: Data("{\"a\":1,\"b\":{\"a\":2}}".utf8)))
        XCTAssertEqual(JSONDuplicateKeyScanner.firstDuplicateKey(in: Data("{\"a\":{\"x\":1,\"x\":2}}".utf8)), "x")
        XCTAssertNil(JSONDuplicateKeyScanner.firstDuplicateKey(in: Data("[{\"k\":1},{\"k\":2}]".utf8)))
    }
}

final class TimestampTests: XCTestCase {
    func testRoundTrip() {
        let text = "2026-11-21T16:10:03.600Z"
        guard let ms = ProtocolTimestamp.parseMilliseconds(text) else { return XCTFail("parse") }
        XCTAssertEqual(ms, 1_795_277_403_600)
        XCTAssertEqual(ProtocolTimestamp.formatMilliseconds(ms), text)
    }

    func testFractionVariants() {
        XCTAssertEqual(ProtocolTimestamp.parseMilliseconds("1970-01-01T00:00:00Z"), 0)
        XCTAssertEqual(ProtocolTimestamp.parseMilliseconds("1970-01-01T00:00:00.5Z"), 500)
        XCTAssertEqual(ProtocolTimestamp.parseMilliseconds("1970-01-01T00:00:00.05Z"), 50)
        XCTAssertEqual(ProtocolTimestamp.parseMilliseconds("2024-02-29T23:59:59.999Z"), 1_709_251_199_999)
    }

    func testRejectsInvalid() {
        for text in ["2026-11-2116:10:03", "2026-13-01T00:00:00Z", "2025-02-29T00:00:00Z",
                     "2026-11-21T24:00:00Z", "2026-11-21T16:10:03.1234Z", "2026-11-21T16:10:03+00:00",
                     "1969-12-31T23:59:59Z", "2026-11-21t16:10:03Z"] {
            XCTAssertNil(ProtocolTimestamp.parseMilliseconds(text), text)
        }
    }

    func testFormatDate() {
        let date = Date(timeIntervalSince1970: 1_795_277_403.6009)
        XCTAssertEqual(ProtocolTimestamp.format(date), "2026-11-21T16:10:03.600Z")
    }
}
