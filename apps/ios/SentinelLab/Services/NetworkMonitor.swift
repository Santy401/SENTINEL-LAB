import Foundation
import Network

/// Reports whether a usable Wi-Fi/wired path exists. The laboratory AP has no
/// internet uplink, so "satisfied" here only means a local path is available.
final class NetworkMonitor: @unchecked Sendable {
    private let monitor = NWPathMonitor()
    private let queue = DispatchQueue(label: "lab.sentinel.app.network-monitor")

    func start(_ onChange: @escaping @MainActor (Bool, Bool) -> Void) {
        monitor.pathUpdateHandler = { path in
            let available = path.status == .satisfied
            let wifi = path.usesInterfaceType(.wifi)
            Task { @MainActor in onChange(available, wifi) }
        }
        monitor.start(queue: queue)
    }

    func stop() {
        monitor.cancel()
    }
}
