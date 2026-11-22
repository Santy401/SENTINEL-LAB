import Metal
import SentinelKit
import SwiftUI

/// Hero card: the ESP32-S3-DevKitC-1 in 3D, its live LED and a tap-to-inspect
/// sheet. Falls back to a 2D schematic when Metal (SceneKit) is unavailable.
struct BoardCard: View {
    @Environment(AppModel.self) private var model
    @State private var selected: String?
    @State private var sheetPart: BoardPartInfo?
    private let metalAvailable = MTLCreateSystemDefaultDevice() != nil

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label("ESP32-S3-DevKitC-1", systemImage: "cpu").font(.headline)
                Spacer()
                StatusPill(text: modeLabel, symbol: "lightbulb.led.fill", color: ledSwiftColor)
            }
            ZStack(alignment: .bottomLeading) {
                RoundedRectangle(cornerRadius: 14)
                    .fill(LinearGradient(colors: [Color(red: 0.07, green: 0.1, blue: 0.16), Color(red: 0.02, green: 0.03, blue: 0.06)],
                                         startPoint: .top, endPoint: .bottom))
                if metalAvailable {
                    BoardSceneView(led: ledTuple, shakeIntensity: model.motion.snapshot.metrics.intensity,
                                   selectedPart: selected) { part in
                        selected = part
                        sheetPart = ESP32S3DevKitC1.part(part)
                    }
                } else {
                    BoardSchematic(led: ledSwiftColor, selected: selected) { part in
                        selected = part
                        sheetPart = ESP32S3DevKitC1.part(part)
                    }
                    .padding()
                }
                Text(metalAvailable ? "Arrastra para girar · pellizca para acercar · toca una pieza"
                                    : "Vista 2D (este dispositivo no ofrece Metal) · toca una pieza")
                    .font(.caption2).foregroundStyle(.white.opacity(0.7))
                    .padding(10)
            }
            .frame(height: 280)
            .clipShape(RoundedRectangle(cornerRadius: 14))
            Text("Modelo original y aproximado (dimensiones públicas de Espressif), el mismo de la web. El LED virtual copia el color que reporta el firmware.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(Color(.systemBackground), in: RoundedRectangle(cornerRadius: 16))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(Color(.separator).opacity(0.4)))
        .sheet(item: $sheetPart, onDismiss: { selected = nil }) { part in
            BoardPartSheet(part: part)
                .presentationDetents([.medium])
        }
    }

    private var rgb: RgbStatus? { model.realtimeStatus?.rgb }

    private var ledTuple: (r: Int, g: Int, b: Int)? {
        guard let rgb, rgb.enabled != false, let r = rgb.r, let g = rgb.g, let b = rgb.b else { return nil }
        return (r, g, b)
    }

    private var ledSwiftColor: Color {
        guard let led = ledTuple, max(led.r, led.g, led.b) > 0 else { return .gray }
        let peak = Double(max(led.r, led.g, led.b))
        return Color(red: Double(led.r) / peak, green: Double(led.g) / peak, blue: Double(led.b) / peak)
    }

    private var modeLabel: String {
        guard let rgb else { return "LED sin datos" }
        if rgb.enabled == false { return "LED desactivado" }
        return rgb.modeLabel
    }
}

struct BoardPartSheet: View {
    let part: BoardPartInfo

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text(part.summary)
                }
                Section("Detalles") {
                    ForEach(part.details, id: \.self) { Label($0, systemImage: "info.circle") }
                }
                if part.id == "rgb" {
                    Section("Modos del LED") {
                        ForEach(["boot", "offline", "client", "synced", "motion", "event"], id: \.self) { mode in
                            if let legend = ESP32S3DevKitC1.rgbModeLegend[mode] {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(legend.label).font(.subheadline.weight(.semibold))
                                    Text(legend.meaning).font(.caption).foregroundStyle(.secondary)
                                }
                            }
                        }
                    }
                }
            }
            .navigationTitle(part.name)
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

/// Top view fallback, drawn from the same primitives (no Metal needed).
struct BoardSchematic: View {
    let led: Color
    let selected: String?
    let onSelect: (String?) -> Void

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / 66, geo.size.height / 28)
            let origin = CGPoint(x: geo.size.width / 2, y: geo.size.height / 2)
            ZStack {
                ForEach(Array(ESP32S3DevKitC1.primitives.enumerated()), id: \.offset) { _, p in
                    if case let .box(w, _, l) = p.shape, p.y >= -0.1 {
                        Rectangle()
                            .fill(color(for: p))
                            .frame(width: w * scale, height: l * scale)
                            .position(x: origin.x + p.x * scale, y: origin.y + p.z * scale)
                            .onTapGesture { onSelect(p.part) }
                    }
                }
                Circle().fill(led).frame(width: 3.4 * scale, height: 3.4 * scale)
                    .shadow(color: led, radius: 6)
                    .position(x: origin.x + ESP32S3DevKitC1.ledCenter.x * scale, y: origin.y + ESP32S3DevKitC1.ledCenter.z * scale)
                    .onTapGesture { onSelect("rgb") }
            }
        }
        .accessibilityLabel("Vista superior esquemática de la placa")
    }

    private func color(for p: BoardPrimitive) -> Color {
        let hex = p.material.hex
        let base = Color(red: Double((hex >> 16) & 0xff) / 255, green: Double((hex >> 8) & 0xff) / 255, blue: Double(hex & 0xff) / 255)
        return p.part != nil && p.part == selected ? .blue.opacity(0.8) : base
    }
}
