import XCTest
@testable import SentinelLab

final class AppSettingsTests: XCTestCase {
    private func freshDefaults() -> UserDefaults {
        let name = "SentinelLabTests-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: name)!
        defaults.removePersistentDomain(forName: name)
        return defaults
    }

    func testDefaultsPointAtLaboratoryAccessPoint() {
        let settings = AppSettings(defaults: freshDefaults())
        XCTAssertEqual(settings.host, "192.168.4.1")
        XCTAssertTrue(settings.autoConnect)
        XCTAssertTrue(settings.deviceId.hasPrefix("iphone-"))
    }

    func testDeviceIdIsStableAcrossLaunches() {
        let defaults = freshDefaults()
        let first = AppSettings(defaults: defaults).deviceId
        XCTAssertEqual(AppSettings(defaults: defaults).deviceId, first)
    }

    func testHostValidation() {
        XCTAssertTrue(AppSettings.isValidHost("192.168.4.1"))
        XCTAssertTrue(AppSettings.isValidHost("sentinel.local"))
        XCTAssertFalse(AppSettings.isValidHost(""))
        XCTAssertFalse(AppSettings.isValidHost("http://192.168.4.1"))
        XCTAssertFalse(AppSettings.isValidHost("192.168.4.1:81"))
        XCTAssertFalse(AppSettings.isValidHost("bad host"))
    }

    func testRealtimeURL() {
        XCTAssertEqual(RealtimeTransport.url(host: "192.168.4.1")?.absoluteString, "ws://192.168.4.1:81/realtime")
    }
}
