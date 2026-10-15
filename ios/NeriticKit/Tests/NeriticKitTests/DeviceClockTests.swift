import Foundation
import Testing

@testable import NeriticKit

/// The firmware only broadcasts state when something changes, so the `time` it
/// sends is a snapshot. Everything below is about turning that snapshot into a
/// clock that keeps running, the way `web/LiveClock.jsx` does.
@Suite("Device clock")
struct DeviceClockTests {

    // MARK: - Parsing

    @Test("Parses HH:MM and HH:MM:SS")
    func parsesDeviceTime() {
        // The expected values are computed into locals rather than written as
        // arithmetic inside #expect: the macro captures each side of a
        // comparison independently, so `579 * 60` would be inferred as Int and
        // never equal the Double the parser returns.
        let nineThirtyNine = Double(9 * 3600 + 39 * 60)
        let nineThirtyNineAnd26 = nineThirtyNine + 26

        #expect(DeviceClock.secondsSinceMidnight(from: "09:39") == nineThirtyNine)
        #expect(DeviceClock.secondsSinceMidnight(from: "00:00") == 0)
        #expect(DeviceClock.secondsSinceMidnight(from: "23:59:59") == 86399)
        #expect(DeviceClock.secondsSinceMidnight(from: "09:39:26") == nineThirtyNineAnd26)
    }

    @Test("An unsynchronised or unusable clock parses to nothing")
    func rejectsUnusableTime() {
        // Deliberately stricter than the web UI, which substitutes the browser
        // clock. The schedule runs on the device's clock, so showing the phone's
        // time would point at a time the device does not agree with.
        #expect(DeviceClock.secondsSinceMidnight(from: "--:--") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: nil) == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "later") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "9") == nil)
    }

    @Test("Out-of-range fields are rejected rather than wrapped")
    func rejectsOutOfRangeTime() {
        #expect(DeviceClock.secondsSinceMidnight(from: "24:00") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "12:60") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "12:00:60") == nil)
        #expect(DeviceClock.secondsSinceMidnight(from: "12:00:00:00") == nil)
    }

    // MARK: - Wrapping and formatting

    @Test("Seconds wrap into a single day")
    func wrapsAtMidnight() {
        #expect(DeviceClock.wrapped(0) == 0)
        #expect(DeviceClock.wrapped(86399) == 86399)
        #expect(DeviceClock.wrapped(86400) == 0)
        #expect(DeviceClock.wrapped(86400 + 60) == 60)
        #expect(DeviceClock.wrapped(-60) == 86340)
    }

    @Test("Formats as zero-padded HH:MM:SS")
    func formatsTime() {
        #expect(DeviceClock.displayString(seconds: 0) == "00:00:00")
        #expect(DeviceClock.displayString(seconds: 579 * 60 + 26) == "09:39:26")
        #expect(DeviceClock.displayString(seconds: 86399) == "23:59:59")
        // Wraps rather than overflowing the hours field.
        #expect(DeviceClock.displayString(seconds: 86400 + 3601) == "01:00:01")
    }

    // MARK: - Anchoring

    @Test("An anchor advances with real elapsed time")
    func anchorTicksForward() {
        let start = Date(timeIntervalSince1970: 1_000_000)
        let anchor = DeviceClockAnchor(secondsSinceMidnight: 9 * 3600, receivedAt: start)

        #expect(anchor.displayString(at: start) == "09:00:00")
        #expect(anchor.displayString(at: start.addingTimeInterval(1)) == "09:00:01")
        #expect(anchor.displayString(at: start.addingTimeInterval(75)) == "09:01:15")
        #expect(anchor.minutes(at: start.addingTimeInterval(30)) == 540.5)
    }

    @Test("An anchor rolls over midnight")
    func anchorWrapsOverMidnight() {
        let start = Date(timeIntervalSince1970: 1_000_000)
        let anchor = DeviceClockAnchor(secondsSinceMidnight: 23 * 3600 + 59 * 60 + 30, receivedAt: start)

        #expect(anchor.displayString(at: start) == "23:59:30")
        #expect(anchor.displayString(at: start.addingTimeInterval(30)) == "00:00:00")
        #expect(anchor.displayString(at: start.addingTimeInterval(90)) == "00:01:00")
    }

    @Test("An anchor is only built from a usable device time")
    func anchorRequiresDeviceTime() {
        #expect(DeviceClockAnchor(deviceTime: "08:15") != nil)
        #expect(DeviceClockAnchor(deviceTime: "--:--") == nil)
        #expect(DeviceClockAnchor(deviceTime: nil) == nil)
    }

    @Test("Rebuilding an anchor restores the device's exact time")
    func anchorRebuildCorrectsDrift() {
        let start = Date(timeIntervalSince1970: 1_000_000)
        let first = DeviceClockAnchor(secondsSinceMidnight: 3600, receivedAt: start)
        // Suppose 5 minutes of drift accumulated locally...
        #expect(first.displayString(at: start.addingTimeInterval(300)) == "01:05:00")

        // ...then a fresh broadcast re-anchors to the authoritative reading.
        let second = DeviceClockAnchor(
            deviceTime: "01:04:58",
            receivedAt: start.addingTimeInterval(300)
        )
        #expect(second?.displayString(at: start.addingTimeInterval(300)) == "01:04:58")
    }
}
