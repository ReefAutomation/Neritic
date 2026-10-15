import Foundation

/// The device's wall clock.
///
/// The firmware broadcasts state only when something *changes*
/// (`broadcastState()` is called from `src/state.cpp`, never on a timer), so the
/// `time` field is a snapshot that goes stale the moment it arrives. Clients are
/// expected to tick forward from it — this is what `web/LiveClock.jsx` does.
public enum DeviceClock {

    public static let secondsPerDay: Double = 24 * 60 * 60

    /// Parses a device time string ("HH:MM" or "HH:MM:SS") into seconds since
    /// midnight, or nil when the device has no usable clock.
    ///
    /// Values are validated rather than coerced: out-of-range fields return nil
    /// so a garbled string cannot be displayed as a plausible time.
    public static func secondsSinceMidnight(from time: String?) -> Double? {
        guard let time else { return nil }
        let trimmed = time.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, trimmed != "--:--" else { return nil }

        let parts = trimmed.split(separator: ":")
        guard parts.count == 2 || parts.count == 3 else { return nil }

        var values: [Int] = []
        for part in parts {
            guard let value = Int(part) else { return nil }
            values.append(value)
        }

        let hours = values[0]
        let minutes = values[1]
        let seconds = values.count == 3 ? values[2] : 0

        guard (0...23).contains(hours),
              (0...59).contains(minutes),
              (0...59).contains(seconds)
        else { return nil }

        return Double(hours * 3600 + minutes * 60 + seconds)
    }

    /// Wraps an absolute second count into a single day.
    public static func wrapped(_ seconds: Double) -> Double {
        let day = secondsPerDay
        let remainder = seconds.truncatingRemainder(dividingBy: day)
        return remainder < 0 ? remainder + day : remainder
    }

    /// Formats seconds as "HH:MM:SS", wrapped into a single day.
    public static func displayString(seconds: Double) -> String {
        let total = Int(wrapped(seconds).rounded(.down))
        let hours = total / 3600
        let minutes = (total % 3600) / 60
        let secs = total % 60
        return String(format: "%02d:%02d:%02d", hours, minutes, secs)
    }
}

/// A device clock reading, anchored to the moment it was received so the display
/// can advance between broadcasts instead of freezing.
public struct DeviceClockAnchor: Equatable, Sendable {
    /// Seconds since midnight as reported by the device.
    public let secondsSinceMidnight: Double
    /// When that reading arrived.
    public let receivedAt: Date

    public init(secondsSinceMidnight: Double, receivedAt: Date = Date()) {
        self.secondsSinceMidnight = secondsSinceMidnight
        self.receivedAt = receivedAt
    }

    /// Builds an anchor from a device time string, or nil if there is no
    /// synchronised clock yet.
    public init?(deviceTime: String?, receivedAt: Date = Date()) {
        guard let seconds = DeviceClock.secondsSinceMidnight(from: deviceTime) else { return nil }
        self.init(secondsSinceMidnight: seconds, receivedAt: receivedAt)
    }

    /// Device time at a given instant, wrapped into a single day.
    public func seconds(at instant: Date) -> Double {
        DeviceClock.wrapped(secondsSinceMidnight + instant.timeIntervalSince(receivedAt))
    }

    /// "HH:MM:SS" at a given instant.
    public func displayString(at instant: Date) -> String {
        DeviceClock.displayString(seconds: seconds(at: instant))
    }

    /// Minutes since midnight, for placing the schedule graph's "now" marker.
    public func minutes(at instant: Date) -> Double {
        seconds(at: instant) / 60
    }
}
