import SwiftUI
import Charts
import NeriticKit

/// The 24-hour schedule graphic, mirroring the graph in `web/Graph.jsx`.
///
/// Three layers, same as the web chart:
/// * shaded bands showing which preset the schedule selects at each time,
/// * a stepped brightness curve with a filled area beneath it,
/// * a dashed red line at the device's current time.
struct BrightnessGraphView: View {
    let timeline: BrightnessTimeline
    let nowMinute: Double?

    /// The web UI's fallback band colour (`rgb(0,116,217)`), used when the
    /// scheduled preset has no colour of its own.
    private static let defaultBandColor = Color(red: 0 / 255, green: 116 / 255, blue: 217 / 255)

    var body: some View {
        Chart {
            ForEach(Array(timeline.bands.enumerated()), id: \.offset) { _, band in
                RectangleMark(
                    xStart: .value("Start", band.startMinute),
                    xEnd: .value("End", band.endMinute),
                    yStart: .value("Low", 0),
                    yEnd: .value("High", 100)
                )
                .foregroundStyle(color(for: band.hex).opacity(0.22))
            }

            ForEach(Array(timeline.points.enumerated()), id: \.offset) { _, point in
                AreaMark(
                    x: .value("Time", point.minute),
                    y: .value("Brightness", point.brightness)
                )
                .interpolationMethod(.linear)
                .foregroundStyle(Color.accentColor.opacity(0.15))
            }

            ForEach(Array(timeline.points.enumerated()), id: \.offset) { _, point in
                LineMark(
                    x: .value("Time", point.minute),
                    y: .value("Brightness", point.brightness)
                )
                // Straight segments, matching the web chart (`tension: 0` with no
                // `stepped`). A step interpolation would turn each scheduled ramp
                // — a diagonal between a ramp end and the next event — into a
                // horizontal run plus a vertical jump.
                .interpolationMethod(.linear)
                .foregroundStyle(Color.accentColor)
                .lineStyle(StrokeStyle(lineWidth: 2))
            }

            if let nowMinute {
                RuleMark(x: .value("Now", nowMinute))
                    .lineStyle(StrokeStyle(lineWidth: 2, dash: [6, 6]))
                    .foregroundStyle(.red)
                    .annotation(position: .top, alignment: .center, spacing: 2) {
                        Text("now")
                            .font(.caption2)
                            .foregroundStyle(.red)
                    }
            }
        }
        .chartXScale(domain: 0...BrightnessTimeline.minutesPerDay)
        .chartYScale(domain: 0...100)
        .chartXAxis {
            AxisMarks(values: [0, 360, 720, 1080, 1440]) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let minutes = value.as(Double.self) {
                        Text(Self.hourLabel(minutes))
                    }
                }
            }
        }
        .chartYAxis {
            AxisMarks(position: .leading, values: [0, 50, 100]) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let level = value.as(Double.self) {
                        Text("\(Int(level))%")
                    }
                }
            }
        }
        .frame(height: 170)
        .accessibilityElement()
        .accessibilityLabel("Schedule brightness graph")
        .accessibilityValue(accessibilityDescription)
    }

    private func color(for hex: String?) -> Color {
        guard let hex else { return Self.defaultBandColor }
        return LEDColor.parse(hex: hex).swiftUIColor
    }

    /// `00:00`, `06:00`, … `24:00`.
    private static func hourLabel(_ minutes: Double) -> String {
        let hours = Int(minutes / 60)
        return String(format: "%02d:00", hours)
    }

    private var accessibilityDescription: String {
        guard timeline.hasSchedule else { return "No schedule configured" }
        let levels = timeline.points.map { Int($0.brightness.rounded()) }
        let lowest = levels.min() ?? 0
        let highest = levels.max() ?? 0
        return "Scheduled brightness ranges from \(lowest) to \(highest) percent across the day"
    }
}
