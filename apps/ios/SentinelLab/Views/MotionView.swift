import Charts
import SentinelKit
import SwiftUI

/// Physics lab: real CoreMotion readings of this iPhone. Acceleration is
/// shown as acceleration; nothing is integrated into velocity or position.
struct MotionView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var settings = model.settings
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    if let reason = model.motion.unavailableReason {
                        ContentUnavailableView("Sensores no disponibles", systemImage: "gyroscope",
                                               description: Text(reason))
                    } else {
                        Picker("Vista", selection: $settings.immersiveMotion) {
                            Text("Inmersiva").tag(true)
                            Text("Científica").tag(false)
                        }
                        .pickerStyle(.segmented)
                        .accessibilityLabel("Modo de visualización")

                        if settings.immersiveMotion {
                            immersive
                        } else {
                            scientific
                        }
                        streamCard
                        explanation
                    }
                }
                .padding()
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("Movimiento")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        model.motion.resetSession()
                    } label: {
                        Label("Reiniciar sesión", systemImage: "arrow.counterclockwise")
                    }
                }
            }
        }
    }

    private var snapshot: MotionSnapshot { model.motion.snapshot }

    // MARK: Immersive

    private var immersive: some View {
        VStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 22)
                    .fill(LinearGradient(colors: [LabPalette.navy, LabPalette.navy.opacity(0.85),
                                                  LabPalette.intensityColor(snapshot.metrics.intensity).opacity(0.35)],
                                         startPoint: .top, endPoint: .bottom))
                VStack(spacing: 6) {
                    Orientation3DView(attitude: snapshot.attitude, gravity: snapshot.gravity,
                                      intensity: snapshot.metrics.intensity)
                        .frame(height: 240)
                    axisLegend.foregroundStyle(.white.opacity(0.85))
                }
                .padding(.vertical, 10)
            }
            .frame(height: 290)

            SectionCard(title: "Fuerza de agitación", symbol: "waveform.path.ecg") {
                ShakeGauge(metrics: snapshot.metrics)
                    .frame(height: 260)
                    .frame(maxWidth: .infinity)
                MetricGrid {
                    MetricTile(title: "Fuerza neta estimada", value: String(format: "%.3f N", snapshot.metrics.forceNewtons),
                               symbol: "arrow.up.and.down.and.arrow.left.and.right",
                               tint: LabPalette.intensityColor(snapshot.metrics.intensity),
                               footnote: "m·a con masa nominal 164 g del iPhone 12")
                    MetricTile(title: "Pico de la sesión", value: String(format: "%.2f m/s²", snapshot.metrics.sessionPeakMs2),
                               symbol: "chart.line.uptrend.xyaxis", tint: LabPalette.alert,
                               footnote: String(format: "%.2f g · %.3f N", snapshot.metrics.sessionPeakMs2 / Physics.standardGravity,
                                                snapshot.metrics.peakForceNewtons))
                }
            }
            ledCard
        }
    }

    // MARK: Scientific

    private var scientific: some View {
        VStack(spacing: 14) {
            SectionCard(title: "Aceleración lineal (sin gravedad)", symbol: "move.3d") {
                MetricGrid {
                    MetricTile(title: "|a| bruta", value: String(format: "%.3f m/s²", snapshot.metrics.rawMs2),
                               symbol: "waveform", footnote: String(format: "%.4f g · sin filtrar", snapshot.metrics.rawG))
                    MetricTile(title: "|a| filtrada (RMS 0,5 s)", value: String(format: "%.3f m/s²", snapshot.metrics.rmsMs2),
                               symbol: "waveform.path", tint: LabPalette.intensityColor(snapshot.metrics.intensity),
                               footnote: String(format: "%.4f g", snapshot.metrics.rmsG))
                    MetricTile(title: "Máx. / mín. (filtrada)",
                               value: String(format: "%.2f / %.2f", snapshot.metrics.sessionMaxMs2, snapshot.metrics.sessionMinMs2),
                               symbol: "arrow.up.arrow.down", footnote: "m/s² en esta sesión")
                    MetricTile(title: "Promedio (filtrada)", value: String(format: "%.3f m/s²", snapshot.metrics.sessionAverageMs2),
                               symbol: "sum", footnote: "\(snapshot.metrics.sampleCount) muestras")
                    MetricTile(title: "Pico bruto", value: String(format: "%.2f m/s²", snapshot.metrics.sessionPeakMs2),
                               symbol: "bolt", tint: LabPalette.alert,
                               footnote: String(format: "F = %.3f N", snapshot.metrics.peakForceNewtons))
                    MetricTile(title: "Frecuencia de muestreo", value: String(format: "%.0f Hz", snapshot.metrics.sampleRateHz),
                               symbol: "metronome", footnote: "Objetivo \(Int(MotionService.sensorRateHz)) Hz")
                }
            }
            axisChart
            SectionCard(title: "Vectores", symbol: "arrow.up.right") {
                vectorRow("Aceleración lineal", snapshot.linear, unit: "m/s²")
                vectorRow("Gravedad", snapshot.gravity, unit: "m/s²")
                vectorRow("Velocidad angular", snapshot.rotationRate, unit: "rad/s")
                KeyValueRow(key: "|g| medida", value: String(format: "%.3f m/s²", snapshot.gravity.magnitude))
            }
            SectionCard(title: "Orientación", symbol: "rotate.3d") {
                KeyValueRow(key: "Alabeo (roll)", value: degrees(snapshot.attitude.roll))
                KeyValueRow(key: "Cabeceo (pitch)", value: degrees(snapshot.attitude.pitch))
                KeyValueRow(key: "Guiñada (yaw)", value: degrees(snapshot.attitude.yaw))
                Text("Guiñada relativa a una referencia arbitraria (inicio de la sesión), sin brújula.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ledCard
        }
    }

    // MARK: Shared parts

    private var axisChart: some View {
        SectionCard(title: "Aceleración por eje · últimos 10 s", symbol: "chart.xyaxis.line") {
            Chart {
                ForEach(model.motion.axisHistory) { point in
                    LineMark(x: .value("Tiempo", point.date), y: .value("m/s²", point.x))
                        .foregroundStyle(by: .value("Eje", "X"))
                    LineMark(x: .value("Tiempo", point.date), y: .value("m/s²", point.y))
                        .foregroundStyle(by: .value("Eje", "Y"))
                    LineMark(x: .value("Tiempo", point.date), y: .value("m/s²", point.z))
                        .foregroundStyle(by: .value("Eje", "Z"))
                }
            }
            .chartForegroundStyleScale(["X": Color.red, "Y": Color.green, "Z": Color.blue])
            .chartXAxis(.hidden)
            .chartYAxisLabel("m/s²")
            .frame(height: 180)
            .accessibilityLabel("Gráfica de aceleración lineal por eje en los últimos 10 segundos")
        }
    }

    private var axisLegend: some View {
        HStack(spacing: 14) {
            Label("X", systemImage: "circle.fill").foregroundStyle(.red)
            Label("Y", systemImage: "circle.fill").foregroundStyle(.green)
            Label("Z", systemImage: "circle.fill").foregroundStyle(.blue)
            Label("Gravedad", systemImage: "arrow.down").foregroundStyle(.yellow)
        }
        .font(.caption2.weight(.semibold))
        .labelStyle(.titleAndIcon)
    }

    private var ledCard: some View {
        SectionCard(title: "LED RGB del ESP32", symbol: "lightbulb.led") {
            VirtualLed(rgb: model.realtimeStatus?.rgb)
            if let phone = model.realtimeStatus?.phoneMotion, phone.active == true {
                KeyValueRow(key: "El ESP32 recibe", value: String(format: "%.2f m/s² · intensidad %.0f %%",
                                                                   phone.linearAccelMs2 ?? 0, (phone.intensity ?? 0) * 100))
            }
        }
    }

    private var streamCard: some View {
        SectionCard(title: "Enlace con el ESP32", symbol: "dot.radiowaves.right") {
            @Bindable var settings = model.settings
            Toggle("Enviar movimiento al ESP32 (5 Hz)", isOn: $settings.motionStreaming)
            Label(model.motionStreamLabel,
                  systemImage: model.canStreamMotion && settings.motionStreaming ? "antenna.radiowaves.left.and.right" : "pause.circle")
                .font(.subheadline)
                .foregroundStyle(model.canStreamMotion && settings.motionStreaming ? .green : .secondary)
            KeyValueRow(key: "Muestras enviadas", value: "\(model.motionSamplesSent)")
            if let echo = model.motionEchoMs2 {
                KeyValueRow(key: "Eco recibido del ESP32", value: String(format: "%.2f m/s² · %@", echo,
                                                                         Format.relative(model.motionEchoAt)))
            }
            if let error = model.motionSendError {
                Label(error, systemImage: "exclamationmark.triangle").font(.footnote).foregroundStyle(.orange)
            }
        }
    }

    private var explanation: some View {
        Text("""
        Mediciones reales de CoreMotion a \(Int(MotionService.sensorRateHz)) Hz. La aceleración lineal excluye la gravedad. \
        La fuerza es la fuerza neta sobre el teléfono (F = m·a, 164 g), no la de la mano. No se calcula velocidad ni \
        posición: integrar el acelerómetro acumula deriva. Al ESP32 se envía, cada 200 ms, la muestra real con mayor \
        |a| de esa ventana.
        """)
        .font(.footnote)
        .foregroundStyle(.secondary)
    }

    private func vectorRow(_ title: String, _ v: Vec3, unit: String) -> some View {
        KeyValueRow(key: title, value: String(format: "x %+.2f  y %+.2f  z %+.2f %@", v.x, v.y, v.z, unit))
    }

    private func degrees(_ radians: Double) -> String {
        String(format: "%+.1f°", radians * 180 / .pi)
    }
}
