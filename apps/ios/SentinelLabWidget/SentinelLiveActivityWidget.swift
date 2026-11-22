import ActivityKit
import SentinelKit
import SwiftUI
import WidgetKit

@main
struct SentinelWidgetBundle: WidgetBundle {
    var body: some Widget {
        SentinelLiveActivityWidget()
    }
}

/// Lock Screen / Dynamic Island presentation of the ESP32 status. Data comes
/// only from the app (SentinelActivityState); the widget never fakes values.
struct SentinelLiveActivityWidget: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: SentinelActivityAttributes.self) { context in
            LockScreenView(state: context.state, host: context.attributes.deviceHost)
                .activityBackgroundTint(Color(red: 0.04, green: 0.06, blue: 0.1))
                .activitySystemActionForegroundColor(.white)
        } dynamicIsland: { context in
            let state = context.state
            return DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    LedDot(state: state, size: 22)
                }
                DynamicIslandExpandedRegion(.trailing) {
                    Text(String(format: "%.1f m/s²", state.shakeMs2))
                        .font(.headline.monospacedDigit())
                }
                DynamicIslandExpandedRegion(.bottom) {
                    VStack(alignment: .leading, spacing: 6) {
                        IntensityBar(value: state.shakeIntensity)
                        Text(state.lastAlert.map { "Última alerta: \($0)" } ?? statusLine(state))
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
            } compactLeading: {
                LedDot(state: state, size: 12)
            } compactTrailing: {
                Text("\(Int((state.shakeIntensity * 100).rounded()))%")
                    .font(.caption2.monospacedDigit())
            } minimal: {
                LedDot(state: state, size: 12)
            }
        }
    }
}

private func statusLine(_ state: SentinelActivityState) -> String {
    let link = state.connected ? "Conectado" : "Sin conexión"
    let clock = state.clockSynced ? "reloj sincronizado" : "reloj sin sincronizar"
    return "\(link) · \(clock)"
}

private struct LockScreenView: View {
    let state: SentinelActivityState
    let host: String

    var body: some View {
        HStack(spacing: 14) {
            LedDot(state: state, size: 34)
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("SENTINEL-LAB").font(.headline).foregroundStyle(.white)
                    Spacer()
                    Text(state.updatedAt, style: .time).font(.caption2.monospacedDigit()).foregroundStyle(.white.opacity(0.6))
                }
                Text(statusLine(state)).font(.caption).foregroundStyle(.white.opacity(0.75))
                HStack {
                    IntensityBar(value: state.shakeIntensity)
                    Text(String(format: "%.1f m/s²", state.shakeMs2))
                        .font(.caption.monospacedDigit()).foregroundStyle(.white)
                }
                if let alert = state.lastAlert {
                    Label(alert, systemImage: "exclamationmark.triangle.fill")
                        .font(.caption.weight(.semibold)).foregroundStyle(.orange)
                }
            }
        }
        .padding(14)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("SENTINEL-LAB en \(host). \(statusLine(state)). Agitación \(Int((state.shakeIntensity * 100).rounded())) por ciento.")
    }
}

private struct LedDot: View {
    let state: SentinelActivityState
    let size: CGFloat

    var body: some View {
        let peak = max(state.ledRed, state.ledGreen, state.ledBlue)
        let color = peak > 0
            ? Color(red: Double(state.ledRed) / Double(peak), green: Double(state.ledGreen) / Double(peak),
                    blue: Double(state.ledBlue) / Double(peak))
            : Color.gray
        Circle()
            .fill(color)
            .frame(width: size, height: size)
            .shadow(color: color.opacity(peak > 0 ? 0.9 : 0), radius: size / 3)
            .accessibilityLabel("LED RGB del ESP32")
    }
}

private struct IntensityBar: View {
    let value: Double

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Capsule().fill(Color.white.opacity(0.15))
                Capsule()
                    .fill(LinearGradient(colors: [.teal, .orange, .red], startPoint: .leading, endPoint: .trailing))
                    .frame(width: max(4, geo.size.width * value))
            }
        }
        .frame(height: 8)
    }
}
