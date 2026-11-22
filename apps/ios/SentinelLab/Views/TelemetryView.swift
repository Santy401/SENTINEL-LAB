import Charts
import SwiftUI
import SentinelKit

struct TelemetryView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    if model.isDemo { DemoBanner() }
                    if model.history.clientIds.isEmpty {
                        ContentUnavailableView {
                            Label("Sin observaciones RF", systemImage: "waveform.path.ecg")
                        } description: {
                            Text("Aparecerán cuando el ESP32 informe estaciones asociadas a su punto de acceso, por WebSocket o por /api/rf.")
                        }
                        .frame(minHeight: 240)
                    } else {
                        chartCard
                        clientsCard
                    }
                    eventsCard
                    Text("RSSI es una medida de potencia recibida, no una distancia. Las distancias solo se muestran si el firmware las marca como calibradas.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                .padding()
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("Telemetría")
        }
    }

    private var windowStart: Date {
        Date().addingTimeInterval(-Double(model.settings.chartWindowMinutes) * 60)
    }

    private struct ChartPoint: Identifiable {
        let id: String
        let label: String
        let date: Date
        let rssi: Int
    }

    /// Flattened, windowed points; gaps (nil RSSI) are dropped from the line.
    private var chartPoints: [ChartPoint] {
        let start = windowStart
        let useFiltered = model.settings.showFilteredRssi
        return model.history.clientIds.flatMap { clientId in
            model.history.points(for: clientId).compactMap { point -> ChartPoint? in
                guard point.date >= start,
                      let rssi = useFiltered ? (point.filteredRssiDbm ?? point.rssiDbm) : point.rssiDbm else { return nil }
                return ChartPoint(id: "\(clientId)-\(point.date.timeIntervalSince1970)", label: "STA \(clientId)",
                                  date: point.date, rssi: rssi)
            }
        }
    }

    private var chartCard: some View {
        SectionCard(title: "RSSI por estación", symbol: "chart.xyaxis.line") {
            Text("Fuente: \(model.dataSourceLabel) · ventana \(model.settings.chartWindowMinutes) min")
                .font(.caption).foregroundStyle(.secondary)
            Chart(chartPoints) { point in
                LineMark(x: .value("Hora", point.date), y: .value("RSSI (dBm)", point.rssi),
                         series: .value("Estación", point.label))
                    .foregroundStyle(by: .value("Estación", point.label))
                    .interpolationMethod(.monotone)
            }
            .chartYScale(domain: -100 ... -20)
            .chartYAxisLabel("dBm")
            .frame(height: 240)
            .accessibilityLabel("Gráfica de RSSI por estación en los últimos \(model.settings.chartWindowMinutes) minutos")
        }
    }

    private var clientsCard: some View {
        SectionCard(title: "Estaciones", symbol: "wifi") {
            if let rf = model.latestRf {
                ForEach(rf.clients) { client in
                    HStack {
                        Image(systemName: client.connected ? "wifi" : "wifi.slash")
                            .foregroundStyle(client.connected ? .green : .secondary)
                        VStack(alignment: .leading) {
                            Text("STA \(client.clientId)").font(.subheadline.weight(.semibold))
                            Text("Visto \(Format.time(client.lastSeen))").font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        VStack(alignment: .trailing) {
                            Text(Format.dbm(client.rssiDbm)).font(.body.monospacedDigit())
                            if let distance = client.estimatedDistance, distance.calibrated {
                                Text(String(format: "≈ %.1f m (calibrado)", distance.valueM))
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                    .accessibilityElement(children: .combine)
                }
            } else if let devices = model.rfSnapshot?.devices, !devices.isEmpty {
                ForEach(devices) { device in
                    HStack {
                        Image(systemName: device.connected == true ? "wifi" : "wifi.slash")
                            .foregroundStyle(device.connected == true ? .green : .secondary)
                        Text("STA \(device.id)").font(.subheadline.weight(.semibold))
                        Spacer()
                        Text(Format.dbm(device.rssiDbm.map { Int($0.rounded()) })).font(.body.monospacedDigit())
                    }
                    .accessibilityElement(children: .combine)
                }
                Text("Datos de /api/rf (HTTP).").font(.caption).foregroundStyle(.secondary)
            } else {
                Text("Sin estaciones informadas.").foregroundStyle(.secondary)
            }
        }
    }

    private var eventsCard: some View {
        SectionCard(title: "Eventos recientes", symbol: "bell.badge") {
            if model.events.isEmpty {
                Text("Sin eventos todavía.").font(.subheadline).foregroundStyle(.secondary)
            } else {
                ForEach(model.events.suffix(25).reversed()) { event in
                    HStack(alignment: .top) {
                        Image(systemName: icon(for: event.name)).foregroundStyle(.tint)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(event.name).font(.subheadline.weight(.semibold))
                            Text([event.clientId.map { "STA \($0)" }, event.rssiDbm.map { Format.dbm($0) }, event.message]
                                .compactMap { $0 }.joined(separator: " · "))
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        VStack(alignment: .trailing, spacing: 2) {
                            Text(Format.time(event.date)).font(.caption.monospacedDigit())
                            Text(event.origin.rawValue).font(.caption2).foregroundStyle(.secondary)
                        }
                    }
                    .accessibilityElement(children: .combine)
                }
            }
        }
    }

    private func icon(for name: String) -> String {
        switch name {
        case "DEVICE_CONNECTED": return "link"
        case "DEVICE_DISCONNECTED": return "link.badge.plus"
        case "SIGNAL_WEAK": return "wifi.exclamationmark"
        case "SIGNAL_RECOVERED": return "wifi"
        case "MOTION_ALERT", "MOVEMENT_DETECTED": return "figure.walk.motion"
        default: return "circle.fill"
        }
    }
}
