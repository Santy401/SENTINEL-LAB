import SwiftUI
import SentinelKit

enum Format {
    static func bytes(_ value: UInt64?) -> String {
        guard let value else { return "—" }
        return ByteCountFormatter.string(fromByteCount: Int64(value), countStyle: .memory)
    }

    static func uptime(_ milliseconds: UInt64?) -> String {
        guard let milliseconds else { return "—" }
        let seconds = Int(milliseconds / 1_000)
        let days = seconds / 86_400
        let hours = (seconds % 86_400) / 3_600
        let minutes = (seconds % 3_600) / 60
        let rest = seconds % 60
        if days > 0 { return "\(days) d \(hours) h \(minutes) min" }
        if hours > 0 { return "\(hours) h \(minutes) min" }
        if minutes > 0 { return "\(minutes) min \(rest) s" }
        return "\(rest) s"
    }

    static func count(_ value: UInt64?) -> String { value.map(String.init) ?? "—" }

    static func dbm(_ value: Int?) -> String { value.map { "\($0) dBm" } ?? "—" }

    static func relative(_ date: Date?) -> String {
        guard let date else { return "nunca" }
        let formatter = RelativeDateTimeFormatter()
        formatter.locale = Locale(identifier: "es")
        formatter.unitsStyle = .short
        return formatter.localizedString(for: date, relativeTo: Date())
    }

    static func time(_ date: Date?) -> String {
        guard let date else { return "—" }
        return date.formatted(date: .omitted, time: .standard)
    }
}

extension HealthLevel {
    var color: Color {
        switch self {
        case .ok: return .green
        case .info: return .blue
        case .warning: return .orange
        case .critical: return .red
        }
    }

    var label: String {
        switch self {
        case .ok: return "Correcto"
        case .info: return "Información"
        case .warning: return "Atención"
        case .critical: return "Crítico"
        }
    }

    var symbol: String {
        switch self {
        case .ok: return "checkmark.seal.fill"
        case .info: return "info.circle.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .critical: return "xmark.octagon.fill"
        }
    }
}

extension ConnectionPhase {
    var color: Color {
        switch self {
        case .streaming: return .green
        case .awaitingData, .connecting: return .orange
        case .waitingToRetry, .offline: return .red
        case .idle: return .secondary
        }
    }

    var symbol: String {
        switch self {
        case .streaming: return "dot.radiowaves.left.and.right"
        case .awaitingData: return "hourglass"
        case .connecting: return "arrow.triangle.2.circlepath"
        case .waitingToRetry: return "arrow.clockwise.circle"
        case .offline: return "wifi.slash"
        case .idle: return "pause.circle"
        }
    }
}

/// Capsule label used for connection and health status.
struct StatusPill: View {
    let text: String
    let symbol: String
    let color: Color

    var body: some View {
        Label(text, systemImage: symbol)
            .font(.subheadline.weight(.semibold))
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .foregroundStyle(color)
            .background(color.opacity(0.14), in: Capsule())
            .accessibilityElement(children: .combine)
    }
}

/// Explicit marker shown on every screen while demo mode is active.
struct DemoBanner: View {
    var body: some View {
        Label("MODO DEMO · datos sintéticos, no son mediciones", systemImage: "theatermasks.fill")
            .font(.footnote.weight(.bold))
            .frame(maxWidth: .infinity)
            .padding(8)
            .foregroundStyle(.white)
            .background(Color.purple, in: RoundedRectangle(cornerRadius: 10))
            .accessibilityLabel("Modo demostración activo. Los datos mostrados son sintéticos.")
    }
}

struct MetricTile: View {
    let title: String
    let value: String
    var symbol: String
    var tint: Color = .accentColor
    var footnote: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(title, systemImage: symbol)
                .font(.caption.weight(.medium))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title3.weight(.semibold).monospacedDigit())
                .foregroundStyle(tint)
                .lineLimit(1)
                .minimumScaleFactor(0.6)
            if let footnote {
                Text(footnote)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 14))
        .accessibilityElement(children: .combine)
    }
}

struct SectionCard<Content: View>: View {
    let title: String
    var symbol: String?
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let symbol {
                Label(title, systemImage: symbol).font(.headline)
            } else {
                Text(title).font(.headline)
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(Color(.systemBackground), in: RoundedRectangle(cornerRadius: 16))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(Color(.separator).opacity(0.4)))
    }
}

struct KeyValueRow: View {
    let key: String
    let value: String

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(key).foregroundStyle(.secondary)
            Spacer(minLength: 12)
            Text(value)
                .multilineTextAlignment(.trailing)
                .font(.body.monospacedDigit())
                .textSelection(.enabled)
        }
        .font(.subheadline)
        .accessibilityElement(children: .combine)
    }
}

struct HealthFindingRow: View {
    let finding: HealthFinding

    var body: some View {
        Label {
            Text(finding.message).font(.subheadline)
        } icon: {
            Image(systemName: finding.level.symbol).foregroundStyle(finding.level.color)
        }
        .accessibilityLabel("\(finding.level.label): \(finding.message)")
    }
}

/// Adaptive grid that collapses to one column at large Dynamic Type sizes.
struct MetricGrid<Content: View>: View {
    @Environment(\.dynamicTypeSize) private var typeSize
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        let columns = typeSize.isAccessibilitySize ? 1 : 2
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: columns), spacing: 10) {
            content
        }
    }
}
