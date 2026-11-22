import SwiftUI
import SentinelKit

struct DashboardView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    if model.isDemo { DemoBanner() }
                    BoardCard()
                    if !model.alerts.isEmpty { AlertsCard() }
                    connectionCard
                    if model.realtimeStatus == nil && model.deviceState == nil && !model.isDemo {
                        noDeviceState
                    } else {
                        physicalCard
                        DistanceCard()
                        metrics
                        healthCard
                    }
                }
                .padding()
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("SENTINEL-LAB")
            .refreshable { await model.refresh() }
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    if model.isRefreshing { ProgressView().accessibilityLabel("Actualizando") }
                }
            }
        }
    }

    private var connectionCard: some View {
        SectionCard(title: "Conexión", symbol: "antenna.radiowaves.left.and.right") {
            HStack {
                StatusPill(text: model.phaseLabel, symbol: model.machine.phase.symbol,
                           color: model.isDemo ? .purple : model.machine.phase.color)
                Spacer()
                if model.machine.isActive {
                    Button("Desconectar", role: .destructive) { model.disconnect() }
                        .buttonStyle(.bordered)
                } else if !model.isDemo {
                    Button("Conectar") { model.connect() }
                        .buttonStyle(.borderedProminent)
                }
            }
            KeyValueRow(key: "Dispositivo", value: model.settings.host)
            KeyValueRow(key: "Fuente de datos", value: model.dataSourceLabel)
            KeyValueRow(key: "Última trama", value: Format.relative(model.lastFrameAt))
            if !model.networkAvailable {
                Label("El iPhone no tiene una red local disponible.", systemImage: "wifi.slash")
                    .foregroundStyle(.red).font(.subheadline)
            }
            if let explanation = model.silenceExplanation {
                Label(explanation, systemImage: "clock.badge.exclamationmark")
                    .foregroundStyle(.orange).font(.subheadline)
            }
            if let failure = model.machine.lastFailure, model.machine.phase != .streaming {
                Label(failure, systemImage: "exclamationmark.bubble")
                    .foregroundStyle(.secondary).font(.footnote)
            }
            if let error = model.lastHTTPError {
                Label(error, systemImage: "network.slash")
                    .foregroundStyle(.secondary).font(.footnote)
            }
        }
    }

    /// Live link between the phone's motion and the ESP32's physical RGB LED.
    private var physicalCard: some View {
        SectionCard(title: "Laboratorio físico", symbol: "lightbulb.led") {
            VirtualLed(rgb: model.realtimeStatus?.rgb)
            let metrics = model.motion.snapshot.metrics
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Agitación del iPhone").font(.caption).foregroundStyle(.secondary)
                    Text(String(format: "%.2f m/s² · %.3f N", metrics.rmsMs2, metrics.forceNewtons))
                        .font(.title3.weight(.semibold).monospacedDigit())
                        .foregroundStyle(LabPalette.intensityColor(metrics.intensity))
                }
                Spacer()
                Gauge(value: metrics.intensity) { Text("Intensidad") }
                    .gaugeStyle(.accessoryCircularCapacity)
                    .tint(LabPalette.intensityColor(metrics.intensity))
            }
            Text(model.motionStreamLabel).font(.caption).foregroundStyle(.secondary)
        }
    }

    private var noDeviceState: some View {
        ContentUnavailableView {
            Label("Sin datos del ESP32", systemImage: "cpu")
        } description: {
            Text("Conecta el iPhone a la red Wi-Fi SENTINEL-LAB y verifica la dirección en Ajustes (por defecto 192.168.4.1).")
        } actions: {
            Button("Reintentar") { Task { await model.refresh() } }
                .buttonStyle(.borderedProminent)
        }
        .frame(minHeight: 260)
    }

    private var metrics: some View {
        let status = model.realtimeStatus
        return MetricGrid {
            MetricTile(title: "Firmware", value: status?.imageVersion ?? model.deviceState?.fw ?? "—",
                       symbol: "memorychip",
                       footnote: status?.httpApiVersion.map { "API HTTP \($0)" } ?? status?.fw.map { "fw \($0)" })
            MetricTile(title: "Uptime", value: Format.uptime(status?.uptimeMs ?? model.deviceState?.uptimeMs),
                       symbol: "timer", footnote: status?.resetReason.map { "Reinicio: \($0)" })
            MetricTile(title: "Heap libre", value: Format.bytes(status?.freeHeapBytes), symbol: "gauge.with.dots.needle.50percent",
                       footnote: status?.minFreeHeapBytes.map { "Mínimo \(Format.bytes($0))" })
            MetricTile(title: "Reloj UTC", value: clockText(status), symbol: "clock",
                       tint: status?.clockSynced == true ? .green : .orange,
                       footnote: status?.clockSource.map { "Fuente: \($0)" })
            MetricTile(title: "Clientes WS", value: status?.clientCount.map { "\($0) / 4" } ?? "—",
                       symbol: "person.2.wave.2")
            MetricTile(title: "Estaciones Wi-Fi", value: model.rfSnapshot?.clients.map(String.init) ?? model.latestRf.map { "\($0.clientCount)" } ?? "—",
                       symbol: "wifi", footnote: model.rfSnapshot?.channel.map { "Canal \($0)" })
        }
    }

    private func clockText(_ status: RealtimeStatus?) -> String {
        guard let synced = status?.clockSynced else { return "—" }
        return synced ? "Sincronizado" : "Sin sincronizar"
    }

    private var healthCard: some View {
        SectionCard(title: "Salud", symbol: "stethoscope") {
            StatusPill(text: model.overallHealth.label, symbol: model.overallHealth.symbol,
                       color: model.overallHealth.color)
            if model.health.isEmpty {
                Text(model.realtimeStatus == nil
                     ? "Sin lectura de /api/realtime/status."
                     : "Sin hallazgos en la última lectura.")
                    .font(.subheadline).foregroundStyle(.secondary)
            } else {
                ForEach(model.health) { HealthFindingRow(finding: $0) }
            }
        }
    }
}
