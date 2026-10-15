import Foundation

/// Thin typed wrapper over the Neritic REST API.
///
/// The device has no authentication and enables CORS for any origin, so this is
/// simply a JSON-over-HTTP client bound to one host. Percentages (0-100) are the
/// only units used here — the device owns the conversion to its internal range.
public struct NeriticClient: Sendable {
    public let baseURL: URL
    private let session: URLSession
    private let decoder = JSONDecoder()
    private let encoder = JSONEncoder()

    public init(baseURL: URL, session: URLSession = .shared) {
        self.baseURL = baseURL
        self.session = session
    }

    /// Convenience for a host or host:port typed by the user.
    public init?(host: String, session: URLSession = .shared) {
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        let withScheme = trimmed.contains("://") ? trimmed : "http://\(trimmed)"
        guard let url = URL(string: withScheme) else { return nil }
        self.init(baseURL: url, session: session)
    }

    // MARK: - Version

    public func version() async throws -> String {
        let response: VersionResponse = try await get("/api/version")
        return response.version
    }

    // MARK: - State

    public func state() async throws -> DeviceState {
        try await get("/api/state")
    }

    /// Sends a partial state update.
    ///
    /// - Important: pass power and brightness together when changing both; the
    ///   firmware applies power first to avoid a brief relight.
    @discardableResult
    public func setState(_ update: StateUpdate) async throws -> SuccessResponse {
        try await post("/api/state", body: update)
    }

    // MARK: - Effects and presets

    public func effects() async throws -> [Effect] {
        let response: EffectsResponse = try await get("/api/effects")
        return response.effects
    }

    public func presets() async throws -> [Preset] {
        let response: PresetsResponse = try await get("/api/presets")
        return response.presets
    }

    /// Applies a preset immediately.
    @discardableResult
    public func applyPreset(id: Int) async throws -> SuccessResponse {
        try await post("/api/preset", body: PresetWrite.apply(id: id))
    }

    /// Saves a preset definition. See `PresetParams.speed` for the firmware's
    /// read/write asymmetry.
    @discardableResult
    public func savePreset(_ preset: PresetWrite) async throws -> SuccessResponse {
        try await post("/api/preset", body: preset)
    }

    // MARK: - Configuration

    public func config() async throws -> DeviceConfig {
        try await get("/api/config")
    }

    @discardableResult
    public func updateConfig(_ update: ConfigUpdate) async throws -> SuccessResponse {
        try await post("/api/config", body: update)
    }

    /// Updates a single timer. `POST /api/timer` writes exactly one timer per
    /// call; use `updateConfig` with a whole `timers` array to replace them all.
    @discardableResult
    public func updateTimer(_ timer: TimerConfig) async throws -> SuccessResponse {
        try await post("/api/timer", body: timer)
    }

    public func timezones() async throws -> [TimezoneOption] {
        try await get("/api/timezones")
    }

    // MARK: - Updates

    /// Current version plus the releases advertised by GitHub.
    ///
    /// The device answers 502 with `{"error": ...}` when it cannot reach GitHub;
    /// that is decoded rather than thrown so the caller can show it in context.
    public func updateInfo() async throws -> UpdateInfo {
        try await get("/api/update", acceptedStatuses: [200, 502])
    }

    /// Starts the on-device remote update. The device downloads and flashes the
    /// image itself; progress arrives over the WebSocket `ota_status` frames.
    @discardableResult
    public func startUpdate() async throws -> SuccessResponse {
        try await post("/api/update", body: EmptyBody.shared)
    }

    /// Reboots the device.
    @discardableResult
    public func reboot() async throws -> SuccessResponse {
        try await post("/api/command", body: CommandRequest(command: "reboot"))
    }

    /// Erases configuration and reboots. Destructive.
    @discardableResult
    public func factoryReset() async throws -> SuccessResponse {
        try await post("/api/factory_reset", body: EmptyBody.shared)
    }

    // MARK: - Wi-Fi provisioning

    /// SSIDs visible to the device. The device switches its radio into AP+STA
    /// mode and performs a blocking scan, so this can take a couple of seconds.
    public func scanWiFiNetworks() async throws -> [String] {
        let data = try await request(path: "/wifi/scan", method: "GET")
        do {
            return try WiFiScanResponse.decode(from: data)
        } catch {
            throw NeriticError.decoding(underlying: "wifi scan: \(error)")
        }
    }

    /// Hands new credentials to the device. It persists them and reboots, so the
    /// connection will drop — expect `NeriticError.unreachable` and reconnect.
    public func setWiFiCredentials(_ credentials: WiFiCredentials) async throws {
        var components = URLComponents()
        components.queryItems = [
            URLQueryItem(name: "ssid", value: credentials.ssid),
            URLQueryItem(name: "password", value: credentials.password),
        ]
        let body = Data((components.percentEncodedQuery ?? "").utf8)
        var request = URLRequest(url: url(for: "/wifi"))
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = body
        _ = try await perform(request, acceptedStatuses: [200])
    }

    // MARK: - Transport

    private struct EmptyBody: Encodable, Sendable {
        static let shared = EmptyBody()
        func encode(to encoder: Encoder) throws {
            var container = encoder.singleValueContainer()
            try container.encode([String: String]())
        }
    }

    private func url(for path: String) -> URL {
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)
        components?.path = path
        return components?.url ?? baseURL.appendingPathComponent(path)
    }

    private func get<T: Decodable>(
        _ path: String,
        acceptedStatuses: Set<Int> = [200]
    ) async throws -> T {
        let data = try await request(path: path, method: "GET", acceptedStatuses: acceptedStatuses)
        return try decode(data)
    }

    private func post<T: Decodable, Body: Encodable>(
        _ path: String,
        body: Body,
        acceptedStatuses: Set<Int> = [200]
    ) async throws -> T {
        var request = URLRequest(url: url(for: path))
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        do {
            request.httpBody = try encoder.encode(body)
        } catch {
            throw NeriticError.decoding(underlying: "request body: \(error)")
        }
        let data = try await perform(request, acceptedStatuses: acceptedStatuses)
        return try decode(data)
    }

    private func request(
        path: String,
        method: String,
        acceptedStatuses: Set<Int> = [200]
    ) async throws -> Data {
        var request = URLRequest(url: url(for: path))
        request.httpMethod = method
        return try await perform(request, acceptedStatuses: acceptedStatuses)
    }

    private func perform(_ request: URLRequest, acceptedStatuses: Set<Int>) async throws -> Data {
        var request = request
        request.timeoutInterval = 10
        request.cachePolicy = .reloadIgnoringLocalCacheData

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session.data(for: request)
        } catch let error as URLError where error.code == .cancelled {
            throw NeriticError.cancelled
        } catch let error as URLError where error.code == .networkConnectionLost {
            // The peer vanished mid-request. For a restart command that is the
            // expected outcome, so it gets its own case and callers decide
            // whether it counts as a failure.
            throw NeriticError.connectionLost
        } catch {
            throw NeriticError.unreachable(underlying: "\(error)")
        }

        guard let http = response as? HTTPURLResponse else {
            throw NeriticError.decoding(underlying: "non-HTTP response")
        }
        guard acceptedStatuses.contains(http.statusCode) else {
            throw NeriticError.http(status: http.statusCode, message: message(from: data))
        }
        return data
    }

    private func decode<T: Decodable>(_ data: Data) throws -> T {
        do {
            return try decoder.decode(T.self, from: data)
        } catch {
            throw NeriticError.decoding(underlying: "\(error)")
        }
    }

    /// Pulls a human-readable message out of an error body when there is one.
    private func message(from data: Data) -> String? {
        guard !data.isEmpty else { return nil }
        if let envelope = try? decoder.decode(SuccessResponse.self, from: data) {
            if let message = envelope.message, !message.isEmpty { return message }
            if let error = envelope.error, !error.isEmpty { return error }
        }
        if let text = String(data: data, encoding: .utf8), !text.isEmpty {
            return String(text.prefix(200))
        }
        return nil
    }
}
