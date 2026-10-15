import Foundation
import Testing

@testable import NeriticKit

/// These expectations are derived from the algorithm in `web/Graph.jsx` so the
/// app and the browser draw the same schedule curve.
@Suite("Schedule timeline")
struct BrightnessTimelineTests {

    private func timer(
        hour: Int,
        minute: Int = 0,
        brightness: Int,
        presetId: Int,
        enabled: Bool = true
    ) -> TimerConfig {
        TimerConfig(
            enabled: enabled,
            type: TimerKind.regular.rawValue,
            hour: hour,
            minute: minute,
            presetId: presetId,
            brightness: brightness
        )
    }

    private func preset(id: Int, color: String) -> Preset {
        Preset(
            id: id,
            name: "Preset \(id)",
            effect: 0,
            enabled: true,
            params: PresetParams(speed: 100, intensity: 50, colors: [color])
        )
    }

    // MARK: - No schedule

    @Test("With no timers the curve is flat at the current brightness")
    func flatWithoutTimers() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 42,
            timers: [],
            presets: [],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 60_000, manual: 0, effect: 0)
        )

        #expect(timeline.hasSchedule == false)
        #expect(timeline.points.count == 25)
        #expect(timeline.points.allSatisfy { $0.brightness == 42 })
        #expect(timeline.bands.isEmpty)
        #expect(timeline.points.first?.minute == 0)
        #expect(timeline.points.last?.minute == 1440)
    }

    @Test("Disabled timers alone still produce a flat curve")
    func flatWithOnlyDisabledTimers() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 10,
            timers: [timer(hour: 8, brightness: 90, presetId: 0, enabled: false)],
            presets: [preset(id: 0, color: "#FFFFFF")],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 60_000, manual: 0, effect: 0)
        )

        #expect(timeline.hasSchedule == false)
        #expect(timeline.points.count == 25)
    }

    @Test("A missing schedule duration falls back to a flat curve rather than guessing")
    func flatWithoutTransitionTimes() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 55,
            timers: [timer(hour: 8, brightness: 90, presetId: 0)],
            presets: [preset(id: 0, color: "#FFFFFF")],
            transitionTimes: nil
        )

        #expect(timeline.hasSchedule == false)
        #expect(timeline.points.allSatisfy { $0.brightness == 55 })
    }

    // MARK: - Curve shape

    @Test("A ramp steps from the previous level to the scheduled one")
    func rampSteps() {
        // 07:00 → 60%, 21:00 → 4%, one-minute scheduled fade.
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 0,
            timers: [
                timer(hour: 7, brightness: 60, presetId: 1),
                timer(hour: 21, brightness: 4, presetId: 2),
            ],
            presets: [preset(id: 1, color: "#FF5500"), preset(id: 2, color: "#280078")],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 60_000, manual: 0, effect: 0)
        )

        #expect(timeline.hasSchedule)
        #expect(timeline.points.map(\.minute) == [0, 420, 421, 1260, 1261, 1440])
        #expect(timeline.points.map(\.brightness) == [4, 4, 60, 60, 4, 4])
    }

    @Test("Points are sorted and unique by minute")
    func sortedAndUnique() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 0,
            timers: [
                timer(hour: 23, brightness: 0, presetId: 0),
                timer(hour: 7, brightness: 60, presetId: 0),
                timer(hour: 9, brightness: 100, presetId: 0),
            ],
            presets: [preset(id: 0, color: "#FFFFFF")],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 10_800_000, manual: 0, effect: 0)
        )

        let minutes = timeline.points.map(\.minute)
        #expect(minutes == minutes.sorted())
        #expect(Set(minutes).count == minutes.count)
        #expect(minutes.first == 0)
        #expect(minutes.last == 1440)
    }

    @Test("Brightness is clamped to 100%")
    func clampsBrightness() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 0,
            timers: [timer(hour: 12, brightness: 100, presetId: 0)],
            presets: [preset(id: 0, color: "#FFFFFF")],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 3_600_000, manual: 0, effect: 0)
        )

        #expect(timeline.points.allSatisfy { $0.brightness <= 100 })
    }

    // MARK: - Bands

    @Test("Bands take their colour from the preset the schedule selects")
    func bandsFollowActivePreset() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 0,
            timers: [
                timer(hour: 7, brightness: 60, presetId: 1),
                timer(hour: 21, brightness: 4, presetId: 2),
            ],
            presets: [preset(id: 1, color: "#FF5500"), preset(id: 2, color: "#280078")],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 60_000, manual: 0, effect: 0)
        )

        // 0→420 still belongs to the 21:00 preset (it is the last one in effect
        // before the day rolls over), then 07:00 takes over.
        #expect(timeline.bands.map(\.hex) == [
            "#FF5500", "#FF5500", "#FF5500", "#280078", "#280078",
        ])
        #expect(timeline.bands.first?.startMinute == 0)
        #expect(timeline.bands.first?.endMinute == 420)
        #expect(timeline.bands.last?.endMinute == 1440)
    }

    @Test("A band degrades to the default colour when no preset matches")
    func bandWithoutPreset() {
        let timeline = BrightnessTimelineBuilder.build(
            currentBrightness: 0,
            timers: [timer(hour: 8, brightness: 50, presetId: 99)],
            presets: [],
            transitionTimes: TransitionTimesConfig(powerOn: 0, schedule: 60_000, manual: 0, effect: 0)
        )

        #expect(timeline.bands.isEmpty == false)
        #expect(timeline.bands.allSatisfy { $0.hex == nil })
    }
}
