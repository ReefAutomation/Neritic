import Foundation

// MARK: - Units
//
// The device exposes brightness, speed and intensity as PERCENTAGES (0-100) on
// /api/state, /api/config and /api/timer. The device converts to its internal
// 0-255 range itself (see `percentToHex` / `hexToPercent` in the firmware's
// src/config.h); clients must never do that conversion.
//
// One exception exists and is a firmware inconsistency, not a client concern:
// GET /api/presets returns `params.speed` in the internal 0-255 range while
// POST /api/preset expects a percentage. See `PresetParams.speed`.

// MARK: - State (/api/state)

/// Payload of `GET /api/state` and of the text frames pushed over `/ws`.
///
/// Every field is optional: the firmware omits nothing today, but partial
/// updates must not fail decoding.
public struct DeviceState: Codable, Equatable, Sendable {
    public var power: Bool?
    /// Percentage, 0-100.
    public var brightness: Int?
    public var effect: Int?
    public var preset: Int?
    public var params: EffectParams?
    public var transitionTime: Int?
    /// "HH:MM", or "--:--" until the device has synchronised its clock.
    public var time: String?
    /// "HH:MM", empty string when unknown.
    public var sunrise: String?
    public var sunset: String?

    public init(
        power: Bool? = nil,
        brightness: Int? = nil,
        effect: Int? = nil,
        preset: Int? = nil,
        params: EffectParams? = nil,
        transitionTime: Int? = nil,
        time: String? = nil,
        sunrise: String? = nil,
        sunset: String? = nil
    ) {
        self.power = power
        self.brightness = brightness
        self.effect = effect
        self.preset = preset
        self.params = params
        self.transitionTime = transitionTime
        self.time = time
        self.sunrise = sunrise
        self.sunset = sunset
    }

    /// True until the device reports a real wall-clock time.
    public var hasValidTime: Bool { time != nil && time != "--:--" }
}

public struct EffectParams: Codable, Equatable, Sendable {
    /// Percentage, 0-100.
    public var speed: Int?
    /// Percentage, 0-100.
    public var intensity: Int?
    /// Hex strings, e.g. "#FF5500" (some effects use "#RRGGBBWW").
    public var colors: [String]?
    public var reverse: Bool?

    public init(
        speed: Int? = nil,
        intensity: Int? = nil,
        colors: [String]? = nil,
        reverse: Bool? = nil
    ) {
        self.speed = speed
        self.intensity = intensity
        self.colors = colors
        self.reverse = reverse
    }
}

// MARK: - Effects (/api/effects)

public struct Effect: Codable, Equatable, Identifiable, Sendable {
    public var id: Int
    public var name: String

    public init(id: Int, name: String) {
        self.id = id
        self.name = name
    }
}

public struct EffectsResponse: Codable, Sendable {
    public var effects: [Effect]
}

// MARK: - Presets (/api/presets, /api/preset)

public struct Preset: Codable, Equatable, Identifiable, Sendable {
    public var id: Int
    public var name: String
    public var effect: Int
    public var enabled: Bool
    public var params: PresetParams

    public init(id: Int, name: String, effect: Int, enabled: Bool, params: PresetParams) {
        self.id = id
        self.name = name
        self.effect = effect
        self.enabled = enabled
        self.params = params
    }
}

public struct PresetParams: Codable, Equatable, Sendable {
    /// - Warning: firmware inconsistency. `GET /api/presets` returns this in the
    ///   device's internal 0-255 range, but `POST /api/preset` interprets it as a
    ///   percentage (it calls `percentToHex`). A read-modify-write round trip
    ///   therefore does not preserve `speed`.
    ///   Also note that `POST /api/preset` silently defaults a missing `speed`
    ///   to 100% and a missing `intensity` to 50%.
    public var speed: Int?
    /// Percentage, 0-100 (this one *is* converted by the device on read).
    public var intensity: Int?
    public var colors: [String]?

    public init(speed: Int? = nil, intensity: Int? = nil, colors: [String]? = nil) {
        self.speed = speed
        self.intensity = intensity
        self.colors = colors
    }
}

public struct PresetsResponse: Codable, Sendable {
    public var presets: [Preset]
}

/// Body of `POST /api/preset`.
///
/// With `apply: true` the device applies the preset by `id` and ignores every
/// other field. Otherwise it saves the full definition.
public struct PresetWrite: Codable, Sendable {
    public var id: Int
    public var name: String?
    public var effect: Int?
    public var enabled: Bool?
    public var params: PresetParams?
    public var apply: Bool?

    public init(
        id: Int,
        name: String? = nil,
        effect: Int? = nil,
        enabled: Bool? = nil,
        params: PresetParams? = nil,
        apply: Bool? = nil
    ) {
        self.id = id
        self.name = name
        self.effect = effect
        self.enabled = enabled
        self.params = params
        self.apply = apply
    }

    /// Apply an existing preset immediately.
    public static func apply(id: Int) -> PresetWrite {
        PresetWrite(id: id, apply: true)
    }
}

// MARK: - State writes (/api/state POST)

/// Body of `POST /api/state`. All fields are optional (partial update).
///
/// - Important: when changing power *and* brightness together, send both in one
///   value. The firmware deliberately applies `power` first so that a power-off
///   does not briefly relight the strip; splitting them across two requests
///   reintroduces that artefact.
public struct StateUpdate: Codable, Sendable {
    public var power: Bool?
    /// Percentage, 0-100.
    public var brightness: Int?
    public var effect: Int?
    public var params: EffectParams?
    public var transitionTime: Int?

    public init(
        power: Bool? = nil,
        brightness: Int? = nil,
        effect: Int? = nil,
        params: EffectParams? = nil,
        transitionTime: Int? = nil
    ) {
        self.power = power
        self.brightness = brightness
        self.effect = effect
        self.params = params
        self.transitionTime = transitionTime
    }
}

// MARK: - Configuration (/api/config)

public struct DeviceConfig: Codable, Equatable, Sendable {
    public var network: NetworkConfig?
    public var led: LEDConfig?
    public var safety: SafetyConfig?
    public var time: TimeConfig?
    public var transitionTimes: TransitionTimesConfig?
    public var timers: [TimerConfig]?

    public init(
        network: NetworkConfig? = nil,
        led: LEDConfig? = nil,
        safety: SafetyConfig? = nil,
        time: TimeConfig? = nil,
        transitionTimes: TransitionTimesConfig? = nil,
        timers: [TimerConfig]? = nil
    ) {
        self.network = network
        self.led = led
        self.safety = safety
        self.time = time
        self.transitionTimes = transitionTimes
        self.timers = timers
    }

    /// The subset of the config that the firmware is willing to accept.
    /// `Configuration::partialUpdate` in src/config.cpp ignores everything else,
    /// so the app must not invent fields.
    public func asUpdate() -> ConfigUpdate {
        ConfigUpdate(
            network: network,
            led: led,
            safety: safety,
            time: time,
            transitionTimes: transitionTimes,
            timers: timers
        )
    }
}

/// Body of `POST /api/config`. Only the supplied sections are changed, and
/// within a section only the supplied keys (the firmware checks each key
/// individually before applying it).
public struct ConfigUpdate: Codable, Sendable {
    public var network: NetworkConfig?
    public var led: LEDConfig?
    public var safety: SafetyConfig?
    public var time: TimeConfig?
    public var transitionTimes: TransitionTimesConfig?
    public var timers: [TimerConfig]?

    public init(
        network: NetworkConfig? = nil,
        led: LEDConfig? = nil,
        safety: SafetyConfig? = nil,
        time: TimeConfig? = nil,
        transitionTimes: TransitionTimesConfig? = nil,
        timers: [TimerConfig]? = nil
    ) {
        self.network = network
        self.led = led
        self.safety = safety
        self.time = time
        self.transitionTimes = transitionTimes
        self.timers = timers
    }
}

public struct NetworkConfig: Codable, Equatable, Sendable {
    public var ssid: String?
    public var password: String?
    public var hostname: String?
    /// Password for the device's own setup access point.
    public var apPassword: String?

    public init(ssid: String? = nil, password: String? = nil, hostname: String? = nil, apPassword: String? = nil) {
        self.ssid = ssid
        self.password = password
        self.hostname = hostname
        self.apPassword = apPassword
    }
}

public struct LEDConfig: Codable, Equatable, Sendable {
    public var pin: Int?
    public var count: Int?
    public var type: String?
    public var colorOrder: String?
    public var relayPin: Int?
    public var relayActiveHigh: Bool?

    public init(
        pin: Int? = nil,
        count: Int? = nil,
        type: String? = nil,
        colorOrder: String? = nil,
        relayPin: Int? = nil,
        relayActiveHigh: Bool? = nil
    ) {
        self.pin = pin
        self.count = count
        self.type = type
        self.colorOrder = colorOrder
        self.relayPin = relayPin
        self.relayActiveHigh = relayActiveHigh
    }
}

public struct SafetyConfig: Codable, Equatable, Sendable {
    /// Percentage, 0-100 (converted to the internal range by the device).
    public var maxBrightness: Int?
    public var minTransitionTime: Int?

    public init(maxBrightness: Int? = nil, minTransitionTime: Int? = nil) {
        self.maxBrightness = maxBrightness
        self.minTransitionTime = minTransitionTime
    }
}

public struct TimeConfig: Codable, Equatable, Sendable {
    public var ntpServer: String?
    public var timezone: String?
    public var latitude: Double?
    public var longitude: Double?

    public init(ntpServer: String? = nil, timezone: String? = nil, latitude: Double? = nil, longitude: Double? = nil) {
        self.ntpServer = ntpServer
        self.timezone = timezone
        self.latitude = latitude
        self.longitude = longitude
    }
}

public struct TransitionTimesConfig: Codable, Equatable, Sendable {
    public var powerOn: Int?
    public var schedule: Int?
    public var manual: Int?
    public var effect: Int?

    public init(powerOn: Int? = nil, schedule: Int? = nil, manual: Int? = nil, effect: Int? = nil) {
        self.powerOn = powerOn
        self.schedule = schedule
        self.manual = manual
        self.effect = effect
    }
}

public struct TimerConfig: Codable, Equatable, Identifiable, Sendable {
    /// Present on read (`GET /api/config`). `POST /api/timer` identifies the
    /// timer by this id.
    public var id: Int?
    public var enabled: Bool
    /// 0 = regular, 1 = sunrise, 2 = sunset.
    public var type: Int
    public var hour: Int
    public var minute: Int
    public var presetId: Int
    /// Percentage, 0-100.
    public var brightness: Int

    public init(
        id: Int? = nil,
        enabled: Bool,
        type: Int,
        hour: Int,
        minute: Int,
        presetId: Int,
        brightness: Int
    ) {
        self.id = id
        self.enabled = enabled
        self.type = type
        self.hour = hour
        self.minute = minute
        self.presetId = presetId
        self.brightness = brightness
    }

    /// Identity for SwiftUI lists; falls back to the schedule when the device
    /// did not send an id.
    public var stableID: String {
        if let id { return "id-\(id)" }
        return "\(type)-\(hour)-\(minute)-\(presetId)"
    }
}

public enum TimerKind: Int, Sendable, CaseIterable {
    case regular = 0
    case sunrise = 1
    case sunset = 2

    public var label: String {
        switch self {
        case .regular: return "Clock"
        case .sunrise: return "Sunrise"
        case .sunset: return "Sunset"
        }
    }
}

// MARK: - Timezones (/api/timezones)

public struct TimezoneOption: Codable, Equatable, Identifiable, Sendable {
    public var name: String
    /// Device-supplied offset such as "GMT -3". Empty until the device has
    /// synchronised its clock; clients must not derive it themselves.
    public var gmt: String

    public var id: String { name }

    public init(name: String, gmt: String) {
        self.name = name
        self.gmt = gmt
    }

    /// `America/Santiago (GMT -3)`, or just the name when no offset is known.
    public var label: String {
        gmt.isEmpty ? name : "\(name) (\(gmt))"
    }
}

// MARK: - Version and updates (/api/version, /api/update)

public struct VersionResponse: Codable, Equatable, Sendable {
    public var version: String

    public init(version: String) {
        self.version = version
    }
}

/// One entry of the release manifest published with each GitHub release.
public struct FirmwareRelease: Codable, Equatable, Sendable {
    /// Build environment, e.g. "esp32c6".
    public var env: String?
    public var version: String?
    public var url: String?

    public init(env: String? = nil, version: String? = nil, url: String? = nil) {
        self.env = env
        self.version = version
        self.url = url
    }
}

/// `GET /api/update` returns `{"current":"1.2.3","latest":[...]}`, or a 502 with
/// `{"error":"..."}` when the device cannot reach GitHub.
public struct UpdateInfo: Codable, Equatable, Sendable {
    public var current: String?
    public var latest: [FirmwareRelease]?
    public var error: String?

    public init(current: String? = nil, latest: [FirmwareRelease]? = nil, error: String? = nil) {
        self.current = current
        self.latest = latest
        self.error = error
    }

    /// Newest version advertised for a given build environment.
    public func latestVersion(forEnvironment env: String?) -> String? {
        guard let latest else { return nil }
        if let env, let match = latest.first(where: { $0.env == env }), let version = match.version {
            return version
        }
        return latest.compactMap(\.version).first
    }
}

/// Status frames pushed to clients that subscribed with `{"type":"ota_client"}`.
public struct OTAStatus: Codable, Equatable, Sendable {
    public var status: String
    public var message: String?
    public var progress: Int?

    public init(status: String, message: String? = nil, progress: Int? = nil) {
        self.status = status
        self.message = message
        self.progress = progress
    }

    public var isTerminal: Bool { status == "success" || status == "error" }
}

// MARK: - Wi-Fi provisioning (/wifi/scan, /wifi)

/// `GET /wifi/scan` returns a bare JSON array of SSID strings.
public enum WiFiScanResponse {
    /// Decodes either `["ssid", ...]` or `{"ssids":[...]}`.
    public static func decode(from data: Data) throws -> [String] {
        let decoder = JSONDecoder()
        if let names = try? decoder.decode([String].self, from: data) {
            return names
        }
        return try decoder.decode(WiFiScanEnvelope.self, from: data).ssids
    }

    private struct WiFiScanEnvelope: Codable {
        var ssids: [String]
    }
}

/// `POST /wifi` is form-encoded (`ssid`, `password`), replies with HTML and
/// reboots the device.
public struct WiFiCredentials: Sendable {
    public var ssid: String
    public var password: String

    public init(ssid: String, password: String) {
        self.ssid = ssid
        self.password = password
    }
}

// MARK: - Simple acknowledgements

public struct CommandRequest: Codable, Sendable {
    public var command: String

    public init(command: String) {
        self.command = command
    }
}

public struct SuccessResponse: Codable, Equatable, Sendable {
    public var success: Bool?
    public var message: String?
    public var error: String?
}
