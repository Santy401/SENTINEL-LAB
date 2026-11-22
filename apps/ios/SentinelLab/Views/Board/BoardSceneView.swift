import SceneKit
import SentinelKit
import SwiftUI
import UIKit

/// Interactive 3D ESP32-S3-DevKitC-1 built from `ESP32S3DevKitC1.primitives`
/// (the same model as the web). Orbit/zoom with gestures, tap a part to
/// inspect it. The WS2812 dome glows with the colour the firmware reports.
struct BoardSceneView: UIViewRepresentable {
    /// LED colour from `/api/realtime/status.rgb` (0...255); nil = unknown/off.
    var led: (r: Int, g: Int, b: Int)?
    /// Phone shake intensity 0...1 (firmware scale) for a subtle wobble.
    var shakeIntensity: Double
    var selectedPart: String?
    var onSelect: (String?) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(onSelect: onSelect) }

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView(frame: .zero)
        view.backgroundColor = .clear
        view.antialiasingMode = .multisampling4X
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = false
        view.rendersContinuously = false
        view.preferredFramesPerSecond = 30
        view.isAccessibilityElement = true
        view.accessibilityLabel = "Modelo 3D interactivo de la placa ESP32-S3-DevKitC-1"
        view.accessibilityHint = "Arrastra para girar, pellizca para acercar y toca una pieza para ver su ficha."
        let built = BoardSceneBuilder.build()
        view.scene = built.scene
        view.pointOfView = built.camera
        context.coordinator.board = built
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tapped(_:)))
        view.addGestureRecognizer(tap)
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        guard let board = context.coordinator.board else { return }
        context.coordinator.onSelect = onSelect
        board.setLed(led)
        board.highlight(selectedPart)
        board.wobble(intensity: UIAccessibility.isReduceMotionEnabled ? 0 : shakeIntensity)
    }

    final class Coordinator: NSObject {
        var onSelect: (String?) -> Void
        var board: BoardSceneBuilder.Board?

        init(onSelect: @escaping (String?) -> Void) {
            self.onSelect = onSelect
        }

        @objc func tapped(_ gesture: UITapGestureRecognizer) {
            guard let view = gesture.view as? SCNView else { return }
            let point = gesture.location(in: view)
            let hits = view.hitTest(point, options: [.searchMode: SCNHitTestSearchMode.closest.rawValue])
            let part = hits.lazy.compactMap { $0.node.name.flatMap { $0.hasPrefix("part:") ? String($0.dropFirst(5)) : nil } }.first
            UISelectionFeedbackGenerator().selectionChanged()
            onSelect(part)
        }
    }
}

enum BoardSceneBuilder {
    /// Scene scale: 1 SceneKit unit = 10 mm (as on the web).
    static let scale: Float = 0.1

    final class Board {
        let scene: SCNScene
        let camera: SCNNode
        let root: SCNNode
        let ledMaterial: SCNMaterial
        let ledHalo: SCNNode
        let ledLight: SCNLight
        private var partMaterials: [String: [SCNMaterial]] = [:]
        private var highlighted: String?
        private var lastLed: (Int, Int, Int)?
        private var lastWobble: Double = -1

        init(scene: SCNScene, camera: SCNNode, root: SCNNode, ledMaterial: SCNMaterial, ledHalo: SCNNode,
             ledLight: SCNLight, partMaterials: [String: [SCNMaterial]]) {
            self.scene = scene
            self.camera = camera
            self.root = root
            self.ledMaterial = ledMaterial
            self.ledHalo = ledHalo
            self.ledLight = ledLight
            self.partMaterials = partMaterials
        }

        func setLed(_ led: (r: Int, g: Int, b: Int)?) {
            let value = led.map { ($0.r, $0.g, $0.b) } ?? (0, 0, 0)
            if let lastLed, lastLed == value { return }
            lastLed = value
            // The firmware caps channels at 64/255; scale up for visibility
            // while keeping the hue exact.
            let peak = max(value.0, value.1, value.2)
            let gain = peak > 0 ? 255.0 / Double(max(peak, 64)) : 0
            let color = UIColor(red: CGFloat(min(255, Double(value.0) * gain)) / 255,
                                green: CGFloat(min(255, Double(value.1) * gain)) / 255,
                                blue: CGFloat(min(255, Double(value.2) * gain)) / 255, alpha: 1)
            let level = ESP32S3DevKitC1.ledLevel(r: value.0, g: value.1, b: value.2)
            let lit = peak > 0
            SCNTransaction.begin()
            SCNTransaction.animationDuration = 0.25
            ledMaterial.emission.contents = lit ? color : UIColor.black
            ledMaterial.diffuse.contents = lit ? color : UIColor(hex: BoardMaterial.led.hex)
            ledHalo.opacity = lit ? CGFloat(0.35 + 0.55 * min(1, level * 4)) : 0
            ledHalo.geometry?.firstMaterial?.diffuse.contents = color
            ledLight.color = color
            ledLight.intensity = lit ? CGFloat(120 + 900 * min(1, level * 4)) : 0
            SCNTransaction.commit()
        }

        func highlight(_ part: String?) {
            guard part != highlighted else { return }
            if let previous = highlighted {
                for m in partMaterials[previous] ?? [] { m.emission.contents = UIColor.black }
            }
            highlighted = part
            if let part {
                let tint = UIColor(red: 0.12, green: 0.44, blue: 0.92, alpha: 1).withAlphaComponent(0.55)
                for m in partMaterials[part] ?? [] where m !== ledMaterial { m.emission.contents = tint }
            }
        }

        /// Subtle tilt that follows the shake (≤ 4°), eased; zero when calm.
        func wobble(intensity: Double) {
            let quantized = (intensity * 20).rounded() / 20
            guard quantized != lastWobble else { return }
            lastWobble = quantized
            root.removeAction(forKey: "wobble")
            guard quantized > 0.04 else {
                root.runAction(SCNAction.rotateTo(x: 0, y: 0, z: 0, duration: 0.4, usesShortestUnitArc: true), forKey: "wobble")
                return
            }
            let angle = CGFloat(quantized) * .pi / 45  // up to 4°
            let period = 0.5 - 0.25 * quantized
            let swing = SCNAction.sequence([
                SCNAction.rotateTo(x: angle * 0.4, y: 0, z: angle, duration: period, usesShortestUnitArc: true),
                SCNAction.rotateTo(x: -angle * 0.4, y: 0, z: -angle, duration: period, usesShortestUnitArc: true),
            ])
            swing.timingMode = .easeInEaseOut
            root.runAction(SCNAction.repeatForever(swing), forKey: "wobble")
        }
    }

    static func build() -> Board {
        let scene = SCNScene()
        scene.lightingEnvironment.contents = environmentImage()
        scene.lightingEnvironment.intensity = 1.1
        let root = SCNNode()
        root.name = "esp32-s3-devkitc-1"
        scene.rootNode.addChildNode(root)

        var materials: [BoardMaterial: SCNMaterial] = [:]
        func material(_ kind: BoardMaterial) -> SCNMaterial {
            if let existing = materials[kind] { return existing }
            let m = SCNMaterial()
            m.lightingModel = .physicallyBased
            m.diffuse.contents = UIColor(hex: kind.hex)
            m.metalness.contents = kind.metalness
            m.roughness.contents = kind.roughness
            materials[kind] = m
            return m
        }

        var partMaterials: [String: [SCNMaterial]] = [:]
        var ledMaterial = material(.led)
        for primitive in ESP32S3DevKitC1.primitives {
            let geometry: SCNGeometry
            switch primitive.shape {
            case let .box(width, height, length):
                geometry = SCNBox(width: CGFloat(width) * CGFloat(scale), height: CGFloat(height) * CGFloat(scale),
                                  length: CGFloat(length) * CGFloat(scale), chamferRadius: 0)
            case let .cylinder(radius, height):
                geometry = SCNCylinder(radius: CGFloat(radius) * CGFloat(scale), height: CGFloat(height) * CGFloat(scale))
            case let .sphere(radius):
                geometry = SCNSphere(radius: CGFloat(radius) * CGFloat(scale))
            }
            var mat: SCNMaterial
            if primitive.material == .pcb {
                // Silkscreen on the top face, plain dark sides.
                let top = material(.pcb).copy() as! SCNMaterial
                top.diffuse.contents = silkscreen() ?? UIColor(hex: BoardMaterial.pcb.hex)
                let side = material(.pcbSide)
                geometry.materials = [side, side, side, side, top, side]  // SCNBox: front,right,back,left,top,bottom
                mat = top
            } else if primitive.part != nil {
                // Per-part copies so highlighting one part never tints another.
                mat = material(primitive.material).copy() as! SCNMaterial
                geometry.firstMaterial = mat
            } else {
                mat = material(primitive.material)
                geometry.firstMaterial = mat
            }
            if primitive.material == .led {
                mat.lightingModel = .physicallyBased
                mat.transparency = 0.92
                ledMaterial = mat
            }
            let node = SCNNode(geometry: geometry)
            node.position = SCNVector3(Float(primitive.x) * scale, Float(primitive.y) * scale, Float(primitive.z) * scale)
            if let part = primitive.part {
                node.name = "part:\(part)"
                partMaterials[part, default: []].append(mat)
            }
            root.addChildNode(node)
        }

        // LED glow: additive halo sphere + coloured point light.
        let c = ESP32S3DevKitC1.ledCenter
        let halo = SCNSphere(radius: CGFloat(4.2 * Double(scale)))
        let haloMat = SCNMaterial()
        haloMat.lightingModel = .constant
        haloMat.blendMode = .add
        haloMat.writesToDepthBuffer = false
        haloMat.transparency = 0.35
        halo.firstMaterial = haloMat
        let haloNode = SCNNode(geometry: halo)
        haloNode.position = SCNVector3(Float(c.x) * scale, Float(c.y) * scale, Float(c.z) * scale)
        haloNode.opacity = 0
        haloNode.renderingOrder = 10
        root.addChildNode(haloNode)
        let ledLight = SCNLight()
        ledLight.type = .omni
        ledLight.intensity = 0
        ledLight.attenuationStartDistance = 0
        ledLight.attenuationEndDistance = 3.5
        let lightNode = SCNNode()
        lightNode.light = ledLight
        lightNode.position = SCNVector3(Float(c.x) * scale, Float(c.y + 1.5) * scale, Float(c.z) * scale)
        root.addChildNode(lightNode)

        // Studio lighting: soft key with shadows, fill and ambient.
        let key = SCNLight()
        key.type = .directional
        key.intensity = 900
        key.castsShadow = true
        key.shadowMode = .deferred
        key.shadowRadius = 6
        key.shadowSampleCount = 8
        key.shadowColor = UIColor.black.withAlphaComponent(0.35)
        let keyNode = SCNNode()
        keyNode.light = key
        keyNode.eulerAngles = SCNVector3(-Float.pi / 3, Float.pi / 6, 0)
        scene.rootNode.addChildNode(keyNode)
        let ambient = SCNLight()
        ambient.type = .ambient
        ambient.intensity = 220
        let ambientNode = SCNNode()
        ambientNode.light = ambient
        scene.rootNode.addChildNode(ambientNode)

        // Shadow catcher below the headers.
        let floor = SCNFloor()
        floor.reflectivity = 0
        let floorMat = SCNMaterial()
        floorMat.lightingModel = .shadowOnly
        floor.firstMaterial = floorMat
        let floorNode = SCNNode(geometry: floor)
        floorNode.position = SCNVector3(0, -1.05, 0)
        scene.rootNode.addChildNode(floorNode)

        let camera = SCNCamera()
        camera.fieldOfView = 34
        camera.zNear = 0.05
        camera.zFar = 100
        camera.wantsHDR = true
        camera.bloomIntensity = 0.9
        camera.bloomThreshold = 0.75
        camera.bloomBlurRadius = 6
        let cameraNode = SCNNode()
        cameraNode.camera = camera
        cameraNode.position = SCNVector3(4.2, 7.6, 9.4)
        cameraNode.look(at: SCNVector3(0, 0, 0))
        scene.rootNode.addChildNode(cameraNode)

        return Board(scene: scene, camera: cameraNode, root: root, ledMaterial: ledMaterial, ledHalo: haloNode,
                     ledLight: ledLight, partMaterials: partMaterials)
    }

    /// Two-tone vertical gradient used as image-based lighting.
    private static func environmentImage() -> UIImage {
        let size = CGSize(width: 64, height: 32)
        return UIGraphicsImageRenderer(size: size).image { ctx in
            let colors = [UIColor(white: 0.95, alpha: 1).cgColor, UIColor(white: 0.25, alpha: 1).cgColor] as CFArray
            if let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 1]) {
                ctx.cgContext.drawLinearGradient(gradient, start: .zero, end: CGPoint(x: 0, y: size.height), options: [])
            }
        }
    }

    /// Silkscreen texture for the PCB top face (same layout as the web).
    private static func silkscreen() -> UIImage? {
        let size = CGSize(width: 1024, height: 412)
        let renderer = UIGraphicsImageRenderer(size: size)
        return renderer.image { ctx in
            let cg = ctx.cgContext
            UIColor(hex: BoardMaterial.pcb.hex).setFill()
            cg.fill(CGRect(origin: .zero, size: size))
            cg.setStrokeColor(UIColor(white: 1, alpha: 0.025).cgColor)
            for x in stride(from: -412.0, to: 1024.0, by: 14) {
                cg.move(to: CGPoint(x: x, y: 0))
                cg.addLine(to: CGPoint(x: x + 412, y: 412))
            }
            cg.strokePath()
            let ink = UIColor(red: 0.92, green: 0.93, blue: 0.95, alpha: 0.85)
            func draw(_ text: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat, _ weight: UIFont.Weight) {
                let font = UIFont.monospacedSystemFont(ofSize: size, weight: weight)
                (text as NSString).draw(at: CGPoint(x: x, y: y - size), withAttributes: [.font: font, .foregroundColor: ink])
            }
            draw("ESP32-S3-DevKitC-1", 540, 180, 30, .semibold)
            draw("N16R8 · SENTINEL-LAB", 540, 220, 22, .medium)
            draw("RGB@48", 330, 300, 22, .medium)
            draw("USB", 940, 120, 18, .medium)
            draw("UART", 935, 300, 18, .medium)
            draw("BOOT", 860, 60, 18, .medium)
            draw("RST", 860, 372, 18, .medium)
            cg.setStrokeColor(UIColor(white: 0.92, alpha: 0.35).cgColor)
            cg.setLineWidth(2)
            for i in 0..<22 {
                let x = 70 + CGFloat(i) * 40.5
                cg.stroke(CGRect(x: x - 13, y: 10, width: 26, height: 26))
                cg.stroke(CGRect(x: x - 13, y: 376, width: 26, height: 26))
            }
        }
    }
}

extension UIColor {
    convenience init(hex: UInt32) {
        self.init(red: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
                  blue: CGFloat(hex & 0xff) / 255, alpha: 1)
    }
}
