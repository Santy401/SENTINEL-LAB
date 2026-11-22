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

                Section {
                    Toggle("Vibración según la agitación", isOn: $settings.hapticsEnabled)
                    Toggle("Actividad en vivo (pantalla bloqueada)", isOn: Binding(
                        get: { model.settings.liveActivityEnabled },
                        set: { model.setLiveActivity($0) }))
                    if let error = model.liveActivityError {
                        Text(error).font(.caption).foregroundStyle(.orange)
                    } else if model.liveActivityRunning {
                        Text("Actividad en vivo activa.").font(.caption).foregroundStyle(.secondary)
                    }
                    Toggle("Notificar alertas en segundo plano", isOn: $settings.alertNotifications)
                        .onChange(of: settings.alertNotifications) { _, enabled in
                            if enabled { Task { await model.requestNotificationPermission() } }
                        }
                    if !model.alerts.isEmpty {
                        Button("Borrar alertas (\(model.alerts.count))", role: .destructive) { model.clearAlerts() }
                    }
                } header: {
                    Text("Laboratorio")
                } footer: {
                    Text("Alertas locales: agitación ≥ 4 m/s², iPhone fuera de alcance (RSSI < −85 dBm), reinicio del ESP32, heap < 40 KB y reloj sin sincronizar más de 2 min. Cada tipo se limita a una alerta por minuto.")
                }
                AlertWebhookSection(model: model, settings: settings)
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


/// Sends the iPhone's own alerts to the n8n webhook (Telegram) over cellular
/// data when the ESP32 Wi-Fi has no internet. The HMAC secret stays in the Keychain.
private struct AlertWebhookSection: View {
    @Bindable var model: AppModel
    @Bindable var settings: AppSettings
    @State private var secretDraft = ""

    var body: some View {
        Section {
            Toggle("Enviar alertas a Telegram (n8n)", isOn: $settings.alertWebhookEnabled)
            TextField("URL del webhook", text: $settings.alertWebhookURL)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
                .font(.footnote.monospaced())
            SecureField(model.alertSecretConfigured ? "Secreto guardado · pega otro para cambiarlo"
                                                    : "Secreto de alertas (HMAC)", text: $secretDraft)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
            HStack {
                Button("Guardar secreto") {
                    model.saveAlertSecret(secretDraft)
                    secretDraft = ""
                }
                .disabled(secretDraft.trimmingCharacters(in: .whitespacesAndNewlines).count < 16)
                Spacer()
                if model.alertSecretConfigured {
                    Button("Eliminar", role: .destructive) { model.clearAlertSecret() }
                }
            }
            Button("Enviar alerta de prueba") { Task { await model.sendTestAlert() } }
                .disabled(!model.alertSecretConfigured)
            if let status = model.webhookStatus {
                Text(status).font(.caption).foregroundStyle(.secondary)
            }
        } header: {
            Text("Alertas remotas")
        } footer: {
            Text("El iPhone firma cada alerta (HMAC-SHA256) y la envía a n8n, que la reenvía a Telegram. Si la Wi-Fi SENTINEL-LAB no tiene internet, iOS usa los datos móviles. Si no hay conexión, las alertas esperan en cola y se reintentan.")
        }
    }
}
