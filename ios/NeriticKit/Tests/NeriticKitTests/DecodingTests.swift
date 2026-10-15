import Foundation
import Testing

@testable import NeriticKit

/// Payloads captured from the firmware's JSON generators in
/// `src/webserver.cpp` (see `getStateJSON`, `getPresetsJSON`, `buildEffectsCache`)
/// and `defaults/config.json`.
private enum Fixture {
    static let state = Data("""
    {"power":true,"effect":2,"preset":3,"params":{"speed":40,"intensity":35,\
    "colors":["#FF5500","#280078"]},"brightness":65,"transitionTime":8000,\
    "time":"12:34","sunrise":"06:12","sunset":"19:44"}
    """.utf8)

    /// The device reports "--:--" for the clock until NTP has synchronised.
    static let stateWithoutClock = Data("""
    {"power":false,"effect":0,"preset":0,"params":{"speed":255,"intensity":128,\
    "colors":["#000000","#000000"]},"brightness":0,"transitionTime":10000,\
    "time":"--:--","sunrise":"","sunset":""}
    """.utf8)

    static let presets = Data("""
    {"presets":[{"id":0,"name":"Off","effect":0,"enabled":true,"params":\
    {"speed":255,"intensity":100,"colors":["#000000"]}},{"id":3,"name":"Sunset",\
    "effect":2,"enabled":true,"params":{"speed":64,"intensity":45,"colors":\
    ["#FF0050","#B400FF"]}}]}
    """.utf8)

    static let effects = Data("""
    {"effects":[{"id":0,"name":"Solid"},{"id":1,"name":"Sunrise"},\
    {"id":8,"name":"Tidal Surge"}]}
    """.utf8)

    static let config = Data("""
    {"network":{"hostname":"Neritic","apPassword":"","ssid":""},\
    "led":{"pin":14,"count":34,"type":"SK6812","colorOrder":"GRB","relayPin":2,\
    "relayActiveHigh":true},"safety":{"maxBrightness":80,\
    "minTransitionTime":5000},"time":{"ntpServer":"pool.ntp.org",\
    "timezone":"America/Santiago","latitude":0.0,"longitude":0.0},\
    "transitionTimes":{"powerOn":60000,"schedule":10800000,"manual":10000,\
    "effect":10000},"timers":[{"id":0,"enabled":true,"type":0,"hour":23,\
    "minute":0,"presetId":0,"brightness":0}]}
    """.utf8)

    static let timezones = Data("""
    [{"name":"America/Santiago","gmt":"GMT -3"},{"name":"Europe/Berlin","gmt":""}]
    """.utf8)

    static let update = Data("""
    {"current":"v2.0.0","latest":[{"env":"esp32c6","version":"v2.0.1",\
    "url":"https://example.invalid/firmware.bin"}]}
    """.utf8)

    /// The device answers 502 with this body when it cannot reach GitHub.
    static let updateUnreachable = Data(#"{"error":"Could not reach GitHub"}"#.utf8)
}

@Suite("REST payload decoding")
struct DecodingTests {
    private let decoder = JSONDecoder()

    @Test("State decodes with all fields present")
    func stateDecodes() throws {
        let state = try decoder.decode(DeviceState.self, from: Fixture.state)

        #expect(state.power == true)
        #expect(state.brightness == 65)
        #expect(state.effect == 2)
        #expect(state.preset == 3)
        #expect(state.transitionTime == 8000)
        #expect(state.params?.speed == 40)
        #expect(state.params?.intensity == 35)
        #expect(state.params?.colors == ["#FF5500", "#280078"])
        #expect(state.time == "12:34")
        #expect(state.hasValidTime)
    }

    @Test("State with an unsynchronised clock is not treated as valid time")
    func stateWithoutClock() throws {
        let state = try decoder.decode(DeviceState.self, from: Fixture.stateWithoutClock)

        #expect(state.time == "--:--")
        #expect(state.hasValidTime == false)
        #expect(state.power == false)
    }

    @Test("Partial state updates decode without the omitted fields")
    func partialStateDecodes() throws {
        let state = try decoder.decode(DeviceState.self, from: Data(#"{"power":true}"#.utf8))

        #expect(state.power == true)
        #expect(state.brightness == nil)
        #expect(state.params == nil)
    }

    @Test("Presets decode, including the raw preset speed the device returns")
    func presetsDecode() throws {
        let response = try decoder.decode(PresetsResponse.self, from: Fixture.presets)

        #expect(response.presets.count == 2)
        #expect(response.presets[0].name == "Off")
        #expect(response.presets[1].effect == 2)
        // Documented firmware asymmetry: preset speed arrives in the internal
        // 0-255 range while intensity arrives as a percentage.
        #expect(response.presets[0].params.speed == 255)
        #expect(response.presets[0].params.intensity == 100)
    }

    @Test("Effects decode")
    func effectsDecode() throws {
        let response = try decoder.decode(EffectsResponse.self, from: Fixture.effects)

        #expect(response.effects.map(\.name) == ["Solid", "Sunrise", "Tidal Surge"])
        #expect(response.effects.last?.id == 8)
    }

    @Test("Config decodes with every section")
    func configDecodes() throws {
        let config = try decoder.decode(DeviceConfig.self, from: Fixture.config)

        #expect(config.led?.count == 34)
        #expect(config.led?.colorOrder == "GRB")
        #expect(config.safety?.maxBrightness == 80)
        #expect(config.time?.timezone == "America/Santiago")
        #expect(config.transitionTimes?.powerOn == 60000)
        #expect(config.timers?.count == 1)
        #expect(config.timers?.first?.presetId == 0)
        // The device never returns the station password on read.
        #expect(config.network?.password == nil)
    }

    @Test("A config round trip only sends what the firmware accepts")
    func configUpdateRoundTrip() throws {
        let config = try decoder.decode(DeviceConfig.self, from: Fixture.config)
        let update = config.asUpdate()

        let encoded = try JSONEncoder().encode(update)
        let object = try #require(
            JSONSerialization.jsonObject(with: encoded) as? [String: Any]
        )

        #expect(Set(object.keys) == ["led", "safety", "time", "transitionTimes", "network", "timers"])
    }

    @Test("Timers decode and fall back to a stable identity without an id")
    func timersDecode() throws {
        let config = try decoder.decode(DeviceConfig.self, from: Fixture.config)
        let timer = try #require(config.timers?.first)

        #expect(timer.stableID == "id-0")

        let withoutID = TimerConfig(enabled: true, type: 1, hour: 6, minute: 30, presetId: 1, brightness: 60)
        #expect(withoutID.stableID == "1-6-30-1")
    }

    @Test("Timezone offsets are used as sent, including when empty")
    func timezonesDecode() throws {
        let zones = try decoder.decode([TimezoneOption].self, from: Fixture.timezones)

        #expect(zones[0].label == "America/Santiago (GMT -3)")
        // The device has not synchronised yet, so no offset is known.
        #expect(zones[1].label == "Europe/Berlin")
    }

    @Test("Update info decodes the manifest and picks the matching environment")
    func updateDecodes() throws {
        let info = try decoder.decode(UpdateInfo.self, from: Fixture.update)

        #expect(info.current == "v2.0.0")
        #expect(info.latestVersion(forEnvironment: "esp32c6") == "v2.0.1")
        #expect(info.latest?.first?.url == "https://example.invalid/firmware.bin")
    }

    @Test("A 502 update body decodes into the error rather than throwing")
    func updateUnreachableDecodes() throws {
        let info = try decoder.decode(UpdateInfo.self, from: Fixture.updateUnreachable)

        #expect(info.error == "Could not reach GitHub")
        #expect(info.latest == nil)
    }

    @Test("Wi-Fi scan accepts both response shapes the device can send")
    func wifiScanDecodes() throws {
        let array = try WiFiScanResponse.decode(from: Data(#"["Home","Cafe"]"#.utf8))
        #expect(array == ["Home", "Cafe"])

        let envelope = try WiFiScanResponse.decode(from: Data(#"{"ssids":["Home"]}"#.utf8))
        #expect(envelope == ["Home"])
    }
}
