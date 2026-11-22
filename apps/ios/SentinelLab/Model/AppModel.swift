import Foundation
import Observation
import SentinelKit

/// One event shown in the UI, from the WebSocket stream or from `/api/events`.
struct EventItem: Identifiable, Equatable {
    enum Origin: String { case websocket = "WebSocket", http = "HTTP", demo = "DEMO" }

    let id: String
    let name: String
    let date: Date?
    let clientId: UInt64?
    let rssiDbm: Int?
    let message: String?
    let origin: Origin
}

/// Distance shown for this iPhone, with its provenance spelled out.
struct DistanceReading: Equatable {
    enum Kind: Equatable {
        /// Model fitted on the ESP32 from real measurements at known distances.
        case calibrated
        /// Default log-distance parameters; orientative only.
        case orientative
    }

    let clientId: UInt64
    let rssiDbm: Int?
    let filteredRssiDbm: Int?
    let meters: Double?
    let band: ClosedRange<Double>?
    let kind: Kind
    let model: LogDistanceModel
    let theoreticalMaxM: Double?
}

/// Calibration progress for the guided sheet.
struct CalibrationPoint: Identifiable, Equatable {
    let id = UUID()
    let meters: Double
    var recorded = false
}

/// Central state for the app. All mutation happens on the main actor; the
/// WebSocket and HTTP layers only deliver events into it.
@MainActor
@Observable
final class AppModel {
    // MARK: Configuration
    let settings: AppSettings
    private(set) var tokenConfigured: Bool
    private(set) var isDemo = false

    // MARK: Connection
    private(set) var machine = ConnectionStateMachine()
    private(set) var networkAvailable = true
    private(set) var onWiFi = true
    private(set) var lastFrameAt: Date?
    private(set) var connectedAt: Date?
    private(set) var firmwareSessionId: String?
    private(set) var lastProtocolError: ErrorPayload?

    // MARK: Device data (only from the device, or from DemoFeed when isDemo)
    private(set) var realtimeStatus: RealtimeStatus?
    private var previousStatus: RealtimeStatus?
    private(set) var deviceState: DeviceState?
    private(set) var rfSnapshot: RfSnapshot?
    private(set) var latestSystem: SystemStatus?
    private(set) var latestRf: RfTelemetry?
    private(set) var latestMotion: MotionStatus?
    private(set) var history = RssiHistory(maxClients: 8, samplesPerClient: 600)
    private(set) var events: [EventItem] = []
    private(set) var health: [HealthFinding] = []
    private(set) var lastHTTPError: String?
    private(set) var lastHTTPSuccess: Date?
    private(set) var isRefreshing = false

    // MARK: Diagnostics counters
    private(set) var framesByType: [String: Int] = [:]
    private(set) var decodeFailures = 0
    private(set) var lastDecodeError: String?

    // MARK: Motion (real CoreMotion data; never synthesized)
    let motion = MotionService()
    private(set) var motionSamplesSent = 0
    private(set) var motionLastSentAt: Date?
    private(set) var motionSendError: String?
    /// Last motion.sample relayed back by the ESP32 for this iPhone.
    private(set) var motionEchoAt: Date?
    private(set) var motionEchoMs2: Double?
    @ObservationIgnored private var motionSampleIndex = UInt64(Date().timeIntervalSince1970 * 1_000)

    // MARK: Distance
    private(set) var lab: LabSnapshot?
    private(set) var distanceTracker = SessionDistanceTracker()

    private static let maxEvents = 200
    private static let heartbeatInterval: Duration = .seconds(15)
    /// With a synced clock the firmware sends rf.telemetry every second and a
    /// heartbeat every 5 s; 30 s of silence means the stream is dead.
    private static let staleAfter: TimeInterval = 30

    @ObservationIgnored private var transport: RealtimeTransport?
    @ObservationIgnored private var session: ClientSession?
    @ObservationIgnored private var retryTask: Task<Void, Never>?
    @ObservationIgnored private var heartbeatTask: Task<Void, Never>?
    @ObservationIgnored private var pollTask: Task<Void, Never>?
    @ObservationIgnored private var demoTask: Task<Void, Never>?
    @ObservationIgnored private let monitor = NetworkMonitor()
    private let launchedAt = Date()
    private let appBootId = "app-" + UUID().uuidString.lowercased().prefix(8)

    init(settings: AppSettings = AppSettings()) {
        self.settings = settings
        tokenConfigured = KeychainStore.realtimeToken.read() != nil
        transport = RealtimeTransport { [weak self] event in self?.handleTransport(event) }
        motion.onTransmitSample = { [weak self] reading, filtered in self?.transmitMotion(reading, filteredMs2: filtered) }
    }

    // MARK: - Lifecycle

    func start() {
        motion.start()
        monitor.start { [weak self] available, wifi in self?.networkChanged(available: available, wifi: wifi) }
        startPolling()
        if settings.autoConnect { connect() }
    }

    func connect() {
        guard !isDemo else { return }
        apply(machine.handle(.start))
    }

    func disconnect() {
        if let data = try? session?.goodbye(reason: .clientRequest, at: Date()) {
            let transport = transport
            Task { try? await transport?.send(data) }
        }
        apply(machine.handle(.stop))
    }

    func reconnectNow() {
        apply(machine.handle(.stop))
        resetStreamState()
        connect()
    }

    func refresh() async {
        guard !isDemo else { return }
        await pollOnce(includeSlow: true)
    }

    func hostChanged() {
        resetDeviceData()
        if machine.isActive { reconnectNow() }
    }

    // MARK: - Motion streaming

    /// True when samples can reach the ESP32: authenticated session open.
    var canStreamMotion: Bool {
        guard !isDemo, tokenConfigured, session != nil else { return false }
        switch machine.phase {
        case .awaitingData, .streaming: return true
        default: return false
        }
    }

    var motionStreamLabel: String {
        if isDemo { return "Desactivado en modo DEMO" }
        if !settings.motionStreaming { return "Envío al ESP32 desactivado" }
        if !tokenConfigured { return "Requiere el token (Ajustes) para enviar al ESP32" }
        if !canStreamMotion { return "Esperando la sesión WebSocket" }
        if let echo = motionEchoAt, Date().timeIntervalSince(echo) < 3 { return "Enviando 5 Hz · eco del ESP32 confirmado" }
        return "Enviando 5 Hz al ESP32"
    }

    private func transmitMotion(_ reading: MotionReading, filteredMs2: Double) {
        guard settings.motionStreaming, canStreamMotion, var current = session, let transport else { return }
        motionSampleIndex += 1
        let payload = OutboundMotionSample(deviceId: settings.deviceId, sampleIndex: motionSampleIndex,
                                           reading: reading, filteredMs2: filteredMs2)
        let data: Data
        do {
            data = try current.motionSample(payload, at: Date())
        } catch {
            motionSendError = "Muestra descartada: \(error)"
            return
        }
        session = current
        Task { [weak self] in
            do {
                try await transport.send(data)
                self?.motionSamplesSent += 1
                self?.motionLastSentAt = Date()
                self?.motionSendError = nil
            } catch {
                self?.motionSendError = RealtimeTransport.describe(error)
            }
        }
    }

    // MARK: - Distance

    /// RF clients the ESP32 currently sees as connected (WS first, HTTP fallback).
    var connectedClientIds: [UInt64] {
        if let rf = latestRf, machine.phase == .streaming {
            return rf.clients.filter(\.connected).map(\.clientId)
        }
        return (rfSnapshot?.devices ?? []).filter { $0.connected == true }.map(\.id)
    }

    var thisClientId: UInt64? {
        ThisDeviceResolver.resolve(remembered: settings.thisClientId, connectedClientIds: connectedClientIds)
    }

    func identifyThisDevice(_ clientId: UInt64) {
        settings.thisClientId = clientId
        distanceTracker.reset()
    }

    var distance: DistanceReading? {
        guard let clientId = thisClientId else { return nil }
        var raw: Int?
        var filtered: Int?
        if let client = latestRf?.clients.first(where: { $0.clientId == clientId }), machine.phase == .streaming {
            raw = client.rssiDbm
            filtered = client.filteredRssiDbm
        } else if let device = rfSnapshot?.devices?.first(where: { $0.id == clientId }) {
            raw = device.rssiDbm.map { Int($0.rounded()) }
            filtered = device.filteredDbm.map { Int($0.rounded()) }
        }
        let rssi = filtered ?? raw
        if let calibrated = lab?.calibratedExperiment(clientId: clientId)?.model {
            return DistanceReading(clientId: clientId, rssiDbm: raw, filteredRssiDbm: filtered,
                                   meters: rssi.flatMap { calibrated.distance(rssiDbm: Double($0)) }, band: nil,
                                   kind: .calibrated, model: calibrated,
                                   theoreticalMaxM: calibrated.theoreticalMaxRange())
        }
        let model = LogDistanceModel.uncalibrated
        return DistanceReading(clientId: clientId, rssiDbm: raw, filteredRssiDbm: filtered,
                               meters: rssi.flatMap { model.distance(rssiDbm: Double($0)) },
                               band: rssi.flatMap { model.distanceBand(rssiDbm: Double($0)) },
                               kind: .orientative, model: model, theoreticalMaxM: model.theoreticalMaxRange())
    }

    private func recordDistance() {
        guard !isDemo, let reading = distance, let meters = reading.meters else { return }
        distanceTracker.record(meters: meters, calibrated: reading.kind == .calibrated)
    }

    func resetDistanceSession() { distanceTracker.reset() }

    private var httpClient: DeviceHTTPClient { DeviceHTTPClient(host: settings.host.trimmingCharacters(in: .whitespaces)) }

    func startCalibration(environment: Int) async throws -> Int {
        guard let clientId = thisClientId else {
            throw DeviceHTTPClient.HTTPError.rejected(0, "name, clientId and environment required")
        }
        let stamp = Date().formatted(.dateTime.hour().minute())
        return try await httpClient.createExperiment(name: "iPhone distancia \(stamp)", clientId: clientId,
                                                     environment: environment)
    }

    /// Waits so the ESP32's RSSI filter settles at the new position, then records.
    func recordCalibrationPoint(experimentId: Int, meters: Double) async throws {
        try await Task.sleep(for: .seconds(3))
        try await httpClient.recordCalibrationPoint(experimentId: experimentId, meters: meters,
                                                    notes: "app iOS, punto a \(meters) m")
    }

    func finishCalibration(experimentId: Int) async throws -> LabSnapshot.Experiment? {
        try await httpClient.calibrate(experimentId: experimentId)
        let snapshot = try await httpClient.lab()
        lab = snapshot
        distanceTracker.reset()
        return snapshot.experiments?.first { $0.id == experimentId }
    }

    // MARK: - Token

    func saveToken(_ token: String) throws {
        let trimmed = token.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            try KeychainStore.realtimeToken.delete()
        } else {
            try KeychainStore.realtimeToken.save(trimmed)
        }
        tokenConfigured = !trimmed.isEmpty
        if machine.isActive { reconnectNow() }
    }

    func removeToken() throws {
        try KeychainStore.realtimeToken.delete()
        tokenConfigured = false
        if machine.isActive { reconnectNow() }
    }

    // MARK: - Demo

    func setDemo(_ enabled: Bool) {
        guard enabled != isDemo else { return }
        apply(machine.handle(.stop))
        resetDeviceData()
        isDemo = enabled
        if enabled {
            pollTask?.cancel()
            startDemo()
        } else {
            demoTask?.cancel()
            demoTask = nil
            startPolling()
            if settings.autoConnect { connect() }
        }
    }

    private func startDemo() {
        demoTask?.cancel()
        let started = Date()
        demoTask = Task { [weak self] in
            var feed = DemoFeed()
            while !Task.isCancelled {
                let now = Date()
                guard let self else { return }
                self.acceptStatus(DemoFeed.status(at: now, started: started))
                for frame in feed.frames(at: now) { self.handleFrame(frame, origin: .demo) }
                try? await Task.sleep(for: .seconds(1))
            }
        }
    }

    // MARK: - Derived state

    var phaseLabel: String {
        if isDemo { return "DEMO" }
        switch machine.phase {
        case .idle: return "Desconectado"
        case .connecting(let attempt): return attempt > 1 ? "Conectando (intento \(attempt))" : "Conectando"
        case .awaitingData: return "Conectado, esperando datos"
        case .streaming: return "Transmitiendo"
        case .waitingToRetry(_, let delay): return "Reintentando en \(Int(delay.rounded(.up))) s"
        case .offline: return "Sin red"
        }
    }

    /// Plain-language reason why a connected socket is silent.
    var silenceExplanation: String? {
        guard !isDemo, machine.phase == .awaitingData else { return nil }
        if realtimeStatus?.clockSynced == false {
            return tokenConfigured
                ? "El reloj del ESP32 aún no está sincronizado. Si persiste, verifica que el token coincida con el del firmware."
                : "El ESP32 no transmite hasta que su reloj UTC esté sincronizado. Guarda el token en Ajustes para que esta app lo sincronice, o ejecuta el gateway."
        }
        return "Handshake completado; esperando la primera trama."
    }

    var overallHealth: HealthLevel { HealthEvaluator.overall(health) }

    var dataSourceLabel: String {
        if isDemo { return "DEMO · datos sintéticos" }
        if machine.phase == .streaming { return "WebSocket sentinel.v1" }
        if lastHTTPSuccess != nil { return "HTTP (sondeo)" }
        return "Sin datos"
    }

    // MARK: - Transport events

    private func apply(_ effects: [ConnectionEffect]) {
        for effect in effects {
            switch effect {
            case .openTransport:
                openTransport()
            case .closeTransport:
                heartbeatTask?.cancel()
                heartbeatTask = nil
                transport?.close()
                connectedAt = nil
            case .scheduleRetry(let delay):
                retryTask?.cancel()
                retryTask = Task { [weak self] in
                    try? await Task.sleep(for: .seconds(delay))
                    guard !Task.isCancelled else { return }
                    self?.apply(self?.machine.handle(.retryTimerFired) ?? [])
                }
            case .cancelRetry:
                retryTask?.cancel()
                retryTask = nil
            }
        }
    }

    private func openTransport() {
        session = try? ClientSession()
        firmwareSessionId = nil
        transport?.open(host: settings.host.trimmingCharacters(in: .whitespaces),
                       token: KeychainStore.realtimeToken.read())
    }

    private func handleTransport(_ event: RealtimeTransport.Event) {
        switch event {
        case .opened:
            apply(machine.handle(.transportOpened))
            connectedAt = Date()
            sendHello()
            startHeartbeat()
        case .text(let data):
            handleFrame(data, origin: .websocket)
        case .failed(let reason):
            apply(machine.handle(.transportFailed(reason)))
        }
    }

    private func sendHello() {
        // Sent immediately with the iPhone's UTC time: with a valid token this
        // authenticated hello is what synchronizes the ESP32 clock.
        guard var current = session, let data = try? current.hello(deviceId: settings.deviceId, at: Date()) else { return }
        session = current
        guard let transport else { return }
        Task { [weak self] in
            do { try await transport.send(data) } catch {
                self?.apply(self?.machine.handle(.transportFailed(RealtimeTransport.describe(error))) ?? [])
            }
        }
    }

    private func startHeartbeat() {
        heartbeatTask?.cancel()
        heartbeatTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: AppModel.heartbeatInterval)
                guard !Task.isCancelled, let self else { return }
                self.heartbeatTick()
            }
        }
    }

    private func heartbeatTick() {
        if let last = lastFrameAt ?? connectedAt, machine.phase == .streaming,
           Date().timeIntervalSince(last) > Self.staleAfter {
            apply(machine.handle(.transportFailed("Sin tramas durante \(Int(Self.staleAfter)) s.")))
            return
        }
        let uptime = UInt64(Date().timeIntervalSince(launchedAt) * 1_000)
        guard var current = session,
              let data = try? current.heartbeat(bootId: appBootId, uptimeMs: uptime, at: Date()) else { return }
        session = current
        guard let transport else { return }
        Task { [weak self] in
            do {
                try await transport.send(data)
                try await transport.ping()
            } catch {
                self?.apply(self?.machine.handle(.transportFailed(RealtimeTransport.describe(error))) ?? [])
            }
        }
    }

    private func handleFrame(_ data: Data, origin: EventItem.Origin) {
        let message: ProtocolMessage
        do {
            message = try ProtocolDecoder.decode(data)
        } catch {
            decodeFailures += 1
            lastDecodeError = (error as? ProtocolDecodingError)?.description ?? "JSON inválido"
            return
        }
        framesByType[message.type, default: 0] += 1
        lastFrameAt = Date()
        if origin == .websocket {
            if firmwareSessionId == nil { firmwareSessionId = message.sessionId }
            apply(machine.handle(.frameReceived))
        }

        switch message.body {
        case .rfTelemetry(let rf):
            latestRf = rf
            history.record(rf, at: message.timestamp)
            recordDistance()
        case .systemStatus(let status):
            latestSystem = status
        case .motionStatus(let status):
            latestMotion = status
        case .event(let event):
            appendEvent(EventItem(id: event.eventId, name: event.name.rawValue, date: event.occurredAt,
                                  clientId: event.details?.clientId, rssiDbm: event.details?.rssiDbm,
                                  message: event.details?.message, origin: origin))
        case .error(let error):
            lastProtocolError = error
        case .goodbye(let goodbye):
            apply(machine.handle(.transportFailed("El ESP32 cerró la sesión (\(goodbye.reason.rawValue)).")))
        case .motionSample(let sample):
            if sample.deviceId == settings.deviceId {
                motionEchoAt = Date()
                motionEchoMs2 = (sample.linearAcceleration.x * sample.linearAcceleration.x
                    + sample.linearAcceleration.y * sample.linearAcceleration.y
                    + sample.linearAcceleration.z * sample.linearAcceleration.z).squareRoot()
            }
        case .hello, .heartbeat, .unknown:
            break
        }
    }

    private func appendEvent(_ item: EventItem) {
        guard !events.contains(where: { $0.id == item.id }) else { return }
        events.append(item)
        if events.count > Self.maxEvents { events.removeFirst(events.count - Self.maxEvents) }
    }

    // MARK: - HTTP polling

    private func startPolling() {
        pollTask?.cancel()
        pollTask = Task { [weak self] in
            var tick = 0
            while !Task.isCancelled {
                guard let self else { return }
                await self.pollOnce(includeSlow: tick % 3 == 0)
                tick += 1
                let interval = max(1, self.settings.pollIntervalSeconds)
                try? await Task.sleep(for: .seconds(interval))
            }
        }
    }

    private func pollOnce(includeSlow: Bool) async {
        guard !isDemo, AppSettings.isValidHost(settings.host) else { return }
        isRefreshing = true
        defer { isRefreshing = false }
        let client = DeviceHTTPClient(host: settings.host.trimmingCharacters(in: .whitespaces))
        var errors: [String] = []

        do {
            acceptStatus(try await client.realtimeStatus())
        } catch {
            errors.append(Self.message(error))
        }
        do {
            let rf = try await client.rf()
            rfSnapshot = rf
            if machine.phase != .streaming { recordDistance() }
            // HTTP history only fills the chart while the WebSocket is silent,
            // so the two sources never double-count a sample.
            if machine.phase != .streaming, let devices = rf.devices {
                let now = Date()
                for device in devices {
                    history.record(clientId: device.id,
                                   rssiDbm: device.connected == true ? device.rssiDbm.map { Int($0.rounded()) } : nil,
                                   filteredRssiDbm: device.connected == true ? device.filteredDbm.map { Int($0.rounded()) } : nil,
                                   at: now)
                }
            }
        } catch {
            errors.append(Self.message(error))
        }
        if includeSlow {
            do { deviceState = try await client.deviceState() } catch { errors.append(Self.message(error)) }
            // Lab is optional (older images may differ); never count it as a failure.
            if let snapshot = try? await client.lab() { lab = snapshot }
            do {
                let log = try await client.events()
                for entry in log.events ?? [] where entry.simulated != true {
                    appendEvent(EventItem(id: "http-\(log.bootId ?? "boot")-\(entry.sequence)",
                                          name: entry.event ?? "EVENTO", date: nil, clientId: entry.clientId,
                                          rssiDbm: entry.rssiDbm.map { Int($0.rounded()) }, message: nil,
                                          origin: .http))
                }
            } catch {
                errors.append(Self.message(error))
            }
        }

        if errors.isEmpty {
            lastHTTPError = nil
        } else {
            lastHTTPError = errors.first
        }
        if errors.count < (includeSlow ? 4 : 2) { lastHTTPSuccess = Date() }
    }

    private func acceptStatus(_ status: RealtimeStatus) {
        previousStatus = realtimeStatus
        if let previous = previousStatus, previous.bootId != status.bootId, status.bootId != nil {
            appendEvent(EventItem(id: "reboot-\(status.bootId ?? "")", name: "REINICIO DETECTADO", date: Date(),
                                  clientId: nil, rssiDbm: nil, message: "bootId cambió", origin: isDemo ? .demo : .http))
        }
        realtimeStatus = status
        health = HealthEvaluator.evaluate(current: status, previous: previousStatus)
    }

    private static func message(_ error: Error) -> String {
        if let error = error as? LocalizedError, let text = error.errorDescription { return text }
        return RealtimeTransport.describe(error)
    }

    // MARK: - Network

    private func networkChanged(available: Bool, wifi: Bool) {
        networkAvailable = available
        onWiFi = wifi
        guard !isDemo else { return }
        apply(machine.handle(available ? .networkRestored : .networkLost))
    }

    // MARK: - Evidence

    func evidenceData() throws -> Data {
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "?"
        let build = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "?"
        let report = EvidenceReport(
            generatedAt: Date(), appVersion: "\(version) (\(build))", demo: isDemo, host: settings.host,
            realtimeStatus: realtimeStatus, deviceState: deviceState,
            connection: .init(phase: phaseLabel, failures: machine.failures, reconnects: machine.reconnects,
                              lastFailure: machine.lastFailure, tokenConfigured: tokenConfigured),
            framesByType: framesByType, decodeFailures: decodeFailures, health: health,
            recentEvents: events.map { item in
                EvidenceReport.EventRecord(name: item.name,
                                           occurredAt: item.date.map { ProtocolTimestamp.format($0) } ?? "",
                                           clientId: item.clientId)
            })
        let secrets = [KeychainStore.realtimeToken.read()].compactMap { $0 }
        return try EvidenceExporter.export(report, secrets: secrets)
    }

    func evidenceFile() throws -> URL {
        let stamp = ProtocolTimestamp.format(Date()).replacingOccurrences(of: ":", with: "-")
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("sentinel-evidence-\(isDemo ? "DEMO-" : "")\(stamp).json")
        try evidenceData().write(to: url, options: .atomic)
        return url
    }

    // MARK: - Reset

    private func resetStreamState() {
        lastFrameAt = nil
        firmwareSessionId = nil
        lastProtocolError = nil
    }

    private func resetDeviceData() {
        resetStreamState()
        realtimeStatus = nil
        previousStatus = nil
        deviceState = nil
        rfSnapshot = nil
        latestSystem = nil
        latestRf = nil
        latestMotion = nil
        lab = nil
        distanceTracker.reset()
        motionEchoAt = nil
        motionEchoMs2 = nil
        history.removeAll()
        events.removeAll()
        health = []
        framesByType = [:]
        decodeFailures = 0
        lastDecodeError = nil
        lastHTTPError = nil
        lastHTTPSuccess = nil
    }
}
