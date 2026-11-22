import SwiftUI
import SentinelKit

struct DiagnosticsView: View {
    @Environment(AppModel.self) private var model
    @State private var exportURL: URL?
    @State private var exportError: String?

    var body: some View {
        NavigationStack {
            List {
                if model.isDemo {
                    Section { DemoBanner() }.listRowBackground(Color.clear)
                }
                firmwareSection
                memorySection
                communicationSection
                protocolSection
                evidenceSection
            }
            .navigationTitle("Diagnóstico")
            .refreshable { await model.refresh() }
        }
    }

    private var status: RealtimeStatus? { model.realtimeStatus }

    private var firmwareSection: some View {
        Section("Firmware") {
            KeyValueRow(key: "Imagen", value: status?.imageVersion ?? "—")
            KeyValueRow(key: "API HTTP (fw)", value: status?.httpApiVersion ?? status?.fw ?? model.deviceState?.fw ?? "—")
            KeyValueRow(key: "Núcleo realtime", value: status?.realtimeVersion ?? "—")
            KeyValueRow(key: "bootId", value: status?.bootId ?? "—")
            KeyValueRow(key: "Uptime", value: Format.uptime(status?.uptimeMs))
            KeyValueRow(key: "Motivo de reinicio", value: status?.resetReason ?? "no informado")
            KeyValueRow(key: "Reloj", value: status?.clockSynced == true ? "Sincronizado (\(status?.clockSource ?? "?"))" : "Sin sincronizar")
            if let age = status?.clockSyncAgeMs {
                KeyValueRow(key: "Desde la sincronización", value: Format.uptime(age))
            }
        }
    }

    private var memorySection: some View {
        Section("Memoria") {
            KeyValueRow(key: "Heap libre", value: Format.bytes(status?.freeHeapBytes))
            KeyValueRow(key: "Heap mínimo", value: Format.bytes(status?.minFreeHeapBytes))
            KeyValueRow(key: "Bloque libre máximo", value: Format.bytes(status?.largestFreeBlockBytes))
            KeyValueRow(key: "Pila del loop (mín. libre)", value: Format.bytes(status?.minLoopStackFreeBytes))
            KeyValueRow(key: "Muestras de heap", value: Format.count(status?.heapSamples))
        }
    }

    private var communicationSection: some View {
        Section("Comunicación") {
            KeyValueRow(key: "Estado", value: model.phaseLabel)
            KeyValueRow(key: "Clientes WebSocket", value: status?.clientCount.map(String.init) ?? "—")
            KeyValueRow(key: "Tramas descartadas", value: Format.count(status?.droppedFrames))
            KeyValueRow(key: "Mensajes rechazados", value: Format.count(status?.rejectedMessages))
            KeyValueRow(key: "Telemetría descartada", value: Format.count(status?.droppedTelemetry))
            KeyValueRow(key: "Timeouts de handshake", value: Format.count(status?.handshakeTimeouts))
            KeyValueRow(key: "Timeouts por inactividad", value: Format.count(status?.idleTimeouts))
            KeyValueRow(key: "Fallos de conexión (app)", value: "\(model.machine.failures)")
            KeyValueRow(key: "Reconexiones (app)", value: "\(model.machine.reconnects)")
            KeyValueRow(key: "Última respuesta HTTP", value: Format.relative(model.lastHTTPSuccess))
            if let failure = model.machine.lastFailure {
                Text(failure).font(.footnote).foregroundStyle(.secondary)
            }
        }
    }

    private var protocolSection: some View {
        Section("Protocolo sentinel.v1") {
            KeyValueRow(key: "Sesión del firmware", value: model.firmwareSessionId ?? "—")
            KeyValueRow(key: "Token configurado", value: model.tokenConfigured ? "Sí (Keychain)" : "No")
            KeyValueRow(key: "Tramas inválidas", value: "\(model.decodeFailures)")
            if let decodeError = model.lastDecodeError {
                KeyValueRow(key: "Último rechazo", value: decodeError)
            }
            if let error = model.lastProtocolError {
                KeyValueRow(key: "Último error del ESP32", value: "\(error.code.rawValue): \(error.message)")
            }
            if model.framesByType.isEmpty {
                Text("Sin tramas recibidas.").foregroundStyle(.secondary)
            } else {
                ForEach(model.framesByType.sorted(by: { $0.key < $1.key }), id: \.key) { entry in
                    KeyValueRow(key: entry.key, value: "\(entry.value)")
                }
            }
        }
    }

    private var evidenceSection: some View {
        Section {
            if let exportURL {
                ShareLink(item: exportURL) {
                    Label("Compartir evidencia JSON", systemImage: "square.and.arrow.up")
                }
            }
            Button {
                do {
                    exportURL = try model.evidenceFile()
                    exportError = nil
                } catch {
                    exportError = "No se pudo generar la evidencia."
                }
            } label: {
                Label(exportURL == nil ? "Generar evidencia" : "Regenerar evidencia", systemImage: "doc.badge.gearshape")
            }
            if let exportError {
                Text(exportError).foregroundStyle(.red)
            }
        } header: {
            Text("Evidencia")
        } footer: {
            Text("Incluye versiones, contadores, salud y eventos recientes. Nunca incluye el token, el SSID ni contraseñas.\(model.isDemo ? " Marcada como DEMO." : "")")
        }
    }
}
