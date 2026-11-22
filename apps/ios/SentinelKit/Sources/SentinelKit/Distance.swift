import Foundation

/// Log-distance path-loss model, the same one the firmware fits
/// (include/rf/distance_estimator.h): RSSI = A − 10·n·log10(d / 1 m).
///
/// RSSI depends on body orientation, obstacles, multipath and antennas, so a
/// distance derived from it is an estimate. Uncalibrated defaults are only
/// orientative; a calibrated model comes from real measurements at known
/// distances, made on the ESP32 through the lab API.
public struct LogDistanceModel: Equatable, Sendable {
    /// RSSI expected at 1 m, dBm.
    public var referenceRssiAt1m: Double
    /// Path-loss exponent (≈2 free space, 2.7–3.5 indoors).
    public var exponent: Double

    public static let defaultReferenceRssi = -40.0
    public static let defaultExponent = 2.7
    /// Exponent range used for the uncertainty band of the uncalibrated model.
    public static let exponentBand: ClosedRange<Double> = 2.0...3.5
    /// Practical ESP32 receive sensitivity used as the "edge of coverage".
    public static let sensitivityDbm = -90.0

    public static let uncalibrated = LogDistanceModel(referenceRssiAt1m: defaultReferenceRssi,
                                                      exponent: defaultExponent)

    public init(referenceRssiAt1m: Double, exponent: Double) {
        self.referenceRssiAt1m = referenceRssiAt1m
        self.exponent = exponent
    }

    public var isPlausible: Bool {
        referenceRssiAt1m.isFinite && exponent.isFinite && (0.5...8).contains(exponent)
            && (-100.0 ..< 0).contains(referenceRssiAt1m)
    }

    /// Estimated distance in meters for a received power, or nil when invalid.
    public func distance(rssiDbm: Double) -> Double? {
        guard isPlausible, rssiDbm.isFinite, (-100.0 ..< 0).contains(rssiDbm) else { return nil }
        let meters = pow(10, (referenceRssiAt1m - rssiDbm) / (10 * exponent))
        return meters.isFinite ? meters : nil
    }

    /// Expected RSSI at a distance (inverse of `distance`).
    public func rssi(atMeters meters: Double) -> Double? {
        guard isPlausible, meters.isFinite, meters > 0 else { return nil }
        return referenceRssiAt1m - 10 * exponent * log10(meters)
    }

    /// Theoretical maximum range: distance at which RSSI falls to the given
    /// sensitivity under this model. Not a measurement.
    public func theoreticalMaxRange(sensitivityDbm: Double = LogDistanceModel.sensitivityDbm) -> Double? {
        guard sensitivityDbm < referenceRssiAt1m else { return nil }
        return distance(rssiDbm: sensitivityDbm)
    }

    /// Distance range obtained by sweeping the exponent across `band` with the
    /// same reference: shows how uncertain an uncalibrated estimate is.
    public func distanceBand(rssiDbm: Double,
                             band: ClosedRange<Double> = LogDistanceModel.exponentBand) -> ClosedRange<Double>? {
        let low = LogDistanceModel(referenceRssiAt1m: referenceRssiAt1m, exponent: band.upperBound)
        let high = LogDistanceModel(referenceRssiAt1m: referenceRssiAt1m, exponent: band.lowerBound)
        guard let a = low.distance(rssiDbm: rssiDbm), let b = high.distance(rssiDbm: rssiDbm) else { return nil }
        return min(a, b)...max(a, b)
    }

    /// Least-squares fit of A and n from (distance, RSSI) points, matching the
    /// firmware's calibration. Requires at least two distinct distances.
    public static func fit(_ points: [(meters: Double, rssiDbm: Double)]) -> LogDistanceModel? {
        let valid = points.filter { $0.meters.isFinite && $0.meters > 0 && $0.rssiDbm.isFinite && $0.rssiDbm < 0 }
        guard valid.count >= 2 else { return nil }
        let xs = valid.map { log10($0.meters) }
        let ys = valid.map(\.rssiDbm)
        let count = Double(valid.count)
        let meanX = xs.reduce(0, +) / count
        let meanY = ys.reduce(0, +) / count
        var sxx = 0.0
        var sxy = 0.0
        for (x, y) in zip(xs, ys) {
            sxx += (x - meanX) * (x - meanX)
            sxy += (x - meanX) * (y - meanY)
        }
        guard sxx > 1e-12 else { return nil }
        let slope = sxy / sxx
        let model = LogDistanceModel(referenceRssiAt1m: meanY - slope * meanX, exponent: -slope / 10)
        return model.isPlausible ? model : nil
    }
}

/// Largest distance estimate seen during a session, per model kind, so a
/// calibrated maximum is never mixed with an orientative one.
public struct SessionDistanceTracker: Equatable, Sendable {
    public private(set) var maxCalibratedM: Double?
    public private(set) var maxOrientativeM: Double?

    public init() {}

    public mutating func record(meters: Double, calibrated: Bool) {
        guard meters.isFinite, meters >= 0 else { return }
        if calibrated {
            maxCalibratedM = max(maxCalibratedM ?? 0, meters)
        } else {
            maxOrientativeM = max(maxOrientativeM ?? 0, meters)
        }
    }

    public mutating func reset() {
        maxCalibratedM = nil
        maxOrientativeM = nil
    }
}

/// Chooses which RF client is this iPhone: the remembered one if present,
/// else the only connected client. iOS cannot read its own Wi-Fi MAC/RSSI,
/// so the ESP32's per-client RSSI is the only source.
public enum ThisDeviceResolver {
    public static func resolve(remembered: UInt64?, connectedClientIds: [UInt64]) -> UInt64? {
        if let remembered, connectedClientIds.contains(remembered) { return remembered }
        return connectedClientIds.count == 1 ? connectedClientIds[0] : nil
    }
}
