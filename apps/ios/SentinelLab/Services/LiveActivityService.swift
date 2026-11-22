import ActivityKit
import Foundation
import SentinelKit

/// Lock Screen / Dynamic Island Live Activity with the ESP32 status. Started
/// and stopped from Ajustes; updated by the app only when the content changes
/// meaningfully and at most every 2 s (ActivityKit updates are budgeted).
@MainActor
final class LiveActivityService {
    static let minUpdateInterval: TimeInterval = 2

    private var activity: Activity<SentinelActivityAttributes>?
    private var lastState: SentinelActivityState?
    private var lastUpdate = Date.distantPast
    private(set) var lastError: String?

    var isRunning: Bool { activity?.activityState == .active }

    var systemEnabled: Bool { ActivityAuthorizationInfo().areActivitiesEnabled }

    func start(host: String, state: SentinelActivityState) {
        guard activity == nil else { return }
        guard systemEnabled else {
            lastError = "Las Actividades en vivo están desactivadas en Ajustes de iOS para esta app."
            return
        }
        do {
            let content = ActivityContent(state: state, staleDate: Date().addingTimeInterval(120))
            activity = try Activity.request(attributes: SentinelActivityAttributes(deviceHost: host),
                                            content: content, pushType: nil)
            lastState = state
            lastUpdate = Date()
            lastError = nil
        } catch {
            // Typical causes: the widget extension is missing from the
            // installed bundle, or signing did not include it.
            lastError = "No se pudo iniciar la Actividad en vivo: \(error.localizedDescription)"
        }
    }

    func update(_ state: SentinelActivityState) {
        guard let activity else { return }
        let now = Date()
        if let lastState, !state.differsMeaningfully(from: lastState) { return }
        guard now.timeIntervalSince(lastUpdate) >= Self.minUpdateInterval else { return }
        lastState = state
        lastUpdate = now
        let content = ActivityContent(state: state, staleDate: now.addingTimeInterval(120))
        Task { await activity.update(content) }
    }

    func stop() {
        guard let activity else { return }
        let final = ActivityContent(state: lastState ?? SentinelActivityState(), staleDate: nil)
        Task { await activity.end(final, dismissalPolicy: .immediate) }
        self.activity = nil
        lastState = nil
    }

    /// Re-attach to an activity left running by a previous app launch.
    func resume() {
        activity = Activity<SentinelActivityAttributes>.activities.first
        lastState = activity?.content.state
    }
}
