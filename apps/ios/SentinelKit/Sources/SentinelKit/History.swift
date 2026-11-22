import Foundation

/// Fixed-capacity FIFO; the oldest element is overwritten when full.
public struct RingBuffer<Element>: Sequence {
    public let capacity: Int
    private var storage: [Element?]
    private var head = 0
    public private(set) var count = 0

    public init(capacity: Int) {
        precondition(capacity > 0, "RingBuffer capacity must be positive")
        self.capacity = capacity
        storage = Array(repeating: nil, count: capacity)
    }

    public var isEmpty: Bool { count == 0 }

    public mutating func append(_ element: Element) {
        storage[(head + count) % capacity] = element
        if count < capacity {
            count += 1
        } else {
            head = (head + 1) % capacity
        }
    }

    public mutating func removeAll() {
        storage = Array(repeating: nil, count: capacity)
        head = 0
        count = 0
    }

    public var elements: [Element] { (0..<count).compactMap { storage[(head + $0) % capacity] } }
    public var last: Element? { count == 0 ? nil : storage[(head + count - 1) % capacity] }

    public func makeIterator() -> IndexingIterator<[Element]> { elements.makeIterator() }
}

extension RingBuffer: Sendable where Element: Sendable {}

public struct RssiPoint: Equatable, Sendable, Identifiable {
    public let date: Date
    /// nil marks a gap (client disconnected or reading unavailable).
    public let rssiDbm: Int?
    public let filteredRssiDbm: Int?

    public var id: Date { date }

    public init(date: Date, rssiDbm: Int?, filteredRssiDbm: Int?) {
        self.date = date
        self.rssiDbm = rssiDbm
        self.filteredRssiDbm = filteredRssiDbm
    }
}

/// Bounded per-client RSSI history. Both the number of clients and samples
/// per client are capped so a long session cannot grow memory unboundedly.
public struct RssiHistory: Sendable {
    public let maxClients: Int
    public let samplesPerClient: Int
    private var series: [UInt64: RingBuffer<RssiPoint>] = [:]
    private var lastUpdate: [UInt64: Date] = [:]

    public init(maxClients: Int = 8, samplesPerClient: Int = 300) {
        self.maxClients = max(1, maxClients)
        self.samplesPerClient = max(1, samplesPerClient)
    }

    public var clientIds: [UInt64] { series.keys.sorted() }
    public var totalSamples: Int { series.values.reduce(0) { $0 + $1.count } }

    public mutating func record(clientId: UInt64, rssiDbm: Int?, filteredRssiDbm: Int?, at date: Date) {
        if series[clientId] == nil, series.count >= maxClients,
           let stalest = lastUpdate.min(by: { $0.value < $1.value })?.key {
            series[stalest] = nil
            lastUpdate[stalest] = nil
        }
        var buffer = series[clientId] ?? RingBuffer(capacity: samplesPerClient)
        buffer.append(RssiPoint(date: date, rssiDbm: rssiDbm, filteredRssiDbm: filteredRssiDbm))
        series[clientId] = buffer
        lastUpdate[clientId] = date
    }

    public mutating func record(_ telemetry: RfTelemetry, at date: Date) {
        for client in telemetry.clients {
            record(clientId: client.clientId,
                   rssiDbm: client.connected ? client.rssiDbm : nil,
                   filteredRssiDbm: client.connected ? client.filteredRssiDbm : nil,
                   at: date)
        }
    }

    public func points(for clientId: UInt64) -> [RssiPoint] { series[clientId]?.elements ?? [] }

    public mutating func removeAll() {
        series.removeAll()
        lastUpdate.removeAll()
    }
}
