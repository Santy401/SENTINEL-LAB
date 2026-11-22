import Foundation

// Motion physics shared by the app (CoreMotion) and the tests (Linux).
// Units: CoreMotion reports accelerations in g; everything here converts to
// SI (m/s²) once, at the edge, using standard gravity. Nothing integrates
// acceleration into velocity or position: drift would make that meaningless.

public enum Physics {
    /// Standard gravity, m/s² (CGPM 1901).
    public static let standardGravity = 9.80665
    /// Nominal iPhone 12 mass in kg (Apple technical specifications, 164 g).
    public static let iPhone12MassKg = 0.164
    /// Linear acceleration (m/s²) mapped to intensity 1.0; same scale as the
    /// firmware RGB engine (|lin| / 8 m/s², clamped).
    public static let fullScaleMs2 = 8.0

    /// Net force on the phone, F = m·a (newtons). It is the force that makes
    /// the phone accelerate as measured, not the force of the hand alone.
    public static func newtons(accelerationMs2: Double, massKg: Double = iPhone12MassKg) -> Double {
        massKg * accelerationMs2
    }

    public static func intensity(linearMs2: Double) -> Double {
        guard linearMs2.isFinite else { return 0 }
        return min(max(linearMs2 / fullScaleMs2, 0), 1)
    }
}

/// Plain 3D vector (no SIMD so it compiles on Linux without extra modules).
public struct Vec3: Codable, Equatable, Sendable {
    public var x: Double
    public var y: Double
    public var z: Double

    public init(x: Double, y: Double, z: Double) {
        self.x = x
        self.y = y
        self.z = z
    }

    public static let zero = Vec3(x: 0, y: 0, z: 0)

    public var magnitude: Double { (x * x + y * y + z * z).squareRoot() }
    public var isFinite: Bool { x.isFinite && y.isFinite && z.isFinite }

    public func scaled(_ factor: Double) -> Vec3 { Vec3(x: x * factor, y: y * factor, z: z * factor) }
    public static func + (lhs: Vec3, rhs: Vec3) -> Vec3 { Vec3(x: lhs.x + rhs.x, y: lhs.y + rhs.y, z: lhs.z + rhs.z) }

    /// Rounded copy for transport (0.1 mm/s² resolution keeps frames small).
    public func rounded(decimals: Int = 4) -> Vec3 {
        let scale = pow(10.0, Double(decimals))
        return Vec3(x: (x * scale).rounded() / scale, y: (y * scale).rounded() / scale, z: (z * scale).rounded() / scale)
    }
}

/// One device-motion reading converted to SI units.
public struct MotionReading: Equatable, Sendable {
    /// Monotonic seconds (CMDeviceMotion.timestamp).
    public var time: TimeInterval
    /// Wall-clock time of the reading, for `sensorTimestamp`.
    public var date: Date
    /// User (linear) acceleration, gravity removed, m/s².
    public var linear: Vec3
    /// Gravity vector, m/s².
    public var gravity: Vec3
    /// Rotation rate, rad/s.
    public var rotationRate: Vec3

    public init(time: TimeInterval, date: Date, linear: Vec3, gravity: Vec3, rotationRate: Vec3 = .zero) {
        self.time = time
        self.date = date
        self.linear = linear
        self.gravity = gravity
        self.rotationRate = rotationRate
    }

    /// Builds a reading from CoreMotion values expressed in g.
    public static func fromG(time: TimeInterval, date: Date, userAccelerationG: Vec3, gravityG: Vec3,
                             rotationRate: Vec3) -> MotionReading {
        MotionReading(time: time, date: date, linear: userAccelerationG.scaled(Physics.standardGravity),
                      gravity: gravityG.scaled(Physics.standardGravity), rotationRate: rotationRate)
    }

    /// Total proper acceleration the accelerometer feels, m/s².
    public var total: Vec3 { linear + gravity }
}

/// Motion state thresholds (m/s² of filtered linear acceleration), shared
/// with the `motion.sample` contract.
public enum ShakeClassifier {
    public static let movementThreshold = 0.5
    public static let alertThreshold = 4.0

    public static func state(filteredMs2: Double) -> MotionState {
        if !filteredMs2.isFinite { return .sensorOffline }
        if filteredMs2 < movementThreshold { return .stable }
        if filteredMs2 < alertThreshold { return .movement }
        return .alert
    }
}

/// Snapshot of the shake analysis.
public struct ShakeMetrics: Equatable, Sendable {
    /// Instantaneous |linear acceleration|, m/s² (raw, unfiltered).
    public var rawMs2: Double = 0
    /// RMS of |linear acceleration| over the last `windowSeconds` (filtered).
    public var rmsMs2: Double = 0
    /// Largest raw magnitude inside the RMS window.
    public var windowPeakMs2: Double = 0
    /// Session statistics of the filtered (RMS) value.
    public var sessionMaxMs2: Double = 0
    public var sessionMinMs2: Double = 0
    public var sessionAverageMs2: Double = 0
    /// Largest raw magnitude since the session started.
    public var sessionPeakMs2: Double = 0
    /// Samples received during the last second (Hz).
    public var sampleRateHz: Double = 0
    public var sampleCount: Int = 0
    public var state: MotionState = .stable

    public init() {}

    public var intensity: Double { Physics.intensity(linearMs2: rmsMs2) }
    public var rmsG: Double { rmsMs2 / Physics.standardGravity }
    public var rawG: Double { rawMs2 / Physics.standardGravity }
    public var forceNewtons: Double { Physics.newtons(accelerationMs2: rmsMs2) }
    public var peakForceNewtons: Double { Physics.newtons(accelerationMs2: sessionPeakMs2) }
}

/// Bounded, allocation-stable shake analyzer. Feed it every reading; the RMS
/// window removes single-sample noise while keeping shakes visible.
public struct ShakeAnalyzer: Sendable {
    public let windowSeconds: TimeInterval
    private struct TimedMagnitude: Sendable {
        let time: TimeInterval
        let magnitude: Double
    }

    private var window: RingBuffer<TimedMagnitude>
    private var rateWindow: RingBuffer<TimeInterval>
    private var sumFiltered = 0.0
    public private(set) var metrics = ShakeMetrics()

    public init(windowSeconds: TimeInterval = 0.5, capacity: Int = 256) {
        self.windowSeconds = windowSeconds
        window = RingBuffer(capacity: capacity)
        rateWindow = RingBuffer(capacity: 512)
    }

    @discardableResult
    public mutating func feed(_ reading: MotionReading) -> ShakeMetrics {
        let magnitude = reading.linear.magnitude
        guard magnitude.isFinite else { return metrics }
        window.append(TimedMagnitude(time: reading.time, magnitude: magnitude))
        rateWindow.append(reading.time)

        let inWindow = window.elements.filter { reading.time - $0.time <= windowSeconds }
        let sumSquares = inWindow.reduce(0) { $0 + $1.magnitude * $1.magnitude }
        let rms = inWindow.isEmpty ? magnitude : (sumSquares / Double(inWindow.count)).squareRoot()
        let windowPeak = inWindow.map(\.magnitude).max() ?? magnitude
        let recent = rateWindow.elements.filter { reading.time - $0 <= 1.0 }

        metrics.rawMs2 = magnitude
        metrics.rmsMs2 = rms
        metrics.windowPeakMs2 = windowPeak
        metrics.sampleCount += 1
        sumFiltered += rms
        if metrics.sampleCount == 1 {
            metrics.sessionMinMs2 = rms
            metrics.sessionMaxMs2 = rms
        } else {
            metrics.sessionMinMs2 = min(metrics.sessionMinMs2, rms)
            metrics.sessionMaxMs2 = max(metrics.sessionMaxMs2, rms)
        }
        metrics.sessionAverageMs2 = sumFiltered / Double(metrics.sampleCount)
        metrics.sessionPeakMs2 = max(metrics.sessionPeakMs2, magnitude)
        if let first = recent.first, recent.count > 1, reading.time > first {
            metrics.sampleRateHz = Double(recent.count - 1) / (reading.time - first)
        }
        metrics.state = ShakeClassifier.state(filteredMs2: rms)
        return metrics
    }

    public mutating func reset() {
        window.removeAll()
        rateWindow.removeAll()
        sumFiltered = 0
        metrics = ShakeMetrics()
    }
}

/// Selects, for each fixed window (default 200 ms → 5 Hz), the real reading
/// with the largest |linear acceleration|. Short shakes between transmissions
/// are therefore not lost, and every transmitted value is a measured sample.
public struct PeakHoldWindow: Sendable {
    public let period: TimeInterval
    private var windowStart: TimeInterval?
    private var best: MotionReading?

    public init(period: TimeInterval = 0.2) {
        self.period = period
    }

    /// Returns the selected reading when the current window closes.
    public mutating func offer(_ reading: MotionReading) -> MotionReading? {
        guard let start = windowStart else {
            windowStart = reading.time
            best = reading
            return nil
        }
        if reading.time - start >= period {
            let selected = best
            windowStart = reading.time
            best = reading
            return selected
        }
        if let current = best, reading.linear.magnitude <= current.linear.magnitude { return nil }
        best = reading
        return nil
    }

    public mutating func reset() {
        windowStart = nil
        best = nil
    }
}
