import CoreMotion
import Foundation
import Observation
import SentinelKit

/// Orientation of the phone from CoreMotion's sensor fusion (attitude).
struct Attitude: Equatable {
    var roll: Double = 0
    var pitch: Double = 0
    var yaw: Double = 0
    /// Unit quaternion (x, y, z, w) relative to the reference frame.
    var quaternion: (x: Double, y: Double, z: Double, w: Double) = (0, 0, 0, 1)

    static func == (lhs: Attitude, rhs: Attitude) -> Bool {
        lhs.roll == rhs.roll && lhs.pitch == rhs.pitch && lhs.yaw == rhs.yaw
    }
}

/// One point of the per-axis chart (m/s²).
struct AxisPoint: Identifiable, Equatable {
    let id: Int
    let date: Date
    let x: Double
    let y: Double
    let z: Double
    let magnitude: Double
}

/// UI snapshot published on the main actor (~30 Hz).
struct MotionSnapshot: Equatable {
    var metrics = ShakeMetrics()
    var linear = Vec3.zero
    var gravity = Vec3.zero
    var rotationRate = Vec3.zero
    var attitude = Attitude()
}

/// CoreMotion device motion at 100 Hz. Readings are analyzed on a background
/// queue (RMS, peak, stats, peak-hold for transmission); the main actor only
/// receives throttled snapshots, so the UI never runs at sensor rate.
@MainActor
@Observable
final class MotionService {
    private(set) var snapshot = MotionSnapshot()
    private(set) var axisHistory: [AxisPoint] = []
    private(set) var isRunning = false
    private(set) var unavailableReason: String?

    /// Called on the main actor with each 200 ms peak-hold reading.
    @ObservationIgnored var onTransmitSample: ((MotionReading, Double) -> Void)?

    static let sensorRateHz = 100.0
    static let uiRateHz = 30.0
    static let chartSeconds: TimeInterval = 10

    @ObservationIgnored private let manager = CMMotionManager()
    @ObservationIgnored private let queue: OperationQueue = {
        let queue = OperationQueue()
        queue.name = "lab.sentinel.motion"
        queue.maxConcurrentOperationCount = 1
        queue.qualityOfService = .userInteractive
        return queue
    }()
    @ObservationIgnored private let processor = MotionProcessor()
    @ObservationIgnored private var nextPointId = 0

    func start() {
        guard !isRunning else { return }
        guard manager.isDeviceMotionAvailable else {
            unavailableReason = "Este dispositivo no ofrece Device Motion (acelerómetro + giroscopio)."
            return
        }
        unavailableReason = nil
        manager.deviceMotionUpdateInterval = 1 / Self.sensorRateHz
        let processor = processor
        let bootDate = Date().addingTimeInterval(-ProcessInfo.processInfo.systemUptime)
        manager.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: queue) { [weak self] motion, error in
            guard let motion else {
                if let error {
                    let text = error.localizedDescription
                    Task { @MainActor in self?.unavailableReason = text }
                }
                return
            }
            let reading = MotionReading.fromG(
                time: motion.timestamp,
                date: bootDate.addingTimeInterval(motion.timestamp),
                userAccelerationG: Vec3(x: motion.userAcceleration.x, y: motion.userAcceleration.y,
                                        z: motion.userAcceleration.z),
                gravityG: Vec3(x: motion.gravity.x, y: motion.gravity.y, z: motion.gravity.z),
                rotationRate: Vec3(x: motion.rotationRate.x, y: motion.rotationRate.y, z: motion.rotationRate.z))
            let q = motion.attitude.quaternion
            let attitude = Attitude(roll: motion.attitude.roll, pitch: motion.attitude.pitch, yaw: motion.attitude.yaw,
                                    quaternion: (q.x, q.y, q.z, q.w))
            let output = processor.process(reading, attitude: attitude, uiInterval: 1 / MotionService.uiRateHz)
            guard output.snapshot != nil || output.transmit != nil else { return }
            Task { @MainActor in self?.deliver(output) }
        }
        isRunning = true
    }

    func stop() {
        manager.stopDeviceMotionUpdates()
        isRunning = false
    }

    func resetSession() {
        processor.reset()
        axisHistory.removeAll()
        snapshot = MotionSnapshot()
    }

    private func deliver(_ output: MotionProcessor.Output) {
        if let transmit = output.transmit { onTransmitSample?(transmit.reading, transmit.filteredMs2) }
        guard let next = output.snapshot else { return }
        snapshot = next
        let now = Date()
        nextPointId += 1
        axisHistory.append(AxisPoint(id: nextPointId, date: now, x: next.linear.x, y: next.linear.y,
                                     z: next.linear.z, magnitude: next.metrics.rawMs2))
        let cutoff = now.addingTimeInterval(-Self.chartSeconds)
        if let firstKept = axisHistory.firstIndex(where: { $0.date >= cutoff }), firstKept > 0 {
            axisHistory.removeFirst(firstKept)
        }
    }
}

/// Background-queue state. Only touched from the motion OperationQueue
/// (serial), except `reset`, which takes the lock.
final class MotionProcessor: @unchecked Sendable {
    struct Output: Sendable {
        var snapshot: MotionSnapshot?
        var transmit: (reading: MotionReading, filteredMs2: Double)?
    }

    private let lock = NSLock()
    private var analyzer = ShakeAnalyzer(windowSeconds: 0.5)
    private var peakHold = PeakHoldWindow(period: 0.2)
    private var lastUiTime: TimeInterval = 0

    func process(_ reading: MotionReading, attitude: Attitude, uiInterval: TimeInterval) -> Output {
        lock.lock()
        defer { lock.unlock() }
        let metrics = analyzer.feed(reading)
        var output = Output()
        if let selected = peakHold.offer(reading) {
            output.transmit = (selected, metrics.rmsMs2)
        }
        if reading.time - lastUiTime >= uiInterval {
            lastUiTime = reading.time
            output.snapshot = MotionSnapshot(metrics: metrics, linear: reading.linear, gravity: reading.gravity,
                                             rotationRate: reading.rotationRate, attitude: attitude)
        }
        return output
    }

    func reset() {
        lock.lock()
        analyzer.reset()
        peakHold.reset()
        lastUiTime = 0
        lock.unlock()
    }
}

extension MotionSnapshot: @unchecked Sendable {}
