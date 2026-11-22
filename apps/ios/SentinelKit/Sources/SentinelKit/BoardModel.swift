import Foundation

// ESP32-S3-DevKitC-1 (N16R8) board model shared with the web dashboard.
//
// Source of truth: the web definition. Texts and dimensions come from
// apps/web/src/lib/board-parts.ts; geometry is a millimetre transcription of
// apps/web/src/three/board-model.ts (which uses 1 scene unit = 10 mm).
// apps/ios/scripts/check-board-model.py verifies that the part texts and the
// board dimensions here are identical to the web file. The model is ORIGINAL
// and APPROXIMATE (public DevKitC-1 guide and WROOM-1 datasheet dimensions),
// not a manufacturer CAD drawing.

public struct BoardPartInfo: Equatable, Sendable, Identifiable {
    public let id: String
    public let name: String
    public let summary: String
    public let details: [String]
}

public enum BoardMaterial: String, CaseIterable, Sendable {
    case pcb, pcbSide, shield, moduleBase, copper, gold, steel, plastic, chip, buttonCap, ledPackage, led, passive

    /// sRGB colour (0xRRGGBB), identical to the web materials.
    public var hex: UInt32 {
        switch self {
        case .pcb: return 0x14181d
        case .pcbSide: return 0x0c0f12
        case .shield: return 0xcfd3d8
        case .moduleBase: return 0x1d2430
        case .copper: return 0xd8a04a
        case .gold: return 0xe2b85c
        case .steel: return 0xb9bec5
        case .plastic: return 0x0b0d10
        case .chip: return 0x15181c
        case .buttonCap: return 0x23272d
        case .ledPackage: return 0xf2f2ee
        case .led: return 0x2a2f36
        case .passive: return 0x6b5a45
        }
    }

    public var metalness: Double {
        switch self {
        case .shield, .copper, .gold, .steel: return 1
        case .chip: return 0.15
        case .moduleBase: return 0.1
        case .pcb: return 0.08
        default: return 0
        }
    }

    public var roughness: Double {
        switch self {
        case .pcb: return 0.62
        case .pcbSide: return 0.8
        case .shield: return 0.28
        case .moduleBase: return 0.55
        case .copper: return 0.35
        case .gold: return 0.25
        case .steel: return 0.22
        case .plastic: return 0.7
        case .chip: return 0.45
        case .buttonCap: return 0.5
        case .ledPackage: return 0.4
        case .led: return 0.15
        case .passive: return 0.6
        }
    }
}

public enum BoardShape: Equatable, Sendable {
    case box(width: Double, height: Double, length: Double)  // x, y, z (mm)
    case cylinder(radius: Double, height: Double)
    case sphere(radius: Double)
}

/// One solid of the model. Coordinates in millimetres: x along the board
/// length (antenna at −x, USB-C at +x), y up (top copper at +0.8), z across.
public struct BoardPrimitive: Equatable, Sendable {
    public let shape: BoardShape
    public let x: Double
    public let y: Double
    public let z: Double
    public let material: BoardMaterial
    public let part: String?
}

public enum ESP32S3DevKitC1 {
    public static let lengthMm = 63.0
    public static let widthMm = 25.4
    public static let thicknessMm = 1.6

    public static let parts: [BoardPartInfo] = [
        BoardPartInfo(id: "module", name: "Módulo ESP32-S3-WROOM-1 (N16R8)",
                      summary: "SoC Xtensa LX7 de doble núcleo a 240 MHz bajo blindaje metálico.",
                      details: ["Flash 16 MB (quad) · PSRAM 8 MB (octal)", "Wi-Fi 802.11 b/g/n 2,4 GHz y Bluetooth LE 5", "Aquí corren el firmware SENTINEL y el servidor WebSocket"]),
        BoardPartInfo(id: "antenna", name: "Antena PCB",
                      summary: "Antena impresa en el extremo del módulo, sin plano de masa debajo.",
                      details: ["Emite el punto de acceso SENTINEL-LAB", "Su orientación y los obstáculos cambian el RSSI medido", "No cubrirla con la mano durante experimentos de cobertura"]),
        BoardPartInfo(id: "usb", name: "USB-C nativo (USB)",
                      summary: "USB OTG del propio ESP32-S3 (GPIO19/20).",
                      details: ["Consola serie CDC del firmware", "Alimentación 5 V"]),
        BoardPartInfo(id: "uart", name: "USB-C UART",
                      summary: "Puerto de programación a través del puente USB-serie.",
                      details: ["Usado para flashear (/dev/ttyACM0)", "Las líneas DTR/RTS pueden reiniciar la placa"]),
        BoardPartInfo(id: "rgb", name: "LED RGB WS2812 (GPIO48)",
                      summary: "LED direccionable de la placa; el firmware ≥ 0.5.0 lo usa como indicador de estado.",
                      details: ["El LED virtual de esta escena copia el color reportado por /api/realtime/status", "Brillo limitado y transiciones suaves, sin estroboscopio", "En algunas revisiones de placa el LED está en GPIO38"]),
        BoardPartInfo(id: "boot", name: "Botón BOOT (GPIO0)",
                      summary: "Mantenido al reiniciar, entra en modo descarga.",
                      details: ["No necesario con esptool y auto-reset"]),
        BoardPartInfo(id: "reset", name: "Botón RST (EN)",
                      summary: "Reinicia el ESP32-S3.",
                      details: ["Un reinicio por EN se reporta como POWERON en resetReason"]),
        BoardPartInfo(id: "ldo", name: "Regulador 3,3 V",
                      summary: "LDO que baja los 5 V del USB a 3,3 V.",
                      details: ["Limita la corriente disponible para periféricos externos"]),
        BoardPartInfo(id: "bridge", name: "Puente USB-serie",
                      summary: "Convierte USB en UART para programar (CP2102N en la placa oficial; CH343 en este ejemplar, 1a86:55d3).",
                      details: ["Representación genérica: el encapsulado real puede variar"]),
        BoardPartInfo(id: "headers", name: "Pines 2 × 22",
                      summary: "GPIO, alimentación y tierra con paso de 2,54 mm.",
                      details: ["Lógica a 3,3 V: no conectar 5 V a los GPIO", "Ningún pin está conectado en esta configuración del laboratorio"]),
    ]

    public static func part(_ id: String?) -> BoardPartInfo? {
        guard let id else { return nil }
        return parts.first { $0.id == id }
    }

    /// RGB mode legend (same texts as the web RGB panel).
    public static let rgbModeLegend: [String: (label: String, meaning: String)] = [
        "boot": ("Arranque", "El firmware acaba de iniciar: fundido cian."),
        "offline": ("En espera", "Sin clientes WebSocket: respiración azul tenue y lenta."),
        "client": ("Cliente conectado", "Hay clientes pero el reloj aún no está sincronizado: verde azulado fijo."),
        "synced": ("Sincronizado", "Reloj UTC válido y clientes activos: verde suave."),
        "motion": ("Movimiento", "El iPhone se mueve: el color pasa de turquesa a ámbar y rojo según la intensidad."),
        "event": ("Evento", "Evento RF (conexión, desconexión, señal débil): un pulso blanco suave."),
    ]

    /// Position of the WS2812 dome centre (mm), for lights and halos.
    public static let ledCenter = (x: -3.2, y: 2.3, z: -6.5)

    /// All solids of the board, transcribed from the web model.
    public static let primitives: [BoardPrimitive] = {
        let top = thicknessMm / 2  // 0.8
        let l2 = lengthMm / 2      // 31.5
        var out: [BoardPrimitive] = []
        func add(_ shape: BoardShape, _ x: Double, _ y: Double, _ z: Double, _ m: BoardMaterial, _ part: String?) {
            out.append(BoardPrimitive(shape: shape, x: x, y: y, z: z, material: m, part: part))
        }
        // PCB
        add(.box(width: lengthMm, height: thicknessMm, length: widthMm), 0, 0, 0, .pcb, nil)
        // WROOM-1 module: base (antenna end) + shield can + antenna traces.
        let modLen = 25.5, modWid = 18.0
        let modX = -l2 + modLen / 2 - 1.2
        add(.box(width: modLen, height: 0.8, length: modWid), modX, top + 0.4, 0, .moduleBase, "antenna")
        add(.box(width: 17.6, height: 2.3, length: 15.8), modX + (modLen - 17.6) / 2 - 0.6, top + 0.8 + 1.15, 0, .shield, "module")
        for i in 0..<6 {
            add(.box(width: 0.5, height: 0.06, length: 14), modX - modLen / 2 + 1.2 + Double(i) * 0.95, top + 0.83, 0, .copper, "antenna")
        }
        // WS2812 package + dome.
        let ledX = modX + modLen / 2 + 4
        add(.box(width: 5, height: 1.4, length: 5), ledX, top + 0.7, -6.5, .ledPackage, "rgb")
        add(.sphere(radius: 1.7), ledX, top + 1.5, -6.5, .led, "rgb")
        // USB-C receptacles.
        for (z, part) in [(-6.2, "usb"), (6.2, "uart")] {
            add(.box(width: 7.4, height: 3.2, length: 9), l2 - 3.2, top + 1.6, z, .steel, part)
            add(.box(width: 0.4, height: 2.2, length: 7.6), l2 + 0.45, top + 1.6, z, .plastic, part)
        }
        // BOOT / RST buttons.
        for (z, part) in [(-9.2, "boot"), (9.2, "reset")] {
            add(.box(width: 4.2, height: 1.6, length: 3.2), l2 - 12.5, top + 0.8, z, .steel, part)
            add(.cylinder(radius: 0.9, height: 0.8), l2 - 12.5, top + 2, z, .buttonCap, part)
        }
        // LDO (SOT-223 + tab) and USB-serial bridge.
        add(.box(width: 6.5, height: 1.6, length: 3.5), l2 - 20, top + 0.8, -4.5, .chip, "ldo")
        add(.box(width: 3, height: 0.3, length: 1.2), l2 - 20, top + 0.15, -6.9, .steel, "ldo")
        add(.box(width: 4, height: 0.9, length: 4), l2 - 20, top + 0.45, 4.5, .chip, "bridge")
        // Decorative passives.
        for (x, z) in [(-2.0, -7.0), (1, 7), (4, -2), (8, 5), (10, -8), (13, 1), (16, -6), (-4, 3)] {
            add(.box(width: 1.6, height: 0.5, length: 0.8), x + 6, top + 0.25, z, .passive, nil)
        }
        // 2 × 22 headers, pins pointing down.
        for z in [-11.43, 11.43] {
            add(.box(width: 55.9, height: 2.5, length: 2.5), 0.6, -top - 1.25, z, .plastic, "headers")
            for i in 0..<22 {
                add(.box(width: 0.64, height: 11, length: 0.64), -26.67 + Double(i) * 2.54 + 0.6, -top - 4, z, .gold, "headers")
            }
        }
        return out
    }()

    /// Perceived brightness 0..1 (Rec. 709 luma), as on the web.
    public static func ledLevel(r: Int, g: Int, b: Int) -> Double {
        (0.2126 * Double(r) + 0.7152 * Double(g) + 0.0722 * Double(b)) / 255
    }
}
