#if canImport(ActivityKit) && os(iOS)
import ActivityKit
import Foundation

/// Live Activity shared by the app (starts/updates it) and the widget
/// extension (renders it). The dynamic content is `SentinelActivityState`.
public struct SentinelActivityAttributes: ActivityAttributes {
    public typealias ContentState = SentinelActivityState

    /// Static for the activity's lifetime.
    public var deviceHost: String

    public init(deviceHost: String) {
        self.deviceHost = deviceHost
    }
}
#endif
