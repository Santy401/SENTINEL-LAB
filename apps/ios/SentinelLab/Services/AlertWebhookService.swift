import CryptoKit
import Foundation
import SentinelKit

/// Sends alerts raised on the iPhone straight to the n8n webhook, signed with
/// the shared HMAC secret (Keychain). When the phone is on the ESP32 Wi-Fi
/// (no internet) iOS routes this request over cellular data. Failures are
/// queued and retried with backoff; permanent rejections are dropped.
@MainActor
final class AlertWebhookService {
    struct Pending: Codable {
        let id: String
        let body: Data
        var attempts: Int
        var nextAttempt: Date
    }

    private(set) var lastResult: String?
    private(set) var delivered = 0
    private var queue: [Pending] = []
    private var sending = false
    private let session: URLSession
    private static let maxQueue = 50
    private static let userAgent = "SentinelLabiOS/1.2"

    init() {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.allowsCellularAccess = true
        configuration.allowsExpensiveNetworkAccess = true
        configuration.allowsConstrainedNetworkAccess = true
        configuration.timeoutIntervalForRequest = 15
        configuration.waitsForConnectivity = false
        session = URLSession(configuration: configuration)
    }

    var pendingCount: Int { queue.count }

    func enqueue(_ alert: LabAlert, bootId: String?, firmwareVersion: String?, metrics: [String: Double]) {
        let body = AlertWebhookPayload.body(for: alert, bootId: bootId, firmwareVersion: firmwareVersion,
                                            metrics: metrics)
        queue.append(Pending(id: alert.id, body: body, attempts: 0, nextAttempt: .distantPast))
        if queue.count > Self.maxQueue { queue.removeFirst(queue.count - Self.maxQueue) }
    }

    /// Called periodically; sends whatever is due.
    func flush(url: URL, secret: String?) async {
        guard !sending, let secret, !secret.isEmpty, !queue.isEmpty else { return }
        sending = true
        defer { sending = false }
        let now = Date()
        for index in queue.indices.reversed() where queue[index].nextAttempt <= now {
            let item = queue[index]
            let outcome = await send(item.body, url: url, secret: secret)
            switch outcome {
            case .delivered:
                delivered += 1
                lastResult = "Enviada a n8n \(Date().formatted(date: .omitted, time: .standard))"
                queue.remove(at: index)
            case .permanent(let status):
                lastResult = "Rechazada por n8n (HTTP \(status)); revisa el secreto"
                queue.remove(at: index)
            case .retry(let reason):
                lastResult = "Sin conexión a n8n (\(reason)); reintentando"
                queue[index].attempts += 1
                queue[index].nextAttempt = Date().addingTimeInterval(
                    AlertWebhookPayload.backoff(attempt: queue[index].attempts))
            }
        }
    }

    /// Sends one labeled test alert immediately.
    func sendTest(url: URL, secret: String?) async -> String {
        guard let secret, !secret.isEmpty else { return "Falta el secreto de alertas" }
        let alert = LabAlert(id: "test-\(UUID().uuidString.prefix(8).lowercased())", kind: .clockUnsynced,
                             date: Date(), message: "Prueba enviada desde el iPhone. No es una medición.")
        var body = AlertWebhookPayload.body(for: alert, bootId: nil, firmwareVersion: nil)
        if var json = try? JSONSerialization.jsonObject(with: body) as? [String: Any] {
            json["rule"] = "test"
            json["severity"] = "info"
            json["title"] = "Prueba desde el iPhone"
            body = (try? JSONSerialization.data(withJSONObject: json, options: [.sortedKeys])) ?? body
        }
        switch await send(body, url: url, secret: secret) {
        case .delivered: lastResult = "Prueba enviada a n8n"
        case .permanent(let status): lastResult = "Rechazada por n8n (HTTP \(status)); revisa el secreto"
        case .retry(let reason): lastResult = "No se pudo contactar con n8n (\(reason))"
        }
        return lastResult ?? ""
    }

    private enum Outcome { case delivered, permanent(Int), retry(String) }

    private func send(_ body: Data, url: URL, secret: String) async -> Outcome {
        let timestamp = Int(Date().timeIntervalSince1970)
        let key = SymmetricKey(data: Data(secret.utf8))
        let mac = HMAC<SHA256>.authenticationCode(
            for: AlertWebhookPayload.signedMessage(timestamp: timestamp, body: body), using: key)
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.httpBody = body
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(Self.userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue(String(timestamp), forHTTPHeaderField: "X-Sentinel-Timestamp")
        request.setValue(mac.map { String(format: "%02x", $0) }.joined(), forHTTPHeaderField: "X-Sentinel-Signature")
        do {
            let (_, response) = try await session.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            if (200..<300).contains(status) { return .delivered }
            if AlertWebhookPayload.isPermanentFailure(status) { return .permanent(status) }
            return .retry("HTTP \(status)")
        } catch {
            return .retry((error as? URLError)?.code == .notConnectedToInternet ? "sin internet" : "error de red")
        }
    }
}
