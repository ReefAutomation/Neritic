import Foundation
import Observation

/// Live connection to a controller's `/ws` endpoint.
///
/// Carries three things:
/// * state updates (text frames shaped like `GET /api/state`),
/// * the live LED preview (binary frames, `led.count` × 4 bytes of RGBW),
/// * `ota_status` frames, once subscribed with ``subscribeToOTA()``.
///
/// ### Why ``suspend()`` closes the socket
/// The firmware supports a `{"type":"client_pause"}` message, but it only
/// suppresses *text* broadcasts: `broadcastText` checks `_suspendedClients`
/// while `broadcastBinary` does not (src/webserver.cpp). The 400 ms live LED
/// stream therefore keeps arriving while paused. To actually stop it — for
/// example when the app is backgrounded — the socket must be closed.
///
/// ### Messages deliberately not implemented
/// `claim_live` / `release_live` and `ota_ack` exist in the firmware but are
/// inert: `_activeLiveFd` is never consulted when broadcasting, and
/// `otaAckReceived` is written but never read. This client does not send them.
@MainActor
@Observable
public final class LiveStripStream {

    public enum ConnectionState: Equatable, Sendable {
        case idle
        case connecting
        case connected
        case waiting(retryIn: TimeInterval, reason: String)
        case failed(String)
    }

    public private(set) var state: ConnectionState = .idle
    public private(set) var latestDeviceState: DeviceState?
    public private(set) var latestFrame: LEDFrame?
    public private(set) var otaStatus: OTAStatus?

    /// Called for every decoded binary LED frame, in arrival order.
    public var onFrame: ((LEDFrame) -> Void)?
    /// Called for every decoded state text frame.
    public var onState: ((DeviceState) -> Void)?

    private let baseURL: URL
    private let session: URLSession

    private var task: URLSessionWebSocketTask?
    private var receiveTask: Task<Void, Never>?
    private var heartbeatTask: Task<Void, Never>?
    private var reconnectTask: Task<Void, Never>?

    private var wantsConnection = false
    private var reconnectAttempt = 0
    private var lastPongAt: Date?

    private static let heartbeatInterval: TimeInterval = 4
    private static let heartbeatTimeout: TimeInterval = 12
    private static let maxBackoff: TimeInterval = 8

    public init(baseURL: URL, session: URLSession = .shared) {
        self.baseURL = baseURL
        self.session = session
    }

    public convenience init?(host: String, session: URLSession = .shared) {
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        let withScheme = trimmed.contains("://") ? trimmed : "http://\(trimmed)"
        guard let url = URL(string: withScheme) else { return nil }
        self.init(baseURL: url, session: session)
    }

    // MARK: - Lifecycle

    /// Connects, or reconnects, and keeps the connection alive.
    public func connect() {
        wantsConnection = true
        guard task == nil else { return }
        openSocket()
    }

    /// Closes the connection and stops reconnecting. Must be used instead of
    /// `client_pause` to stop the binary LED stream — see the type's notes.
    public func suspend() {
        wantsConnection = false
        reconnectAttempt = 0
        teardown()
        state = .idle
    }

    /// Sends `{"type":"state"}`, which both returns the current state and keeps
    /// this client subscribed to subsequent state broadcasts.
    public func subscribeToState() {
        send(#"{"type":"state"}"#)
    }

    /// Sends `{"type":"ota_client"}`. From then on the device routes
    /// `ota_status` frames to this client only.
    public func subscribeToOTA() {
        send(#"{"type":"ota_client"}"#)
    }

    /// Releases the OTA subscription so the live stream is not skipped and the
    /// server stops routing OTA frames here.
    public func unsubscribeFromOTA() {
        subscribeToState()
    }

    /// Clears the last OTA status shown in the UI.
    public func resetOTAStatus() {
        otaStatus = nil
    }

    // MARK: - Socket

    private func webSocketURL() -> URL? {
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)
        components?.scheme = baseURL.scheme == "https" ? "wss" : "ws"
        components?.path = "/ws"
        components?.query = nil
        return components?.url
    }

    private func openSocket() {
        guard let url = webSocketURL() else {
            state = .failed("Invalid host")
            return
        }
        state = .connecting

        let task = session.webSocketTask(with: url)
        task.maximumMessageSize = 1 << 20
        self.task = task
        task.resume()

        lastPongAt = Date()

        receiveTask = Task { @MainActor [weak self] in
            while let self, !Task.isCancelled {
                guard let task = self.task else { return }
                do {
                    let message = try await task.receive()
                    self.handle(message)
                } catch {
                    guard !Task.isCancelled else { return }
                    self.handleDisconnect(error)
                    return
                }
            }
        }

        // `{"type":"state"}` doubles as the handshake: the device replies with
        // the current state and keeps this socket subscribed.
        subscribeToState()
        // Still `.connecting` at this point: the label only becomes "Live" once a
        // message has actually arrived, so it cannot claim success for a socket
        // that never opens.
        startHeartbeat()
    }

    private func teardown() {
        receiveTask?.cancel()
        receiveTask = nil
        heartbeatTask?.cancel()
        heartbeatTask = nil
        reconnectTask?.cancel()
        reconnectTask = nil
        task?.cancel(with: .goingAway, reason: nil)
        task = nil
    }

    // MARK: - Heartbeat

    private func startHeartbeat() {
        heartbeatTask?.cancel()
        heartbeatTask = Task { @MainActor [weak self] in
            while let self, !Task.isCancelled {
                try? await Task.sleep(for: .seconds(Self.heartbeatInterval))
                guard !Task.isCancelled else { return }
                guard self.task != nil else { return }

                if let lastPong = self.lastPongAt,
                   Date().timeIntervalSince(lastPong) > Self.heartbeatTimeout {
                    self.handleDisconnect(nil)
                    return
                }
                self.send(#"{"type":"ping"}"#)
            }
        }
    }

    private func send(_ text: String) {
        task?.send(.string(text)) { _ in }
    }

    // MARK: - Receiving

    private struct Envelope: Decodable {
        var type: String?
    }

    private func handle(_ message: URLSessionWebSocketTask.Message) {
        if state != .connected {
            state = .connected
            reconnectAttempt = 0
        }
        switch message {
        case let .string(text):
            handleText(text)
        case let .data(data):
            let frame = LEDFrame.decode(data)
            latestFrame = frame
            onFrame?(frame)
        @unknown default:
            break
        }
    }

    private func handleText(_ text: String) {
        guard let data = text.data(using: .utf8) else { return }

        let type = (try? JSONDecoder().decode(Envelope.self, from: data))?.type

        switch type {
        case "pong":
            lastPongAt = Date()

        case "ota_status":
            if let status = try? JSONDecoder().decode(OTAStatus.self, from: data) {
                otaStatus = status
            }

        default:
            guard let deviceState = try? JSONDecoder().decode(DeviceState.self, from: data) else {
                return
            }
            latestDeviceState = deviceState
            onState?(deviceState)
        }
    }

    // MARK: - Reconnection

    private func handleDisconnect(_ error: Error?) {
        teardown()

        guard wantsConnection else {
            state = .idle
            return
        }

        if let error, Self.isFatal(error) {
            state = .failed((error as NSError).localizedDescription)
            wantsConnection = false
            return
        }

        let attempt = reconnectAttempt
        reconnectAttempt += 1
        let delay = min(Self.maxBackoff, 0.5 * pow(2, Double(min(attempt, 6))))
        let reason = error.map { ($0 as NSError).localizedDescription } ?? "Connection lost"
        state = .waiting(retryIn: delay, reason: reason)

        reconnectTask = Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(delay))
            guard let self, !Task.isCancelled, self.wantsConnection else { return }
            self.openSocket()
        }
    }

    /// A bad URL is not worth retrying; everything else is.
    private static func isFatal(_ error: Error) -> Bool {
        guard let urlError = error as? URLError else { return false }
        return urlError.code == .badURL || urlError.code == .unsupportedURL
    }
}
