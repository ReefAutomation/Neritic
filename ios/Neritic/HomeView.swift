import SwiftUI
import NeriticKit
#if canImport(UIKit)
import UIKit
#endif

// Kept as single literals rather than concatenations: splitting these with `+`
// inside `Text` makes the SwiftUI body too expensive to type-check.
private let currentLookFooter = "Effects are the building blocks presets are made of. Adjusting them here changes what is running now, without changing the preset itself."
private let scheduleFooter = "Brightness across the day. The shading shows which preset each period uses; the dashed line is the device's current time."

struct HomeView: View {
    let model: AppModel

    @State private var brightness: Double = 0
    @State private var speed: Double = 0
    @State private var intensity: Double = 0
    @State private var isDragging = false
    @State private var editingPreset: Preset?
    /// Last device clock reading, used to tick the display between broadcasts.
    @State private var clockAnchor: DeviceClockAnchor?

    private var state: DeviceState { model.deviceState }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ConnectionRow(state: model.connectionState, host: model.host)
                    ClockRow(state: state, anchor: clockAnchor)
                    LEDBarView(frame: model.frame, ledCount: model.config?.led?.count)
                        .frame(height: 64)
                        .listRowInsets(EdgeInsets(top: 8, leading: 16, bottom: 8, trailing: 16))
                }

                Section {
                    Toggle("Power", isOn: Binding(
                        get: { state.power ?? false },
                        set: { newValue in Task { await model.setPower(newValue) } }
                    ))
                    .font(.headline)

                    LabeledSlider(
                        title: "Brightness",
                        value: $brightness,
                        range: 0...100,
                        display: "\(Int(brightness.rounded()))%",
                        onCommit: { Task { await model.setBrightness(Int(brightness.rounded())) } }
                    )
                    .disabled(!(state.power ?? false))
                } header: {
                    Text("Output")
                } footer: {
                    if let max = model.config?.safety?.maxBrightness, max < 100 {
                        Text("The device caps brightness at \(max)% for safety.")
                    }
                }

                // Presets are the unit the user picks. An effect is a reusable
                // building block a preset is made of, so the effects are not
                // offered here as a list of independent choices.
                Section {
                    PresetsGrid(
                        presets: model.presets,
                        effects: model.effects,
                        activePreset: state.preset,
                        onApply: { id in Task { await model.applyPreset(id: id) } },
                        onEdit: { editingPreset = $0 }
                    )
                    .listRowInsets(EdgeInsets(top: 12, leading: 16, bottom: 12, trailing: 16))
                } header: {
                    Text("Presets")
                } footer: {
                    Text("Tap a preset to apply it. Touch and hold to edit it.")
                }

                Section {
                    Picker(
                        "Effect",
                        selection: Binding(
                            get: { state.effect ?? 0 },
                            set: { newValue in Task { await model.setEffect(newValue) } }
                        )
                    ) {
                        ForEach(model.effects) { effect in
                            Text(effect.name).tag(effect.id)
                        }
                    }

                    LabeledSlider(
                        title: "Speed",
                        value: $speed,
                        range: 0...100,
                        display: "\(Int(speed.rounded()))%",
                        onCommit: { Task { await model.setParams(speed: Int(speed.rounded())) } }
                    )
                    LabeledSlider(
                        title: "Intensity",
                        value: $intensity,
                        range: 0...100,
                        display: "\(Int(intensity.rounded()))%",
                        onCommit: { Task { await model.setParams(intensity: Int(intensity.rounded())) } }
                    )
                    ColorList(
                        colors: state.params?.colors ?? [],
                        onCommit: { colors in Task { await model.setParams(colors: colors) } }
                    )
                } header: {
                    Text("Current look")
                } footer: {
                    Text(currentLookFooter)
                }

                Section {
                    // Re-rendered once a minute: minute resolution is all the
                    // marker needs, so there is no reason to redraw the chart
                    // every second.
                    TimelineView(.periodic(from: .now, by: 60)) { context in
                        BrightnessGraphView(
                            timeline: timeline,
                            nowMinute: clockAnchor?.minutes(at: context.date)
                        )
                    }
                    .listRowInsets(EdgeInsets(top: 12, leading: 8, bottom: 8, trailing: 16))
                } header: {
                    Text("Schedule")
                } footer: {
                    if timeline.hasSchedule {
                        Text(scheduleFooter)
                    } else {
                        Text("No scheduled changes yet. Add one in Settings, under Schedule.")
                    }
                }
            }
            .navigationTitle("Lights")
            .sheet(item: $editingPreset) { preset in
                PresetEditor(model: model, preset: preset)
            }
            .onChange(of: state.brightness) { _, newValue in
                guard !isDragging, let newValue else { return }
                brightness = Double(newValue)
            }
            .onChange(of: state.params?.speed) { _, newValue in
                guard !isDragging, let newValue else { return }
                speed = Double(newValue)
            }
            .onChange(of: state.params?.intensity) { _, newValue in
                guard !isDragging, let newValue else { return }
                intensity = Double(newValue)
            }
            .onChange(of: state.time) { _, newValue in
                // Re-anchor on every broadcast so accumulated drift is reset.
                clockAnchor = DeviceClockAnchor(deviceTime: newValue)
            }
            .onAppear {
                syncFromState()
                clockAnchor = DeviceClockAnchor(deviceTime: state.time)
            }
        }
    }

    private func syncFromState() {
        if let value = state.brightness { brightness = Double(value) }
        if let value = state.params?.speed { speed = Double(value) }
        if let value = state.params?.intensity { intensity = Double(value) }
    }

    /// Brightness curve for the day, rebuilt from the device's own schedule.
    private var timeline: BrightnessTimeline {
        BrightnessTimelineBuilder.build(
            currentBrightness: state.brightness ?? 0,
            timers: model.config?.timers ?? [],
            presets: model.presets,
            transitionTimes: model.config?.transitionTimes
        )
    }
}

// MARK: - Components

/// Wide horizontal preview of the strip, drawn from the live binary frames.
struct LEDBarView: View {
    let frame: LEDFrame?
    let ledCount: Int?

    var body: some View {
        Canvas { context, size in
            guard let frame, !frame.pixels.isEmpty else {
                context.fill(
                    Path(CGRect(origin: .zero, size: size)),
                    with: .color(.secondary.opacity(0.15))
                )
                return
            }

            let pixels = frame.pixels
            let slotWidth = size.width / CGFloat(pixels.count)

            // Draw each pixel as a band, plus the interpolated blend into its
            // neighbour so a short strip still looks like a smooth gradient.
            for (index, pixel) in pixels.enumerated() {
                let colour = pixel.displayColor
                let next = index + 1 < pixels.count ? pixels[index + 1].displayColor : colour
                let origin = CGPoint(x: CGFloat(index) * slotWidth, y: 0)

                let gradient = Gradient(colors: [
                    colour.swiftUIColor,
                    next.swiftUIColor,
                ])
                let rect = CGRect(
                    x: origin.x,
                    y: 0,
                    width: slotWidth + 0.5,
                    height: size.height
                )
                context.fill(
                    Path(rect),
                    with: .linearGradient(
                        gradient,
                        startPoint: CGPoint(x: rect.minX, y: 0),
                        endPoint: CGPoint(x: rect.maxX, y: 0)
                    )
                )
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(.quaternary)
        )
        .accessibilityLabel("Live light output preview")
    }
}

private struct ClockRow: View {
    let state: DeviceState
    let anchor: DeviceClockAnchor?

    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                if let anchor {
                    // Ticks locally: the device only pushes state when something
                    // changes, so the time it sends is a snapshot. Mirrors
                    // `web/LiveClock.jsx`, which does exactly this.
                    TimelineView(.periodic(from: .now, by: 1)) { context in
                        Text(anchor.displayString(at: context.date))
                            .font(.system(.title, design: .rounded, weight: .semibold))
                            .monospacedDigit()
                    }
                } else {
                    Text("--:--")
                        .font(.system(.title, design: .rounded, weight: .semibold))
                        .monospacedDigit()
                }

                Text(anchor == nil ? "Waiting for the device clock" : "Device time")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer(minLength: 8)
            VStack(alignment: .trailing, spacing: 4) {
                if let sunrise = state.sunrise, !sunrise.isEmpty {
                    Label(sunrise, systemImage: "sunrise")
                }
                if let sunset = state.sunset, !sunset.isEmpty {
                    Label(sunset, systemImage: "sunset")
                }
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
        }
    }
}

private struct LabeledSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    let display: String
    let onCommit: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(display)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            Slider(
                value: $value,
                in: range,
                onEditingChanged: { editing in
                    if !editing { onCommit() }
                }
            )
        }
    }
}

/// Colours used by the current effect. The device takes an array of hex strings.
private struct ColorList: View {
    let colors: [String]
    let onCommit: ([String]) -> Void

    var body: some View {
        ForEach(Array(colors.enumerated()), id: \.offset) { index, hex in
            ColorPicker(
                "Colour \(index + 1)",
                selection: Binding(
                    get: { LEDColor.parse(hex: hex).swiftUIColor },
                    set: { newValue in
                        var updated = colors
                        updated[index] = newValue.hexString
                        onCommit(updated)
                    }
                ),
                supportsOpacity: false
            )
        }
        .disabled(colors.isEmpty)
    }
}

/// Live socket state, shown at the top of the Lights tab.
///
/// The state is read through the model on every render, so it reflects the real
/// connection. It previously rendered from a cached copy of the connection state
/// that nothing refreshed, which is why it read "Paused" while live data was
/// visibly arriving.
struct ConnectionRow: View {
    let state: LiveStripStream.ConnectionState
    let host: String?

    var body: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(colour)
                .frame(width: 10, height: 10)
            VStack(alignment: .leading, spacing: 2) {
                Text(label)
                    .font(.subheadline.weight(.medium))
                if let detail {
                    Text(detail)
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
            Spacer(minLength: 8)
            if let host {
                Text(host)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Connection status: \(label)")
    }

    private var colour: Color {
        switch state {
        case .connected: return .green
        case .connecting: return .yellow
        case .waiting: return .orange
        case .failed: return .red
        case .idle: return .secondary
        }
    }

    private var label: String {
        switch state {
        case .connected: return "Live"
        case .connecting: return "Connecting…"
        case .waiting: return "Reconnecting…"
        case .failed: return "Offline"
        case .idle: return "Not connected"
        }
    }

    private var detail: String? {
        switch state {
        case let .waiting(retryIn, reason):
            return "Retrying in \(Int(retryIn))s — \(reason)"
        case let .failed(message):
            return message
        default:
            return nil
        }
    }
}

/// The preset chooser: the primary way to change what the lights are doing.
///
/// Each tile mirrors the web UI's preset card — name, the effect it uses, and a
/// preview of its colours.
struct PresetsGrid: View {
    let presets: [Preset]
    let effects: [Effect]
    let activePreset: Int?
    let onApply: (Int) -> Void
    let onEdit: (Preset) -> Void

    private let columns = [
        GridItem(.flexible(), spacing: 12),
        GridItem(.flexible(), spacing: 12),
    ]

    var body: some View {
        LazyVGrid(columns: columns, spacing: 12) {
            ForEach(presets) { preset in
                Button {
                    onApply(preset.id)
                } label: {
                    tile(for: preset)
                }
                .buttonStyle(.plain)
                .contextMenu {
                    Button("Edit") { onEdit(preset) }
                }
            }
        }
    }

    private func tile(for preset: Preset) -> some View {
        let isActive = activePreset == preset.id
        return VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(preset.name.isEmpty ? "Preset \(preset.id)" : preset.name)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.primary)
                    .lineLimit(1)
                Spacer(minLength: 4)
                if isActive {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(Color.accentColor)
                }
            }
            Text("Effect: \(effectName(preset.effect))")
                .font(.caption2)
                .foregroundStyle(.secondary)
                .lineLimit(1)
            SwatchStrip(colors: preset.params.colors ?? [])
                .frame(height: 10)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(isActive ? Color.accentColor.opacity(0.15) : Color.secondary.opacity(0.12))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(isActive ? Color.accentColor : Color.secondary.opacity(0.25))
        )
    }

    private func effectName(_ id: Int) -> String {
        effects.first(where: { $0.id == id })?.name ?? "Effect \(id)"
    }
}

extension RGB {
    var swiftUIColor: Color {
        let (r, g, b) = unitComponents
        return Color(red: r, green: g, blue: b)
    }
}

extension Color {
    /// `#RRGGBB`, the form the device expects.
    var hexString: String {
        #if canImport(UIKit)
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(self).getRed(&r, green: &g, blue: &b, alpha: &a)
        return String(
            format: "#%02X%02X%02X",
            Int((r * 255).rounded()),
            Int((g * 255).rounded()),
            Int((b * 255).rounded())
        )
        #else
        return "#000000"
        #endif
    }
}
