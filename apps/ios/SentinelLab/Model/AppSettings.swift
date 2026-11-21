import Foundation
import Observation

/// Non-secret preferences persisted in UserDefaults. The bearer token is
/// deliberately not here; it lives only in the Keychain.
@Observable
final class AppSettings {
    static let defaultHost = "192.168.4.1"

    private let defaults: UserDefaults

    var host: String { didSet { defaults.set(host, forKey: Keys.host) } }
    var autoConnect: Bool { didSet { defaults.set(autoConnect, forKey: Keys.autoConnect) } }
    var showFilteredRssi: Bool { didSet { defaults.set(showFilteredRssi, forKey: Keys.showFiltered) } }
    var chartWindowMinutes: Int { didSet { defaults.set(chartWindowMinutes, forKey: Keys.chartWindow) } }
    var pollIntervalSeconds: Int { didSet { defaults.set(pollIntervalSeconds, forKey: Keys.pollInterval) } }

    /// Stable per-install identifier used as the Protocol v1 `deviceId`.
    let deviceId: String

    private enum Keys {
        static let host = "settings.host"
        static let autoConnect = "settings.autoConnect"
        static let showFiltered = "settings.showFilteredRssi"
        static let chartWindow = "settings.chartWindowMinutes"
        static let pollInterval = "settings.pollIntervalSeconds"
        static let deviceId = "settings.deviceId"
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        host = defaults.string(forKey: Keys.host) ?? Self.defaultHost
        autoConnect = defaults.object(forKey: Keys.autoConnect) as? Bool ?? true
        showFilteredRssi = defaults.object(forKey: Keys.showFiltered) as? Bool ?? true
        chartWindowMinutes = defaults.object(forKey: Keys.chartWindow) as? Int ?? 5
        pollIntervalSeconds = defaults.object(forKey: Keys.pollInterval) as? Int ?? 2
        if let stored = defaults.string(forKey: Keys.deviceId) {
            deviceId = stored
        } else {
            let generated = "iphone-" + UUID().uuidString.lowercased().prefix(8)
            defaults.set(generated, forKey: Keys.deviceId)
            deviceId = generated
        }
    }

    /// Accepts an IPv4 address or a DNS label host, without scheme or port.
    static func isValidHost(_ text: String) -> Bool {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, trimmed.count <= 253 else { return false }
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")
        return trimmed.unicodeScalars.allSatisfy(allowed.contains) && !trimmed.hasPrefix(".") && !trimmed.hasPrefix("-")
    }
}
