import Foundation
import SentinelKit

/// Read-only client for the HTTP v0.3.1 API and the realtime health route.
struct DeviceHTTPClient {
    enum HTTPError: Error, LocalizedError {
        case invalidHost
        case status(Int, String)
        case decoding(String)
        case rejected(Int, String)

        var errorDescription: String? {
            switch self {
            case .invalidHost: return "Dirección de dispositivo inválida."
            case .status(404, let path): return "\(path) no existe en este firmware (imagen sin núcleo realtime)."
            case .status(503, let path): return "\(path) no está listo (503)."
            case .status(let code, let path): return "\(path) respondió HTTP \(code)."
            case .decoding(let path): return "Respuesta no reconocida en \(path)."
            case .rejected(_, let message): return DeviceHTTPClient.explain(message)
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
    func lab() async throws -> LabSnapshot { try await get("/api/lab") }

    // MARK: Distance calibration (lab API, laboratory network only)

    private struct ExperimentRequest: Encodable { let name: String; let clientId: UInt64; let environment: Int }
    private struct ExperimentReply: Decodable { let id: Int }
    private struct MeasurementRequest: Encodable {
        let experimentId: Int
        let actualM: Double
        let calibration: Bool
        let notes: String
    }
    private struct CalibrationRequest: Encodable { let experimentId: Int }

    /// Creates a distance experiment bound to one RF client. Environment:
    /// 0 open space, 1 indoor, 2 with obstacles.
    func createExperiment(name: String, clientId: UInt64, environment: Int) async throws -> Int {
        let reply: ExperimentReply = try await post("/api/experiments",
                                                    ExperimentRequest(name: String(name.prefix(48)),
                                                                      clientId: clientId, environment: environment))
        return reply.id
    }

    /// Records the client's current RSSI on the ESP32 at a known distance.
    func recordCalibrationPoint(experimentId: Int, meters: Double, notes: String) async throws {
        let _: LabErrorBody = try await post("/api/measurements",
                                             MeasurementRequest(experimentId: experimentId, actualM: meters,
                                                                calibration: true, notes: String(notes.prefix(96))))
    }

    /// Fits A and n on the ESP32 from the recorded calibration points.
    func calibrate(experimentId: Int) async throws {
        let _: LabErrorBody = try await post("/api/calibration", CalibrationRequest(experimentId: experimentId))
    }

    static func explain(_ firmwareMessage: String) -> String {
        switch firmwareMessage {
        case "RSSI unavailable":
            return "El ESP32 no tiene una lectura RSSI reciente de este iPhone. Mantente conectado a SENTINEL-LAB y reintenta."
        case "client session expired; create a new experiment":
            return "El iPhone se reconectó y cambió su sesión RF. Empieza una calibración nueva."
        case "need valid calibration points at distinct distances":
            return "Hacen falta al menos dos puntos válidos a distancias distintas."
        case "experiment capacity reached; export results":
            return "El ESP32 ya guarda 8 experimentos (máximo en RAM). Exporta y reinicia el ESP32 para liberar espacio."
        case "RF observation unavailable":
            return "La observación RF del ESP32 no está disponible ahora mismo."
        case "name, clientId and environment required":
            return "El ESP32 no reconoce este cliente RF. Comprueba que el iPhone está conectado a SENTINEL-LAB."
        default:
            return "El ESP32 rechazó la operación: \(firmwareMessage)"
        }
    }

    private func post<Body: Encodable, Reply: Decodable>(_ path: String, _ body: Body) async throws -> Reply {
        var components = URLComponents()
        components.scheme = "http"
        components.host = host
        components.path = path
        guard let url = components.url else { throw HTTPError.invalidHost }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONEncoder().encode(body)
        let (data, response) = try await session.data(for: request)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard code == 200 else {
            if let error = try? JSONDecoder().decode(LabErrorBody.self, from: data), let message = error.error {
                throw HTTPError.rejected(code, message)
            }
            throw HTTPError.status(code, path)
        }
        do {
            return try JSONDecoder().decode(Reply.self, from: data)
        } catch {
            throw HTTPError.decoding(path)
        }
    }

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
