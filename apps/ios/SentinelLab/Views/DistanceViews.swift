import SentinelKit
import SwiftUI

/// Distance between this iPhone and the ESP32, derived from the RSSI the
/// ESP32 measures for this phone. Always labeled as an estimate.
struct DistanceCard: View {
    @Environment(AppModel.self) private var model
    @State private var showCalibration = false

    var body: some View {
        SectionCard(title: "Distancia al ESP32", symbol: "ruler") {
            if let reading = model.distance {
                content(reading)
            } else if model.connectedClientIds.count > 1 {
                picker
            } else {
                Text("El ESP32 aún no informa ninguna estación conectada. Une este iPhone a la Wi-Fi SENTINEL-LAB.")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .sheet(isPresented: $showCalibration) { CalibrationSheet() }
    }

    @ViewBuilder
    private func content(_ reading: DistanceReading) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text(reading.meters.map { String(format: "%.1f", $0) } ?? "—")
                .font(.system(size: 48, weight: .bold, design: .rounded).monospacedDigit())
                .foregroundStyle(reading.kind == .calibrated ? LabPalette.signal : .primary)
            Text("m").font(.title2.weight(.semibold)).foregroundStyle(.secondary)
            Spacer()
            kindBadge(reading.kind)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Distancia estimada \(reading.meters.map { String(format: "%.1f", $0) } ?? "desconocida") metros")

        if let band = reading.band {
            Text(String(format: "Rango plausible %.1f – %.1f m (exponente n de 2,0 a 3,5)", band.lowerBound, band.upperBound))
                .font(.caption).foregroundStyle(.secondary)
        }
        MetricGrid {
            MetricTile(title: "RSSI medido por el ESP32", value: Format.dbm(reading.rssiDbm), symbol: "antenna.radiowaves.left.and.right",
                       footnote: "Filtrado: \(Format.dbm(reading.filteredRssiDbm))")
            MetricTile(title: "Alcance máximo teórico",
                       value: reading.theoreticalMaxM.map { String(format: "%.0f m", $0) } ?? "—",
                       symbol: "scope", tint: LabPalette.amber,
                       footnote: "Donde el modelo llega a \(Int(LogDistanceModel.sensitivityDbm)) dBm")
            MetricTile(title: "Máxima estimada en la sesión", value: sessionMax(reading.kind), symbol: "figure.walk.motion",
                       footnote: reading.kind == .calibrated ? "Con el modelo calibrado" : "Con el modelo orientativo")
            MetricTile(title: "Modelo", value: String(format: "A %.1f dBm · n %.2f", reading.model.referenceRssiAt1m,
                                                      reading.model.exponent),
                       symbol: "function", footnote: "RSSI = A − 10·n·log₁₀(d)")
        }
        HStack {
            Button {
                showCalibration = true
            } label: {
                Label(reading.kind == .calibrated ? "Recalibrar" : "Calibrar con distancias reales",
                      systemImage: "scope")
            }
            .buttonStyle(.borderedProminent)
            .tint(LabPalette.signal)
            Spacer()
            Button("Reiniciar máximo") { model.resetDistanceSession() }
                .buttonStyle(.bordered)
        }
        if model.connectedClientIds.count > 1 { picker }
        Text(reading.kind == .calibrated
             ? "Estimación calibrada: modelo ajustado en el ESP32 con tus mediciones a distancias conocidas. Sigue siendo una estimación; obstáculos y orientación del cuerpo cambian el RSSI."
             : "Estimación orientativa sin calibrar (A = −40 dBm a 1 m, n = 2,7). El RSSI no determina la distancia exacta; calibra para obtener un modelo de tu entorno.")
            .font(.footnote).foregroundStyle(.secondary)
    }

    private var picker: some View {
        return Picker("Este iPhone es la estación", selection: Binding(
            get: { model.thisClientId ?? 0 },
            set: { model.identifyThisDevice($0) })) {
            if model.thisClientId == nil { Text("Elegir…").tag(UInt64(0)) }
            ForEach(model.connectedClientIds, id: \.self) { id in
                Text("Cliente \(String(id))").tag(id)
            }
        }
        .pickerStyle(.menu)
    }

    private func kindBadge(_ kind: DistanceReading.Kind) -> some View {
        let calibrated = kind == .calibrated
        return Text(calibrated ? "ESTIMACIÓN CALIBRADA" : "ORIENTATIVA · SIN CALIBRAR")
            .font(.caption2.weight(.bold))
            .padding(.horizontal, 8).padding(.vertical, 4)
            .foregroundStyle(calibrated ? LabPalette.signal : LabPalette.amber)
            .background((calibrated ? LabPalette.signal : LabPalette.amber).opacity(0.15), in: Capsule())
    }

    private func sessionMax(_ kind: DistanceReading.Kind) -> String {
        let value = kind == .calibrated ? model.distanceTracker.maxCalibratedM : model.distanceTracker.maxOrientativeM
        return value.map { String(format: "%.1f m", $0) } ?? "—"
    }
}

/// Guided calibration: measure RSSI at known distances on the ESP32 and let
/// the firmware fit the log-distance model.
struct CalibrationSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss

    @State private var distances: [Double] = [1, 2, 4]
    @State private var recorded: Set<Int> = []
    @State private var environment = 1
    @State private var experimentId: Int?
    @State private var busyIndex: Int?
    @State private var isFitting = false
    @State private var errorText: String?
    @State private var result: LabSnapshot.Experiment?

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("Coloca el iPhone a cada distancia medida con cinta métrica desde el ESP32, a la misma altura y sin cuerpos entre ambos. En cada punto pulsa «Medir»: el ESP32 espera 3 s y guarda su RSSI filtrado.")
                        .font(.subheadline)
                    Picker("Entorno", selection: $environment) {
                        Text("Espacio abierto").tag(0)
                        Text("Interior").tag(1)
                        Text("Con obstáculos").tag(2)
                    }
                    .disabled(experimentId != nil)
                }
                Section("Puntos de calibración") {
                    ForEach(distances.indices, id: \.self) { index in
                        HStack {
                            Stepper(value: $distances[index], in: 0.5...30, step: 0.5) {
                                Text(String(format: "%.1f m", distances[index])).monospacedDigit()
                            }
                            .disabled(recorded.contains(index))
                            if recorded.contains(index) {
                                Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                                    .accessibilityLabel("Medido")
                            } else if busyIndex == index {
                                ProgressView()
                            } else {
                                Button("Medir") { Task { await measure(index) } }
                                    .buttonStyle(.borderedProminent)
                                    .disabled(busyIndex != nil || isFitting)
                            }
                        }
                    }
                    if distances.count < 6 && experimentId == nil {
                        Button("Añadir punto") { distances.append((distances.last ?? 1) * 2) }
                    }
                }
                Section {
                    Button {
                        Task { await fit() }
                    } label: {
                        if isFitting { ProgressView() } else { Text("Ajustar modelo en el ESP32") }
                    }
                    .disabled(recorded.count < 2 || isFitting || busyIndex != nil)
                }
                if let result, let fitted = result.model {
                    Section("Modelo calibrado") {
                        KeyValueRow(key: "A (RSSI a 1 m)", value: String(format: "%.1f dBm", fitted.referenceRssiAt1m))
                        KeyValueRow(key: "Exponente n", value: String(format: "%.2f", fitted.exponent))
                        KeyValueRow(key: "Alcance teórico", value: fitted.theoreticalMaxRange().map { String(format: "%.0f m", $0) } ?? "—")
                        Text("Guardado en la RAM del ESP32 (se pierde al reiniciar; exporta desde la web si quieres conservarlo).")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
                if let errorText {
                    Section { Label(errorText, systemImage: "exclamationmark.triangle").foregroundStyle(.orange) }
                }
            }
            .navigationTitle("Calibrar distancia")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Cerrar") { dismiss() } }
            }
        }
    }

    private func measure(_ index: Int) async {
        busyIndex = index
        errorText = nil
        defer { busyIndex = nil }
        do {
            let id: Int
            if let experimentId {
                id = experimentId
            } else {
                id = try await model.startCalibration(environment: environment)
                experimentId = id
            }
            try await model.recordCalibrationPoint(experimentId: id, meters: distances[index])
            recorded.insert(index)
        } catch {
            errorText = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }

    private func fit() async {
        guard let experimentId else { return }
        isFitting = true
        errorText = nil
        defer { isFitting = false }
        do {
            result = try await model.finishCalibration(experimentId: experimentId)
            if result?.model == nil { errorText = "El ESP32 no devolvió un modelo válido; repite con distancias más separadas." }
        } catch {
            errorText = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }
}
