import Foundation
import Observation
import NeriticKit

/// Everything the UI needs: which controller we are talking to, the cached data
/// we have read from it, and the actions we can ask it to perform.
///
/// State that changes continuously (power, brightness, effect…) comes from the
/// live WebSocket and is kept in ``deviceState``; the slower configuration
/// documents are fetched on connect and refreshed after writes.
@MainActor
@Observable
final class AppModel {

    // MARK: - Connection

    private let store = DeviceStore()
    private(set) var host: String?
    private(set) var client: NeriticClient?
    private(set) var stream: LiveStripStream?

    /// Live socket state.
    ///
    /// Read straight from the stream rather than cached in a stored property:
    /// a cached copy is only ever as fresh as the last code that remembered to
    /// update it, which is how the status indicator ended up showing "Paused"
    /// while the connection was in fact delivering live data.
    var connectionState: LiveStripStream.ConnectionState { stream?.state ?? .idle }

    /// True once we have a host, even if the socket has not come up yet.
    var isConfigured: Bool { host != nil }

    // MARK: - Device data

    private(set) var deviceState = DeviceState()
    private(set) var frame: LEDFrame?
    private(set) var config: DeviceConfig?
    private(set) var presets: [Preset] = []
    private(set) var effects: [Effect] = []
    private(set) var timezones: [TimezoneOption] = []
    private(set) var firmwareVersion: String?

    // MARK: - Activity

    private(set) var isBusy = false
    private(set) var lastError: String?
    private(set) var updateInfo: UpdateInfo?

    /// Live OTA progress reported by the device. Populated only after
    /// ``startUpdate()``, which subscribes this client to `ota_status` frames.
    var otaStatus: OTAStatus? { stream?.otaStatus }

    init() {
        if let saved = store.savedHost {
            configure(host: saved, persist: false)
        }
    }

    // MARK: - Connecting

    /// Points the app at a controller. `persist` is false when restoring the
    /// saved host so a typo cannot overwrite a working value.
    func connect(to host: String, persist: Bool = true) {
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, NeriticClient(host: trimmed) != nil else {
            lastError = "That does not look like a valid address."
            return
        }
        stream?.suspend()
        configure(host: trimmed, persist: persist)
    }

    private func configure(host: String, persist: Bool) {
        guard let client = NeriticClient(host: host),
              let stream = LiveStripStream(host: host) else {
            lastError = "That does not look like a valid address."
            return
        }

        self.host = host
        self.client = client
        self.stream = stream
        self.lastError = nil

        if persist { store.savedHost = host }

        stream.onState = { [weak self] state in
            self?.apply(state)
        }
        stream.onFrame = { [weak self] frame in
            self?.frame = frame
        }
        stream.connect()

        Task { await refreshAll() }
    }

    func forgetDevice() {
        stream?.suspend()
        stream = nil
        client = nil
        host = nil
        store.forget()
        deviceState = DeviceState()
        frame = nil
        config = nil
        presets = []
        effects = []
        timezones = []
        firmwareVersion = nil
        updateInfo = nil
    }

    // MARK: - App lifecycle

    /// Called when the app leaves the foreground.
    ///
    /// The socket is closed rather than "paused": the firmware's
    /// `client_pause` message only suppresses text broadcasts, so the 400 ms
    /// binary LED stream would otherwise keep arriving in the background.
    func applicationDidEnterBackground() {
        stream?.suspend()
    }

    /// Called when the app returns to the foreground.
    func applicationWillEnterForeground() {
        stream?.connect()
    }

    // MARK: - Reading

    func refreshAll() async {
        guard let client else { return }
        isBusy = true
        defer { isBusy = false }

        async let version = attempt { try await client.version() }
        async let effects = attempt { try await client.effects() }
        async let presets = attempt { try await client.presets() }
        async let config = attempt { try await client.config() }
        async let timezones = attempt { try await client.timezones() }

        if let value = await version { firmwareVersion = value }
        if let value = await effects { self.effects = value }
        if let value = await presets { self.presets = value }
        if let value = await config { self.config = value }
        if let value = await timezones { self.timezones = value }
    }

    private func apply(_ state: DeviceState) {
        // The device sends whole states, so replace rather than merge; a partial
        // payload (used by some endpoints) still merges field by field.
        if state.params != nil || state.power != nil || state.brightness != nil {
            deviceState = state
        }
        if let preset = state.preset, preset != deviceState.preset {
            deviceState.preset = preset
        }
    }

    // MARK: - Control

    func setPower(_ on: Bool) async {
        await send(StateUpdate(power: on, brightness: on ? deviceState.brightness : nil))
    }

    func setBrightness(_ percent: Int) async {
        await send(StateUpdate(brightness: percent))
    }

    func setEffect(_ id: Int) async {
        await send(StateUpdate(effect: id))
    }

    /// Sets how long the device takes to fade between states. `seconds` is
    /// converted to the milliseconds the API expects.
    func setTransitionTime(seconds: Int) async {
        await send(StateUpdate(transitionTime: seconds * 1000))
    }

    /// Sends speed/intensity/colours. Percentages are clamped to 0...100 — the
    /// device owns the conversion to its internal range.
    func setParams(speed: Int? = nil, intensity: Int? = nil, colors: [String]? = nil) async {
        let params = EffectParams(
            speed: speed.map(clampPercent),
            intensity: intensity.map(clampPercent),
            colors: colors
        )
        await send(StateUpdate(params: params))
    }

    func applyPreset(id: Int) async {
        guard let client else { return }
        await perform {
            _ = try await client.applyPreset(id: id)
            // Applying a preset changes power/effect/brightness; the socket will
            // push the new state.
            self.deviceState.preset = id
        }
    }

    func savePreset(_ preset: PresetWrite) async {
        guard let client else { return }
        await perform {
            _ = try await client.savePreset(preset)
            self.presets = try await client.presets()
        }
    }

    private func send(_ update: StateUpdate) async {
        guard let client else { return }
        // Reflect the change immediately so controls feel responsive; the
        // authoritative value arrives over the socket moments later.
        if let power = update.power { deviceState.power = power }
        if let brightness = update.brightness { deviceState.brightness = brightness }
        if let effect = update.effect { deviceState.effect = effect }
        if let params = update.params {
            deviceState.params?.speed = params.speed ?? deviceState.params?.speed
            deviceState.params?.intensity = params.intensity ?? deviceState.params?.intensity
            deviceState.params?.colors = params.colors ?? deviceState.params?.colors
        }

        await perform {
            _ = try await client.setState(update)
        }
    }

    // MARK: - Configuration

    func updateConfig(_ update: ConfigUpdate) async {
        guard let client else { return }
        await perform {
            _ = try await client.updateConfig(update)
            self.config = try await client.config()
        }
    }

    /// Writes a single timer. `POST /api/timer` handles exactly one timer, so
    /// callers must supply an id.
    func updateTimer(_ timer: TimerConfig) async {
        guard let client, timer.id != nil else { return }
        await perform {
            _ = try await client.updateTimer(timer)
            self.config = try await client.config()
        }
    }

    func saveTimers(_ timers: [TimerConfig]) async {
        await updateConfig(ConfigUpdate(timers: timers))
    }

    // MARK: - Device actions

    /// Restarts the controller.
    ///
    /// - Returns: true when the controller accepted the command.
    @discardableResult
    func reboot() async -> Bool {
        guard let client else { return false }
        return await performRestartCommand {
            _ = try await client.reboot()
        }
    }

    /// Erases the configuration and restarts. Destructive.
    @discardableResult
    func factoryReset() async -> Bool {
        guard let client else { return false }
        let accepted = await performRestartCommand {
            _ = try await client.factoryReset()
        }
        if accepted { forgetDevice() }
        return accepted
    }

    /// Runs a command that the device answers and then restarts on.
    ///
    /// The device replies with success and restarts about 200 ms later, so the
    /// socket is frequently torn down before the response is fully read. That
    /// surfaces as a lost connection rather than a clean 200 and is the
    /// *expected* outcome. A request that never connected at all is still
    /// reported as a failure.
    private func performRestartCommand(_ action: () async throws -> Void) async -> Bool {
        isBusy = true
        defer { isBusy = false }

        do {
            try await action()
            lastError = nil
            return true
        } catch NeriticError.connectionLost {
            lastError = nil
            return true
        } catch let error as NeriticError {
            lastError = error.userFacingDescription
            return false
        } catch {
            lastError = error.localizedDescription
            return false
        }
    }

    // MARK: - Firmware

    func checkForUpdates() async {
        guard let client else { return }
        isBusy = true
        defer { isBusy = false }
        do {
            updateInfo = try await client.updateInfo()
            lastError = nil
        } catch let error as NeriticError {
            lastError = error.userFacingDescription
        } catch {
            lastError = error.localizedDescription
        }
    }

    /// Starts the on-device update. The device downloads the image itself and
    /// reports progress over the socket, so the socket is switched to the OTA
    /// subscription first — the firmware only routes `ota_status` to clients
    /// that asked for it.
    func startUpdate() async {
        guard let client else { return }
        stream?.resetOTAStatus()
        stream?.subscribeToOTA()
        await perform {
            _ = try await client.startUpdate()
        }
    }

    func clearOtaStatus() {
        stream?.resetOTAStatus()
        stream?.unsubscribeFromOTA()
    }

    // MARK: - Wi-Fi provisioning

    func scanWiFiNetworks() async -> [String] {
        guard let client else { return [] }
        isBusy = true
        defer { isBusy = false }
        do {
            return try await client.scanWiFiNetworks()
        } catch let error as NeriticError {
            lastError = error.userFacingDescription
            return []
        } catch {
            lastError = error.localizedDescription
            return []
        }
    }

    func setWiFi(ssid: String, password: String) async {
        guard let client else { return }
        await perform {
            // The device persists the credentials and reboots, so the
            // connection dropping here is expected rather than a failure.
            try? await client.setWiFiCredentials(WiFiCredentials(ssid: ssid, password: password))
        }
    }

    // MARK: - Helpers

    func clearError() { lastError = nil }

    /// Runs a device action, translating failures into a user-facing message.
    private func perform(_ action: () async throws -> Void) async {
        isBusy = true
        defer { isBusy = false }
        do {
            try await action()
            lastError = nil
        } catch let error as NeriticError {
            lastError = error.userFacingDescription
        } catch {
            lastError = error.localizedDescription
        }
    }

    /// Runs a read, returning nil on failure so one unreachable endpoint does
    /// not blank the rest of the screen.
    private func attempt<T>(_ action: () async throws -> T) async -> T? {
        do {
            return try await action()
        } catch let error as NeriticError {
            if !error.isRetryable { lastError = error.userFacingDescription }
            return nil
        } catch {
            return nil
        }
    }

    private func clampPercent(_ value: Int) -> Int {
        min(100, max(0, value))
    }
}

extension AppModel {
    /// Effect metadata by id, falling back to the raw id when the registry has
    /// not loaded yet.
    func effectName(for id: Int?) -> String {
        guard let id else { return "—" }
        return effects.first(where: { $0.id == id })?.name ?? "Effect \(id)"
    }

    func presetName(for id: Int?) -> String? {
        guard let id, id != 0 else { return nil }
        return presets.first(where: { $0.id == id })?.name
    }

    /// Timers as configured, sorted sunrise/sunset first then by time, matching
    /// `sortTimers` in the web UI.
    var sortedTimers: [TimerConfig] {
        let timers = config?.timers ?? []
        return timers.sorted { lhs, rhs in
            let lhsSolar = lhs.type == TimerKind.sunrise.rawValue || lhs.type == TimerKind.sunset.rawValue
            let rhsSolar = rhs.type == TimerKind.sunrise.rawValue || rhs.type == TimerKind.sunset.rawValue
            if lhsSolar != rhsSolar { return lhsSolar }
            return (lhs.hour * 60 + lhs.minute) < (rhs.hour * 60 + rhs.minute)
        }
    }
}
