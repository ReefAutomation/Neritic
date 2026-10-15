import SwiftUI
import NeriticKit

struct PresetsManagerView: View {
    let model: AppModel
    @State private var editing: Preset?

    var body: some View {
        List {
            Section {
                ForEach(model.presets) { preset in
                    Button {
                        editing = preset
                    } label: {
                        PresetRow(
                            preset: preset,
                            effectName: model.effectName(for: preset.effect),
                            isActive: model.deviceState.preset == preset.id
                        )
                    }
                }
            } footer: {
                Text(
                    "A preset pairs an effect with its own speed, intensity, brightness and colours. "
                    + "Tap one to edit it."
                )
            }
        }
        .navigationTitle("Presets")
        .sheet(item: $editing) { preset in
            PresetEditor(model: model, preset: preset)
        }
        .refreshable { await model.refreshAll() }
    }
}

private struct PresetRow: View {
    let preset: Preset
    let effectName: String
    let isActive: Bool

    var body: some View {
        HStack(spacing: 12) {
            SwatchStrip(colors: preset.params.colors ?? [])
                .frame(width: 44, height: 28)

            VStack(alignment: .leading, spacing: 2) {
                Text(preset.name.isEmpty ? "Preset \(preset.id)" : preset.name)
                    .foregroundStyle(.primary)
                Text(effectName)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            if isActive {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.tint)
            } else if !preset.enabled {
                Text("Off")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

/// Small preview of the colours a preset uses.
struct SwatchStrip: View {
    let colors: [String]

    var body: some View {
        Canvas { context, size in
            guard !colors.isEmpty else {
                context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.secondary.opacity(0.2)))
                return
            }
            let width = size.width / CGFloat(colors.count)
            for (index, hex) in colors.enumerated() {
                let rect = CGRect(x: CGFloat(index) * width, y: 0, width: width + 0.5, height: size.height)
                context.fill(Path(rect), with: .color(LEDColor.parse(hex: hex).swiftUIColor))
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 6, style: .continuous).strokeBorder(.quaternary)
        )
    }
}

// MARK: - Editor

struct PresetEditor: View {
    let model: AppModel
    let preset: Preset

    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var effect = 0
    @State private var enabled = true
    @State private var speed = 0.0
    @State private var intensity = 0.0
    @State private var colors: [String] = []

    var body: some View {
        NavigationStack {
            Form {
                Section("Name") {
                    TextField("Preset name", text: $name)
                }

                Section("Effect") {
                    Picker("Effect", selection: $effect) {
                        ForEach(model.effects) { item in
                            Text(item.name).tag(item.id)
                        }
                    }
                    Toggle("Enabled", isOn: $enabled)
                }

                Section {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text("Speed")
                            Spacer()
                            Text("\(Int(speed.rounded()))%")
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
                        Slider(value: $speed, in: 0...100)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text("Intensity")
                            Spacer()
                            Text("\(Int(intensity.rounded()))%")
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
                        Slider(value: $intensity, in: 0...100)
                    }
                } header: {
                    Text("Levels (percent)")
                }

                Section("Colours") {
                    ForEach(Array(colors.enumerated()), id: \.offset) { index, hex in
                        ColorPicker(
                            "Colour \(index + 1)",
                            selection: Binding(
                                get: { LEDColor.parse(hex: hex).swiftUIColor },
                                set: { newValue in
                                    guard colors.indices.contains(index) else { return }
                                    colors[index] = newValue.hexString
                                }
                            ),
                            supportsOpacity: false
                        )
                    }
                    Button("Add colour") { colors.append("#000000") }
                        .disabled(colors.count >= 8)
                    if !colors.isEmpty {
                        Button("Remove last", role: .destructive) { colors.removeLast() }
                    }
                }
            }
            .navigationTitle(preset.name.isEmpty ? "Preset \(preset.id)" : preset.name)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save", action: save)
                }
            }
            .onAppear(perform: load)
        }
    }

    private func load() {
        name = preset.name
        effect = preset.effect
        enabled = preset.enabled
        speed = Double(preset.params.speed ?? 100)
        intensity = Double(preset.params.intensity ?? 50)
        colors = preset.params.colors ?? []
    }

    private func save() {
        let write = PresetWrite(
            id: preset.id,
            name: name,
            effect: effect,
            enabled: enabled,
            params: PresetParams(
                speed: Int(speed.rounded()),
                intensity: Int(intensity.rounded()),
                colors: colors
            )
        )
        Task {
            await model.savePreset(write)
            dismiss()
        }
    }
}
