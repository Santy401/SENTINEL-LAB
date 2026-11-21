import Foundation
import SentinelKit

/// Read-only client for the HTTP v0.3.1 API and the realtime health route.
struct DeviceHTTPClient {
    enum HTTPError: Error, LocalizedError {
        case invalidHost
        case status(Int, String)
        case decoding(String)

        var errorDescription: String? {
            switch self {
            case .invalidHost: return "Dirección de dispositivo inválida."
            case .status(404, let path): return "\(path) no existe en este firmware (imagen sin núcleo realtime)."
            case .status(503, let path): return "\(path) no está listo (503)."
            case .status(let code, let path): return "\(path) respondió HTTP \(code)."
            case .decoding(let path): return "Respuesta no reconocida en \(path)."
            }
        }
    }

    let host: String
    private let session: URLSession

    init(host: String) {
        self.host = host
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 4
        configuration.timeoutIntervalForResource = 6
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.waitsForConnectivity = false
        session = URLSession(configuration: configuration)
    }

    func realtimeStatus() async throws -> RealtimeStatus { try await get("/api/realtime/status") }
    func deviceState() async throws -> DeviceState { try await get("/api/state") }
    func rf() async throws -> RfSnapshot { try await get("/api/rf") }
    func events() async throws -> EventLog { try await get("/api/events") }

    private func get<T: Decodable>(_ path: String) async throws -> T {
        var components = URLComponents()
        components.scheme = "http"
        components.host = host
        components.path = path
        guard let url = components.url else { throw HTTPError.invalidHost }
        let (data, response) = try await session.data(from: url)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard code == 200 else { throw HTTPError.status(code, path) }
        do {
            return try JSONDecoder().decode(T.self, from: data)
        } catch {
            throw HTTPError.decoding(path)
        }
    }
}
