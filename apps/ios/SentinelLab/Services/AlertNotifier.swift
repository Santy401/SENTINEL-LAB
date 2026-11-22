import Foundation
import SentinelKit
import UIKit
import UserNotifications

/// Posts local notifications for alerts raised while the app is in the
/// background. In the foreground alerts appear in the in-app list only.
@MainActor
final class AlertNotifier {
    private(set) var authorized = false

    func requestAuthorization() async {
        let center = UNUserNotificationCenter.current()
        let granted = (try? await center.requestAuthorization(options: [.alert, .sound])) ?? false
        authorized = granted
    }

    func refreshAuthorization() async {
        let settings = await UNUserNotificationCenter.current().notificationSettings()
        authorized = settings.authorizationStatus == .authorized || settings.authorizationStatus == .provisional
    }

    func notify(_ alert: LabAlert) {
        guard authorized, UIApplication.shared.applicationState != .active else { return }
        let content = UNMutableNotificationContent()
        content.title = "SENTINEL-LAB · \(alert.kind.title)"
        content.body = alert.message
        content.sound = alert.kind.severity == .critical ? .default : nil
        content.threadIdentifier = alert.kind.rawValue
        let request = UNNotificationRequest(identifier: alert.id, content: content, trigger: nil)
        UNUserNotificationCenter.current().add(request)
    }
}
