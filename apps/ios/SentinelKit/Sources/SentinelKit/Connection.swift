import Foundation

/// Deterministic 64-bit generator (SplitMix64) so reconnection jitter can be
/// reproduced exactly in tests. Production seeds it from the system RNG.
public struct SplitMix64: RandomNumberGenerator, Sendable {
    private var state: UInt64

    public init(seed: UInt64) { state = seed }

    public init() {
        var system = SystemRandomNumberGenerator()
        state = system.next()
    }

    public mutating func next() -> UInt64 {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }
}

/// Capped exponential backoff with symmetric proportional jitter.
public struct ExponentialBackoff: Equatable, Sendable {
    public var initial: TimeInterval
    public var maximum: TimeInterval
    public var multiplier: Double
    /// Fraction of the nominal delay added or removed at random (0...1).
    public var jitter: Double

    public init(initial: TimeInterval = 1, maximum: TimeInterval = 30, multiplier: Double = 2, jitter: Double = 0.2) {
        self.initial = max(0.05, initial)
        self.maximum = max(self.initial, maximum)
        self.multiplier = max(1, multiplier)
        self.jitter = min(1, max(0, jitter))
    }

    /// Nominal delay before jitter, for attempt >= 1.
    public func nominalDelay(attempt: Int) -> TimeInterval {
        let exponent = Double(max(0, attempt - 1))
        let value = initial * pow(multiplier, exponent)
        return value.isFinite ? min(maximum, value) : maximum
    }

    public func delay<G: RandomNumberGenerator>(attempt: Int, using generator: inout G) -> TimeInterval {
        let nominal = nominalDelay(attempt: attempt)
        guard jitter > 0 else { return nominal }
        let unit = Double(generator.next() >> 11) / Double(1 << 53)
        let factor = 1 + jitter * (unit * 2 - 1)
        return min(maximum, max(0.05, nominal * factor))
    }
}

public enum ConnectionPhase: Equatable, Sendable {
    /// Never started or explicitly stopped by the user.
    case idle
    case connecting(attempt: Int)
    /// WebSocket handshake done; no Protocol v1 frame yet (clock may be unsynced).
    case awaitingData
    case streaming
    case waitingToRetry(attempt: Int, delay: TimeInterval)
    /// The OS reports no usable local network path.
    case offline
}

public enum ConnectionEvent: Equatable, Sendable {
    case start
    case stop
    case transportOpened
    case frameReceived
    case transportFailed(String)
    case retryTimerFired
    case networkLost
    case networkRestored
}

public enum ConnectionEffect: Equatable, Sendable {
    case openTransport
    case closeTransport
    case scheduleRetry(TimeInterval)
    case cancelRetry
}

/// Pure reconnection state machine; the transport executes the effects.
public struct ConnectionStateMachine: Sendable {
    public private(set) var phase: ConnectionPhase = .idle
    public private(set) var lastFailure: String?
    public private(set) var failures: Int = 0
    public private(set) var reconnects: Int = 0
    public let backoff: ExponentialBackoff
    private var attempt = 0
    private var everStreamed = false
    private var generator: SplitMix64

    public init(backoff: ExponentialBackoff = ExponentialBackoff(), generator: SplitMix64 = SplitMix64()) {
        self.backoff = backoff
        self.generator = generator
    }

    public var isActive: Bool { phase != .idle }

    @discardableResult
    public mutating func handle(_ event: ConnectionEvent) -> [ConnectionEffect] {
        switch (phase, event) {
        case (.idle, .start):
            attempt = 1
            phase = .connecting(attempt: attempt)
            return [.openTransport]

        case (_, .stop):
            let wasActive = phase != .idle
            phase = .idle
            attempt = 0
            return wasActive ? [.cancelRetry, .closeTransport] : []

        case (.connecting, .transportOpened):
            phase = .awaitingData
            return []

        case (.awaitingData, .frameReceived), (.streaming, .frameReceived):
            if phase == .awaitingData, everStreamed { reconnects += 1 }
            everStreamed = true
            attempt = 0
            phase = .streaming
            return []

        case (.connecting, .transportFailed(let reason)),
             (.awaitingData, .transportFailed(let reason)),
             (.streaming, .transportFailed(let reason)):
            failures += 1
            lastFailure = reason
            attempt += 1
            let delay = backoff.delay(attempt: max(1, attempt - 1), using: &generator)
            phase = .waitingToRetry(attempt: attempt, delay: delay)
            return [.closeTransport, .scheduleRetry(delay)]

        case (.waitingToRetry(let next, _), .retryTimerFired):
            phase = .connecting(attempt: next)
            return [.openTransport]

        case (.idle, .networkLost), (.offline, .networkLost):
            return []

        case (_, .networkLost):
            phase = .offline
            return [.cancelRetry, .closeTransport]

        case (.offline, .networkRestored):
            attempt = 1
            phase = .connecting(attempt: attempt)
            return [.openTransport]

        default:
            // Stale callbacks (e.g. a late failure after stop) are ignored.
            return []
        }
    }
}
