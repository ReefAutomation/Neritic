import Foundation

/// One vertex of the 24-hour brightness curve.
public struct TimelinePoint: Equatable, Sendable {
    /// Minutes since midnight, 0...1440. Fractional because a ramp is
    /// `transitionDuration` minutes long and need not land on a whole minute.
    public var minute: Double
    /// Brightness in percent, clamped to 0...100.
    public var brightness: Double

    public init(minute: Double, brightness: Double) {
        self.minute = minute
        self.brightness = brightness
    }
}

/// A vertical band behind the curve showing which preset the schedule selects.
public struct TimelineBand: Equatable, Sendable {
    public var startMinute: Double
    public var endMinute: Double
    /// Primary colour of the scheduled preset, or nil to use the default.
    public var hex: String?

    public init(startMinute: Double, endMinute: Double, hex: String?) {
        self.startMinute = startMinute
        self.endMinute = endMinute
        self.hex = hex
    }
}

/// The brightness curve for a full day, derived from the schedule.
public struct BrightnessTimeline: Equatable, Sendable {
    public var points: [TimelinePoint]
    public var bands: [TimelineBand]
    /// False when no timers are enabled: the curve is then a flat line at the
    /// current brightness, matching the web UI.
    public var hasSchedule: Bool

    public init(points: [TimelinePoint], bands: [TimelineBand], hasSchedule: Bool) {
        self.points = points
        self.bands = bands
        self.hasSchedule = hasSchedule
    }

    public static let minutesPerDay: Double = 24 * 60
}

/// Builds the schedule graphic from the device's timers.
///
/// This is a direct port of the timeline maths in `web/Graph.jsx`
/// (`getRampStepPoints`, `calcRampEnd`, `calcStartBrightness`,
/// `buildTimerChartData`, `getBgColors`). The two must agree: the same schedule
/// has to draw the same curve in the app and in the browser, including the
/// wrap-around at midnight.
public enum BrightnessTimelineBuilder {

    /// Builds the curve and bands.
    ///
    /// - Parameters:
    ///   - timers: all timers, in the order the device returned them. Disabled
    ///     timers are ignored for the curve but still consulted for band colours,
    ///     exactly as the web UI does.
    ///   - transitionTimes: supplies the scheduled fade duration. When absent the
    ///     curve cannot be built, so a flat line is returned.
    public static func build(
        currentBrightness: Int,
        timers: [TimerConfig],
        presets: [Preset],
        transitionTimes: TransitionTimesConfig?
    ) -> BrightnessTimeline {
        let minutesPerDay = BrightnessTimeline.minutesPerDay

        let events = timers
            .filter(\.enabled)
            .map { TimelineEvent(time: Double($0.hour * 60 + $0.minute), brightness: Double($0.brightness)) }
            .sorted { $0.time < $1.time }

        guard let transitionTimes, let scheduled = transitionTimes.schedule else {
            return flat(currentBrightness: currentBrightness)
        }

        guard !events.isEmpty else {
            return flat(currentBrightness: currentBrightness)
        }

        // The web UI divides milliseconds by 1000 then by 60, rounds to two
        // decimals and floors at 0.01 minutes.
        var transitionDuration = Double(scheduled) / 1000.0 / 60.0
        transitionDuration = max(0.01, transitionDuration)
        transitionDuration = (transitionDuration * 100).rounded() / 100

        var points = rampStepPoints(
            events: events,
            transitionDuration: transitionDuration,
            minutesPerDay: minutesPerDay
        )

        // Wrap the last event of the day around midnight so the curve closes.
        let count = events.count
        let firstEvent = events[0]
        let lastEvent = events[count - 1]
        let previousEvent = events[(count - 2 + count) % count]
        let lastTime = lastEvent.time

        var midnightBrightness = lastEvent.brightness
        if lastTime + transitionDuration > minutesPerDay, lastTime != firstEvent.time {
            let fraction = (minutesPerDay - lastTime) / transitionDuration
            midnightBrightness = previousEvent.brightness
                + (lastEvent.brightness - previousEvent.brightness) * fraction
        }

        if !points.contains(where: { $0.minute == 0 }) {
            points.append(TimelinePoint(minute: 0, brightness: midnightBrightness))
        }
        if !points.contains(where: { $0.minute == minutesPerDay }) {
            points.append(TimelinePoint(minute: minutesPerDay, brightness: midnightBrightness))
        }

        points.sort { $0.minute < $1.minute }

        // Keep the last value defined for each minute: a ramp end and the next
        // event can share a minute, and the later one wins.
        var seen = Set<Double>()
        var uniquePoints: [TimelinePoint] = []
        for point in points.reversed() where seen.insert(point.minute).inserted {
            uniquePoints.insert(point, at: 0)
        }

        let clamped = uniquePoints.map {
            TimelinePoint(minute: $0.minute, brightness: min(100, $0.brightness))
        }

        return BrightnessTimeline(
            points: clamped,
            bands: bands(for: clamped, timers: timers, presets: presets),
            hasSchedule: true
        )
    }

    // MARK: - Ported helpers

    private struct TimelineEvent {
        var time: Double
        var brightness: Double
    }

    private static func flat(currentBrightness: Int) -> BrightnessTimeline {
        // Mirrors buildFlatChartData: 25 hourly samples at the current level.
        let points = (0...24).map { hour in
            TimelinePoint(minute: Double(hour * 60), brightness: Double(currentBrightness))
        }
        return BrightnessTimeline(points: points, bands: [], hasSchedule: false)
    }

    private static func rampStepPoints(
        events: [TimelineEvent],
        transitionDuration: Double,
        minutesPerDay: Double
    ) -> [TimelinePoint] {
        let count = events.count
        var points: [TimelinePoint] = []

        for index in 0..<count {
            let event = events[index]
            let previousIndex = (index - 1 + count) % count
            let nextIndex = (index + 1) % count

            let rampEnd = calcRampEnd(
                current: event.time,
                transitionDuration: transitionDuration,
                minutesPerDay: minutesPerDay,
                next: events[nextIndex].time
            )
            let startBrightness = calcStartBrightness(
                event: event,
                previousEvent: events[previousIndex],
                events: events,
                previousIndex: previousIndex,
                minutesPerDay: minutesPerDay,
                transitionDuration: transitionDuration
            )

            if points.last?.minute != event.time {
                points.append(TimelinePoint(minute: event.time, brightness: startBrightness))
            }
            if points.last?.minute != rampEnd {
                points.append(TimelinePoint(minute: rampEnd, brightness: event.brightness))
            }
        }

        return points
    }

    private static func calcRampEnd(
        current: Double,
        transitionDuration: Double,
        minutesPerDay: Double,
        next: Double
    ) -> Double {
        var rampEnd = current + transitionDuration
        if rampEnd > minutesPerDay { rampEnd -= minutesPerDay }
        let currentToNext = next > current ? next - current : minutesPerDay - current + next
        if transitionDuration > currentToNext { rampEnd = next }
        return rampEnd
    }

    private static func calcStartBrightness(
        event: TimelineEvent,
        previousEvent: TimelineEvent,
        events: [TimelineEvent],
        previousIndex: Int,
        minutesPerDay: Double,
        transitionDuration: Double
    ) -> Double {
        let currentTime = event.time
        let previousTime = previousEvent.time

        let previousIntendedEnd = previousTime + transitionDuration > minutesPerDay
            ? previousTime + transitionDuration - minutesPerDay
            : previousTime + transitionDuration

        let previousTargetIndex = (previousIndex - 1 + events.count) % events.count
        let previousTargetBrightness = events[previousTargetIndex].brightness

        let previousRampDuration = previousIntendedEnd > previousTime
            ? previousIntendedEnd - previousTime
            : minutesPerDay - previousTime + previousIntendedEnd

        let timeFromPreviousToCurrent = currentTime > previousTime
            ? currentTime - previousTime
            : minutesPerDay - previousTime + currentTime

        var startBrightness = previousEvent.brightness
        if timeFromPreviousToCurrent < previousRampDuration, previousRampDuration > 0 {
            let fraction = timeFromPreviousToCurrent / previousRampDuration
            startBrightness = previousEvent.brightness
                + (previousTargetBrightness - previousEvent.brightness) * (1 - fraction)
        }
        return startBrightness
    }

    /// Colour band for the stretch starting at each point, taken from the preset
    /// the schedule selects at that minute. Mirrors `getBgColors`.
    private static func bands(
        for points: [TimelinePoint],
        timers: [TimerConfig],
        presets: [Preset]
    ) -> [TimelineBand] {
        var result: [TimelineBand] = []

        for (index, point) in points.enumerated() {
            let endMinute = index + 1 < points.count
                ? points[index + 1].minute
                : BrightnessTimeline.minutesPerDay
            guard endMinute > point.minute else { continue }

            let timer = activeTimer(at: point.minute, timers: timers)
            let preset = timer.flatMap { timer in presets.first(where: { $0.id == timer.presetId }) }
                ?? presets.first

            let hex = preset?.params.colors?.first
            result.append(TimelineBand(startMinute: point.minute, endMinute: endMinute, hex: hex))
        }

        return result
    }

    /// The timer in effect at a given minute: the last enabled timer that has
    /// already started, or the first timer when none has.
    private static func activeTimer(at minute: Double, timers: [TimerConfig]) -> TimerConfig? {
        var active: TimerConfig?
        for timer in timers where timer.enabled {
            let timerMinute = Double(timer.hour * 60 + timer.minute)
            if timerMinute <= minute { active = timer }
        }
        return active ?? timers.first
    }
}
