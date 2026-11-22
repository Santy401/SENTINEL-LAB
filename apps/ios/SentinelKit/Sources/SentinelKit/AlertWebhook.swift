import Foundation

/// JSON body for the n8n alert webhook, identical in shape to the gateway's
/// (tools/gateway/delivery.py): id, rule, kind, severity, title, message,
/// occurredAt, device{bootId, firmwareVersion}, metrics{}. The signature is
/// computed by the app over these exact bytes (HMAC-SHA256 of
/// "<unix seconds>.<body>"), so key order does not matter to the receiver.
public enum AlertWebhookPayload {
    public static let defaultURL = URL(string: "https://n8n.nexuscloud.sbs/webhook/sentinel-alerts")!

    public static func body(for alert: LabAlert, bootId: String?, firmwareVersion: String?,
                            metrics: [String: Double] = [:], source: String = "ios") -> Data {
        let iso = ISO8601DateFormatter()
        iso.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        var device: [String: Any] = ["source": source]
        device["bootId"] = bootId ?? NSNull()
        device["firmwareVersion"] = firmwareVersion ?? NSNull()
        let object: [String: Any] = [
            "id": "ios-\(alert.id)",
            "rule": alert.kind.rawValue,
            "kind": "alert",
            "severity": alert.kind.severity.rawValue,
            "title": alert.kind.title,
            "message": alert.message,
            "occurredAt": iso.string(from: alert.date),
            "device": device,
            "metrics": metrics.filter { $0.value.isFinite },
        ]
        return (try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])) ?? Data()
    }

    /// The string the signature covers.
    public static func signedMessage(timestamp: Int, body: Data) -> Data {
        var message = Data("\(timestamp).".utf8)
        message.append(body)
        return message
    }

    /// HTTP statuses that will never succeed on retry.
    public static func isPermanentFailure(_ status: Int) -> Bool {
        [400, 401, 403, 404, 410, 413, 422].contains(status)
    }

    /// Exponential backoff in seconds, capped at 10 minutes.
    public static func backoff(attempt: Int) -> TimeInterval {
        min(600, 5 * pow(2, Double(max(0, attempt))))
    }
}
