import SwiftUI
import SentinelKit

struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @State private var hostDraft = ""
    @State private var tokenDraft = ""
    @State private var message: String?
    @State private var confirmRemove = false

    var body: some View {
        @Bindable var settings = model.settings
        NavigationStack {
            Form {
                Section {
                    TextField("192.168.4.1", text: $hostDraft)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .accessibilityLabel("Dirección del ESP32")
                    Button("Aplicar dirección") {
                        let trimmed = hostDraft.trimmingCharacters(in: .whitespaces)
                        guard AppSettings.isValidHost(trimmed) else {
                            message = "Dirección inválida: usa una IP o nombre sin http:// ni puerto."
                            return
                        }
                        settings.host = trimmed
                        model.hostChanged()
                        message = "Dirección actualizada."
                    }
                    .disabled(hostDraft.trimmingCharacters(in: .whitespaces) == model.settings.host)
                    Toggle("Conectar al abrir", isOn: $settings.autoConnect)
                } header: {
                    Text("Dispositivo")
                } footer: {
                    Text("HTTP en el puerto 80 y WebSocket en ws://<dirección>:81/realtime. Solo red local de laboratorio, sin TLS.")
                }

                Section {
                    SecureField(model.tokenConfigured ? "Token guardado (oculto)" : "Token bearer", text: $tokenDraft)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .textContentType(.password)
                    Button("Guardar token en Keychain") {
                        do {
                            try model.saveToken(tokenDraft)
                            tokenDraft = ""
                            message = model.tokenConfigured ? "Token guardado." : "Token eliminado."
                        } catch {
                            message = error.localizedDescription
                        }
                    }
                    .disabled(tokenDraft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    if model.tokenConfigured {
                        Button("Eliminar token", role: .destructive) { confirmRemove = true }
                    }
                } header: {
                    Text("Autenticación")
                } footer: {
                    Text("Con el token de SENTINEL_REALTIME_BEARER_TOKEN esta app actúa como fuente de hora: su saludo autenticado sincroniza el reloj UTC del ESP32. El token se guarda solo en el Keychain de este iPhone y nunca se exporta.")
                }

                Section("Visualización") {
                    Toggle("Usar RSSI filtrado en la gráfica", isOn: $settings.showFilteredRssi)
                    Stepper("Ventana de gráfica: \(settings.chartWindowMinutes) min",
                            value: $settings.chartWindowMinutes, in: 1...60)
                    Stepper("Sondeo HTTP cada \(settings.pollIntervalSeconds) s",
                            value: $settings.pollIntervalSeconds, in: 1...30)
                }

                Section {
                    Toggle("Modo demostración", isOn: Binding(get: { model.isDemo }, set: { model.setDemo($0) }))
                } header: {
                    Text("Demostración")
                } footer: {
                    Text("Genera datos sintéticos locales para explorar la interfaz sin hardware. Todas las pantallas muestran la etiqueta DEMO y la evidencia exportada queda marcada.")
                }

                Section("Acerca de") {
                    KeyValueRow(key: "Versión", value: appVersion)
                    KeyValueRow(key: "Protocolo", value: "sentinel.v1 · \(protocolVersion)")
                    KeyValueRow(key: "deviceId", value: model.settings.deviceId)
                }
            }
            .navigationTitle("Ajustes")
            .onAppear { hostDraft = model.settings.host }
            .alert("¿Eliminar el token?", isPresented: $confirmRemove) {
                Button("Eliminar", role: .destructive) {
                    do {
                        try model.removeToken()
                        message = "Token eliminado."
                    } catch {
                        message = error.localizedDescription
                    }
                }
                Button("Cancelar", role: .cancel) {}
            } message: {
                Text("Sin token, el ESP32 solo transmitirá si otro gateway sincroniza su reloj.")
            }
            .safeAreaInset(edge: .bottom) {
                if let message {
                    Text(message)
                        .font(.footnote)
                        .padding(10)
                        .frame(maxWidth: .infinity)
                        .background(.thinMaterial)
                        .onTapGesture { self.message = nil }
                        .accessibilityAddTraits(.isStaticText)
                }
            }
        }
    }

    private var appVersion: String {
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "?"
        let build = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "?"
        return "\(version) (\(build))"
    }
}
