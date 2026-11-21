import SwiftUI

@main
struct SentinelLabApp: App {
    @State private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(model)
                .task { model.start() }
        }
    }
}

struct RootView: View {
    var body: some View {
        TabView {
            DashboardView()
                .tabItem { Label("Panel", systemImage: "gauge.with.needle") }
            TelemetryView()
                .tabItem { Label("Telemetría", systemImage: "waveform.path.ecg") }
            DiagnosticsView()
                .tabItem { Label("Diagnóstico", systemImage: "stethoscope") }
            SettingsView()
                .tabItem { Label("Ajustes", systemImage: "gearshape") }
        }
    }
}
