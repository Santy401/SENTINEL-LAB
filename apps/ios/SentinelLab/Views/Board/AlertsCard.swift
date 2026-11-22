import SentinelKit
import SwiftUI

/// Recent local alerts (newest first), raised by AlertEngine from real data.
struct AlertsCard: View {
    @Environment(AppModel.self) private var model
    @State private var expanded = false

    var body: some View {
        SectionCard(title: "Alertas", symbol: "bell.badge") {
            ForEach(Array(model.alerts.prefix(expanded ? 20 : 3))) { alert in
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: symbol(alert.kind.severity))
                        .foregroundStyle(color(alert.kind.severity))
                        .frame(width: 22)
                    VStack(alignment: .leading, spacing: 2) {
                        HStack {
                            Text(alert.kind.title).font(.subheadline.weight(.semibold))
                            Spacer()
                            Text(alert.date, style: .time).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                        }
                        Text(alert.message).font(.caption).foregroundStyle(.secondary)
                    }
                }
                .accessibilityElement(children: .combine)
            }
            if model.alerts.count > 3 {
                Button(expanded ? "Mostrar menos" : "Ver las \(min(model.alerts.count, 20)) más recientes") {
                    expanded.toggle()
                }
                .font(.caption)
            }
        }
    }

    private func symbol(_ severity: AlertSeverity) -> String {
        switch severity {
        case .critical: return "exclamationmark.octagon.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .info: return "info.circle.fill"
        }
    }

    private func color(_ severity: AlertSeverity) -> Color {
        switch severity {
        case .critical: return .red
        case .warning: return .orange
        case .info: return .blue
        }
    }
}
