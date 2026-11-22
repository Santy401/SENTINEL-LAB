import Foundation
import SentinelKit

/// Thin URLSessionWebSocketTask wrapper for `ws://<host>:81/realtime`.
///
/// Each `open` creates a new generation; callbacks from an older socket are
/// dropped so a late close never tears down a fresh connection. All events
/// are delivered on the main actor.
final class RealtimeTransport: NSObject, URLSessionWebSocketDelegate, @unchecked Sendable {
    enum Event {
        case opened
        case text(Data)
        case failed(String)
    }

    private let lock = NSLock()
    private var session: URLSession?
    private var task: URLSessionWebSocketTask?
    private var generation = 0
    private let handler: @MainActor (Event) -> Void

    init(handler: @escaping @MainActor (Event) -> Void) {
        self.handler = handler
        super.init()
    }

    static func url(host: String) -> URL? {
        var components = URLComponents()
        components.scheme = "ws"
        components.host = host
        components.port = 81
        components.path = "/realtime"
        return components.url
    }

    func open(host: String, token: String?) {
        close()
        guard let url = Self.url(host: host) else {
            emit(.failed("Dirección de dispositivo inválida."), generation: currentGeneration())
            return
        }
        var request = URLRequest(url: url, timeoutInterval: 10)
        if let token, !token.isEmpty {
            // URLSession treats Authorization as a reserved header and may
            // drop it on WebSocket upgrades. The firmware (>= 0.4.2) also
            // accepts the token offered as a second subprotocol; it only ever
            // selects sentinel.v1 and never echoes the token.
            request.setValue("\(websocketSubprotocol), sentinel.auth.\(token)",
                             forHTTPHeaderField: "Sec-WebSocket-Protocol")
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        } else {
            request.setValue(websocketSubprotocol, forHTTPHeaderField: "Sec-WebSocket-Protocol")
        }

        let configuration = URLSessionConfiguration.ephemeral
        configuration.waitsForConnectivity = false
        configuration.timeoutIntervalForRequest = 10
        let session = URLSession(configuration: configuration, delegate: self, delegateQueue: nil)
        let task = session.webSocketTask(with: request)
        task.maximumMessageSize = protocolMaxEnvelopeBytes

        lock.lock()
        generation += 1
        let current = generation
        self.session = session
        self.task = task
        lock.unlock()

        task.resume()
        receive(on: task, generation: current)
    }

    func close() {
        lock.lock()
        generation += 1
        let oldTask = task
        let oldSession = session
        task = nil
        session = nil
        lock.unlock()
        oldTask?.cancel(with: .normalClosure, reason: nil)
        oldSession?.invalidateAndCancel()
    }

    func send(_ data: Data) async throws {
        guard let task = currentTask() else { throw URLError(.notConnectedToInternet) }
        try await task.send(.string(String(decoding: data, as: UTF8.self)))
    }

    func ping() async throws {
        guard let task = currentTask() else { throw URLError(.notConnectedToInternet) }
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            task.sendPing { error in
                if let error { continuation.resume(throwing: error) } else { continuation.resume() }
            }
        }
    }

    // MARK: - Private

    private func currentTask() -> URLSessionWebSocketTask? {
        lock.lock(); defer { lock.unlock() }
        return task
    }

    private func currentGeneration() -> Int {
        lock.lock(); defer { lock.unlock() }
        return generation
    }

    private func isCurrent(_ task: URLSessionTask) -> Int? {
        lock.lock(); defer { lock.unlock() }
        return self.task === task ? generation : nil
    }

    private func emit(_ event: Event, generation: Int) {
        Task { @MainActor [weak self] in
            guard let self, self.currentGeneration() == generation else { return }
            self.handler(event)
        }
    }

    private func receive(on task: URLSessionWebSocketTask, generation: Int) {
        task.receive { [weak self] result in
            guard let self, self.currentGeneration() == generation else { return }
            switch result {
            case .success(.string(let text)):
                self.emit(.text(Data(text.utf8)), generation: generation)
                self.receive(on: task, generation: generation)
            case .success(.data):
                // Protocol v1 uses text frames only; binary frames are a contract violation.
                self.emit(.failed("El dispositivo envió una trama binaria no soportada."), generation: generation)
            case .success:
                self.receive(on: task, generation: generation)
            case .failure(let error):
                self.emit(.failed(Self.describe(error)), generation: generation)
            }
        }
    }

    static func describe(_ error: Error) -> String {
        let nsError = error as NSError
        if nsError.domain == NSURLErrorDomain {
            switch nsError.code {
            case NSURLErrorTimedOut: return "Tiempo de espera agotado al contactar el ESP32."
            case NSURLErrorCannotConnectToHost, NSURLErrorCannotFindHost:
                return "No se puede conectar con el ESP32. ¿Está el iPhone en la red SENTINEL-LAB?"
            case NSURLErrorNotConnectedToInternet, NSURLErrorNetworkConnectionLost:
                return "Se perdió la conexión de red."
            case NSURLErrorBadServerResponse:
                return "El ESP32 rechazó el handshake WebSocket (subprotocolo, versión o límite de clientes)."
            default: break
            }
        }
        return "Error de conexión (\(nsError.domain) \(nsError.code))."
    }

    // MARK: - URLSessionWebSocketDelegate

    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask,
                    didOpenWithProtocol negotiated: String?) {
        guard let generation = isCurrent(webSocketTask) else { return }
        if negotiated == websocketSubprotocol {
            emit(.opened, generation: generation)
        } else {
            emit(.failed("El servidor no negoció el subprotocolo sentinel.v1."), generation: generation)
        }
    }

    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask,
                    didCloseWith closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?) {
        guard let generation = isCurrent(webSocketTask) else { return }
        emit(.failed("El ESP32 cerró la sesión WebSocket (código \(closeCode.rawValue))."), generation: generation)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        guard let error, let generation = isCurrent(task) else { return }
        emit(.failed(Self.describe(error)), generation: generation)
    }
}
