import SceneKit
import SentinelKit
import simd
import SwiftUI

/// Visual identity shared with the app icon and the web: navy + signal cyan.
enum LabPalette {
    static let signal = Color(red: 0.25, green: 0.78, blue: 0.88)
    static let navy = Color(red: 0.04, green: 0.13, blue: 0.21)
    static let amber = Color(red: 1.0, green: 0.65, blue: 0.15)
    static let alert = Color(red: 0.95, green: 0.27, blue: 0.27)

    /// Same ramp as the firmware LED: teal → amber → red with intensity.
    static func intensityColor(_ intensity: Double) -> Color {
        let t = min(max(intensity, 0), 1)
        if t < 0.5 {
            let k = t / 0.5
            return Color(red: 0.25 + (1.0 - 0.25) * k, green: 0.78 + (0.65 - 0.78) * k, blue: 0.88 + (0.15 - 0.88) * k)
        }
        let k = (t - 0.5) / 0.5
        return Color(red: 1.0 + (0.95 - 1.0) * k, green: 0.65 + (0.27 - 0.65) * k, blue: 0.15 + (0.27 - 0.15) * k)
    }

    static func stateColor(_ state: MotionState) -> Color {
        switch state {
        case .stable: return signal
        case .movement: return amber
        case .alert: return alert
        case .sensorOffline: return .secondary
        }
    }

    static func stateLabel(_ state: MotionState) -> String {
        switch state {
        case .stable: return "Reposo"
        case .movement: return "Movimiento"
        case .alert: return "Agitación fuerte"
        case .sensorOffline: return "Sin sensor"
        }
    }
}

// MARK: - Virtual RGB LED

/// Mirrors the ESP32's physical WS2812 as reported by /api/realtime/status.
struct VirtualLed: View {
    let rgb: RgbStatus?
    var size: CGFloat = 54

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                Circle()
                    .fill(ledColor.opacity(isLit ? 0.35 : 0))
                    .frame(width: size * 1.6, height: size * 1.6)
                    .blur(radius: size * 0.35)
                Circle()
                    .fill(RadialGradient(colors: [ledColor.opacity(isLit ? 1 : 0.25), ledColor.opacity(isLit ? 0.55 : 0.1)],
                                         center: .center, startRadius: 1, endRadius: size / 2))
                    .frame(width: size, height: size)
                    .overlay(Circle().stroke(Color.white.opacity(0.35), lineWidth: 1))
                    .shadow(color: ledColor.opacity(isLit ? 0.8 : 0), radius: size * 0.25)
            }
            .frame(width: size * 1.6, height: size * 1.6)
            .animation(.easeInOut(duration: 0.4), value: rgb)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.subheadline.weight(.semibold))
                Text(detail).font(.caption).foregroundStyle(.secondary)
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("LED RGB del ESP32: \(title). \(detail)")
    }

    private var isLit: Bool {
        guard let c = rgb?.unitColor, rgb?.enabled != false else { return false }
        return c.red + c.green + c.blue > 0.01
    }

    private var ledColor: Color {
        guard let c = rgb?.unitColor else { return .gray }
        // The physical LED runs dim (eye safety); scale up for display.
        let peak = max(c.red, c.green, c.blue, 0.001)
        return Color(red: c.red / peak, green: c.green / peak, blue: c.blue / peak)
    }

    private var title: String {
        guard let rgb else { return "Firmware sin motor RGB" }
        if rgb.enabled == false { return "LED RGB desactivado" }
        return rgb.modeLabel
    }

    private var detail: String {
        guard let rgb else { return "El LED físico no se controla en esta versión (requiere firmware ≥ 0.5.0)." }
        let pin = rgb.pin.map { "GPIO\($0)" } ?? "GPIO ?"
        if let r = rgb.r, let g = rgb.g, let b = rgb.b { return "WS2812 · \(pin) · RGB \(r), \(g), \(b)" }
        return "WS2812 · \(pin)"
    }
}

// MARK: - Shake gauge

/// Large ring showing the filtered shake intensity, with the raw value and
/// the session peak marked separately.
struct ShakeGauge: View {
    let metrics: ShakeMetrics

    var body: some View {
        ZStack {
            Circle().stroke(Color.secondary.opacity(0.15), style: StrokeStyle(lineWidth: 18, lineCap: .round))
            Circle()
                .trim(from: 0, to: Physics.intensity(linearMs2: metrics.sessionPeakMs2))
                .stroke(LabPalette.alert.opacity(0.25), style: StrokeStyle(lineWidth: 18, lineCap: .round))
                .rotationEffect(.degrees(-90))
            Circle()
                .trim(from: 0, to: metrics.intensity)
                .stroke(AngularGradient(colors: [LabPalette.signal, LabPalette.amber, LabPalette.alert],
                                        center: .center, startAngle: .degrees(0), endAngle: .degrees(360)),
                        style: StrokeStyle(lineWidth: 18, lineCap: .round))
                .rotationEffect(.degrees(-90))
                .animation(.easeOut(duration: 0.12), value: metrics.intensity)
            VStack(spacing: 2) {
                Text(String(format: "%.2f", metrics.rmsMs2))
                    .font(.system(size: 52, weight: .bold, design: .rounded).monospacedDigit())
                    .contentTransition(.numericText())
                Text("m/s² · filtrado RMS 0,5 s").font(.caption).foregroundStyle(.secondary)
                Text(String(format: "%.3f g · %.3f N", metrics.rmsG, metrics.forceNewtons))
                    .font(.subheadline.monospacedDigit())
                    .foregroundStyle(LabPalette.intensityColor(metrics.intensity))
                Label(LabPalette.stateLabel(metrics.state), systemImage: "waveform.path")
                    .font(.caption.weight(.semibold))
                    .padding(.horizontal, 10).padding(.vertical, 4)
                    .background(LabPalette.stateColor(metrics.state).opacity(0.18), in: Capsule())
                    .foregroundStyle(LabPalette.stateColor(metrics.state))
            }
        }
        .padding(10)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Intensidad de agitación")
        .accessibilityValue(String(format: "%.2f metros por segundo al cuadrado, %@", metrics.rmsMs2,
                                   LabPalette.stateLabel(metrics.state)))
    }
}

// MARK: - 3D orientation

/// SceneKit model of the iPhone driven by CoreMotion's attitude quaternion,
/// with the device axes (X red, Y green, Z blue) and the measured gravity
/// vector drawn in the device frame.
struct Orientation3DView: UIViewRepresentable {
    let attitude: Attitude
    let gravity: Vec3
    let intensity: Double

    final class Coordinator {
        let scene = SCNScene()
        let phone = SCNNode()
        let gravityArrow = SCNNode()
        let glow = SCNMaterial()
    }

    func makeCoordinator() -> Coordinator {
        let c = Coordinator()
        let scene = c.scene

        // Phone body in device units (×10 scale of an iPhone 12: 71.5 × 146.7 × 7.4 mm).
        let body = SCNBox(width: 0.715, height: 1.467, length: 0.074, chamferRadius: 0.08)
        let frame = SCNMaterial()
        frame.diffuse.contents = UIColor(white: 0.18, alpha: 1)
        frame.metalness.contents = 0.8
        frame.roughness.contents = 0.25
        frame.lightingModel = .physicallyBased
        body.materials = [frame]
        c.phone.addChildNode(SCNNode(geometry: body))

        let screen = SCNPlane(width: 0.66, height: 1.41)
        screen.cornerRadius = 0.07
        c.glow.diffuse.contents = UIColor(red: 0.04, green: 0.13, blue: 0.21, alpha: 1)
        c.glow.emission.contents = UIColor(red: 0.25, green: 0.78, blue: 0.88, alpha: 1)
        c.glow.emission.intensity = 0.25
        screen.materials = [c.glow]
        let screenNode = SCNNode(geometry: screen)
        screenNode.position = SCNVector3(0, 0, 0.038)
        c.phone.addChildNode(screenNode)

        c.phone.addChildNode(Self.arrow(length: 0.75, color: .systemRed, rotation: SCNVector4(0, 0, 1, -Float.pi / 2)))
        c.phone.addChildNode(Self.arrow(length: 0.95, color: .systemGreen, rotation: SCNVector4(0, 0, 1, 0)))
        c.phone.addChildNode(Self.arrow(length: 0.55, color: .systemBlue, rotation: SCNVector4(1, 0, 0, Float.pi / 2)))
        c.gravityArrow.addChildNode(Self.arrow(length: 1.0, color: .systemYellow, rotation: SCNVector4(0, 0, 1, 0)))
        c.phone.addChildNode(c.gravityArrow)
        scene.rootNode.addChildNode(c.phone)

        let camera = SCNNode()
        camera.camera = SCNCamera()
        camera.camera?.fieldOfView = 40
        camera.position = SCNVector3(0, 0.6, 4.2)
        camera.look(at: SCNVector3(0, 0, 0))
        scene.rootNode.addChildNode(camera)

        let key = SCNNode()
        key.light = SCNLight()
        key.light?.type = .directional
        key.light?.intensity = 900
        key.eulerAngles = SCNVector3(-Float.pi / 4, Float.pi / 6, 0)
        scene.rootNode.addChildNode(key)
        let ambient = SCNNode()
        ambient.light = SCNLight()
        ambient.light?.type = .ambient
        ambient.light?.intensity = 350
        scene.rootNode.addChildNode(ambient)
        return c
    }

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView()
        view.scene = context.coordinator.scene
        view.backgroundColor = .clear
        view.antialiasingMode = .multisampling4X
        view.rendersContinuously = false
        view.isUserInteractionEnabled = false
        view.isAccessibilityElement = true
        view.accessibilityLabel = "Modelo 3D de la orientación del iPhone"
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        let c = context.coordinator
        let q = attitude.quaternion
        // CoreMotion world (Z up) → SceneKit world (Y up): rotate −90° about X.
        let toScene = simd_quatf(angle: -.pi / 2, axis: SIMD3<Float>(1, 0, 0))
        let device = simd_quatf(ix: Float(q.x), iy: Float(q.y), iz: Float(q.z), r: Float(q.w))
        c.phone.simdOrientation = toScene * device

        // Gravity is measured in the device frame; orient the yellow arrow
        // (modelled along +Y) to point along it, length ∝ |g|.
        let g = SIMD3<Float>(Float(gravity.x), Float(gravity.y), Float(gravity.z))
        let length = simd_length(g)
        if length > 0.1 {
            c.gravityArrow.isHidden = false
            c.gravityArrow.simdOrientation = simd_quatf(from: SIMD3<Float>(0, 1, 0), to: g / length)
            c.gravityArrow.simdScale = SIMD3<Float>(repeating: min(length / Float(Physics.standardGravity), 1.5))
        } else {
            c.gravityArrow.isHidden = true
        }
        c.glow.emission.intensity = 0.25 + CGFloat(min(max(intensity, 0), 1)) * 0.9
        view.setNeedsDisplay()
    }

    private static func arrow(length: CGFloat, color: UIColor, rotation: SCNVector4) -> SCNNode {
        let material = SCNMaterial()
        material.diffuse.contents = color
        material.emission.contents = color.withAlphaComponent(0.4)
        let shaft = SCNCylinder(radius: 0.012, height: length)
        shaft.materials = [material]
        let shaftNode = SCNNode(geometry: shaft)
        shaftNode.position = SCNVector3(0, Float(length) / 2, 0)
        let head = SCNCone(topRadius: 0, bottomRadius: 0.04, height: 0.1)
        head.materials = [material]
        let headNode = SCNNode(geometry: head)
        headNode.position = SCNVector3(0, Float(length) + 0.05, 0)
        let node = SCNNode()
        node.addChildNode(shaftNode)
        node.addChildNode(headNode)
        node.rotation = rotation
        return node
    }
}
