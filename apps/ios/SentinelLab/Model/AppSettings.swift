import Foundation
import Observation
import SentinelKit

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
    /// Send real CoreMotion samples to the ESP32 (needs the token).
    var motionStreaming: Bool { didSet { defaults.set(motionStreaming, forKey: Keys.motionStreaming) } }
    /// Immersive (visual) vs scientific (numbers first) motion layout.
    var immersiveMotion: Bool { didSet { defaults.set(immersiveMotion, forKey: Keys.immersiveMotion) } }
    /// Haptic feedback that follows the shake intensity (Movimiento tab).
    var hapticsEnabled: Bool { didSet { defaults.set(hapticsEnabled, forKey: Keys.haptics) } }
    /// Lock Screen Live Activity with the ESP32 status.
    var liveActivityEnabled: Bool { didSet { defaults.set(liveActivityEnabled, forKey: Keys.liveActivity) } }
    /// Local notifications for alerts while the app is in the background.
    var alertNotifications: Bool { didSet { defaults.set(alertNotifications, forKey: Keys.alertNotifications) } }
    /// Send alerts from this iPhone to the n8n webhook (Telegram), over cellular if needed.
    var alertWebhookEnabled: Bool { didSet { defaults.set(alertWebhookEnabled, forKey: Keys.alertWebhook) } }
    var alertWebhookURL: String { didSet { defaults.set(alertWebhookURL, forKey: Keys.alertWebhookURL) } }
    /// RF clientId the user identified as this iPhone (the ESP32 measures it).
    var thisClientId: UInt64? {
        didSet {
            if let thisClientId { defaults.set(String(thisClientId), forKey: Keys.thisClientId) } else {
                defaults.removeObject(forKey: Keys.thisClientId)
            }
        }
    }

    /// Stable per-install identifier used as the Protocol v1 `deviceId`.
    let deviceId: String

    private enum Keys {
        static let host = "settings.host"
        static let autoConnect = "settings.autoConnect"
        static let showFiltered = "settings.showFilteredRssi"
        static let chartWindow = "settings.chartWindowMinutes"
        static let pollInterval = "settings.pollIntervalSeconds"
        static let deviceId = "settings.deviceId"
        static let motionStreaming = "settings.motionStreaming"
        static let immersiveMotion = "settings.immersiveMotion"
        static let thisClientId = "settings.thisClientId"
        static let haptics = "settings.haptics"
        static let liveActivity = "settings.liveActivity"
        static let alertNotifications = "settings.alertNotifications"
        static let alertWebhook = "settings.alertWebhook"
        static let alertWebhookURL = "settings.alertWebhookURL"
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        host = defaults.string(forKey: Keys.host) ?? Self.defaultHost
        autoConnect = defaults.object(forKey: Keys.autoConnect) as? Bool ?? true
        showFilteredRssi = defaults.object(forKey: Keys.showFiltered) as? Bool ?? true
        chartWindowMinutes = defaults.object(forKey: Keys.chartWindow) as? Int ?? 5
        pollIntervalSeconds = defaults.object(forKey: Keys.pollInterval) as? Int ?? 2
        motionStreaming = defaults.object(forKey: Keys.motionStreaming) as? Bool ?? true
        immersiveMotion = defaults.object(forKey: Keys.immersiveMotion) as? Bool ?? true
        thisClientId = defaults.string(forKey: Keys.thisClientId).flatMap(UInt64.init)
        hapticsEnabled = defaults.object(forKey: Keys.haptics) as? Bool ?? true
        liveActivityEnabled = defaults.object(forKey: Keys.liveActivity) as? Bool ?? false
        alertNotifications = defaults.object(forKey: Keys.alertNotifications) as? Bool ?? true
        alertWebhookEnabled = defaults.object(forKey: Keys.alertWebhook) as? Bool ?? true
        alertWebhookURL = defaults.string(forKey: Keys.alertWebhookURL) ?? AlertWebhookPayload.defaultURL.absoluteString
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
