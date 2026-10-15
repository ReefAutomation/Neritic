import SwiftUI
import NeriticKit

struct ScheduleView: View {
    let model: AppModel
    @State private var editing: TimerConfig?

    var body: some View {
        List {
            if model.sortedTimers.isEmpty {
                Section {
                    Text("No scheduled changes yet.")
                        .foregroundStyle(.secondary)
                }
            } else {
                Section {
                    ForEach(model.sortedTimers) { timer in
                        Button {
                            editing = timer
                        } label: {
                            TimerRow(timer: timer, model: model)
                        }
                    }
                    .onDelete(perform: delete)
                } footer: {
                    Text("Swipe a row to delete it. Sunrise and sunset entries track the calculated times for your coordinates.")
                }
            }

            Section {
                Button("Add a schedule") {
                    editing = TimerConfig(
                        enabled: true,
                        type: TimerKind.regular.rawValue,
                        hour: 8,
                        minute: 0,
                        presetId: model.presets.first?.id ?? 0,
                        brightness: 60
                    )
                }
            }
        }
        .navigationTitle("Schedule")
        .sheet(item: $editing) { timer in
            TimerEditor(model: model, timer: timer, isNew: timer.id == nil)
        }
    }

    private func delete(at offsets: IndexSet) {
        var timers = model.sortedTimers
        timers.remove(atOffsets: offsets)
        Task { await model.saveTimers(timers) }
    }
}

private struct TimerRow: View {
    let timer: TimerConfig
    let model: AppModel

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(timeLabel)
                    .font(.headline)
                    .monospacedDigit()
                Text("\(kindLabel) · \(presetLabel) · \(timer.brightness)%")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if !timer.enabled {
                Text("Off")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var timeLabel: String {
        guard let kind = TimerKind(rawValue: timer.type) else { return "—" }
        switch kind {
        case .sunrise, .sunset:
            return kind.label
        case .regular:
            return String(format: "%02d:%02d", timer.hour, timer.minute)
        }
    }

    private var kindLabel: String {
        TimerKind(rawValue: timer.type)?.label ?? "Clock"
    }

    private var presetLabel: String {
        let name = model.presets.first(where: { $0.id == timer.presetId })?.name
        return name ?? "Preset \(timer.presetId)"
    }
}

private struct TimerEditor: View {
    let model: AppModel
    let timer: TimerConfig
    let isNew: Bool

    @Environment(\.dismiss) private var dismiss
    @State private var enabled = true
    @State private var type = TimerKind.regular.rawValue
    @State private var time = Date()
    @State private var presetId = 0
    @State private var brightness = 60.0

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Toggle("Enabled", isOn: $enabled)
                    Picker("Trigger", selection: $type) {
                        ForEach(TimerKind.allCases, id: \.rawValue) { kind in
                            Text(kind.label).tag(kind.rawValue)
                        }
                    }
                }

                if type == TimerKind.regular.rawValue {
                    Section("Time") {
                        DatePicker("At", selection: $time, displayedComponents: .hourAndMinute)
                    }
                }

                Section("Preset") {
                    Picker("Apply", selection: $presetId) {
                        ForEach(model.presets) { preset in
                            Text(preset.name.isEmpty ? "Preset \(preset.id)" : preset.name)
                                .tag(preset.id)
                        }
                    }
                }

                Section {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text("Brightness")
                            Spacer()
                            Text("\(Int(brightness.rounded()))%")
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
                        Slider(value: $brightness, in: 0...100)
                    }
                }
            }
            .navigationTitle(isNew ? "New schedule" : "Edit schedule")
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
        enabled = timer.enabled
        type = timer.type
        presetId = timer.presetId
        brightness = Double(timer.brightness)
        var components = DateComponents()
        components.hour = timer.hour
        components.minute = timer.minute
        time = Calendar.current.date(from: components) ?? Date()
    }

    private func save() {
        let components = Calendar.current.dateComponents([.hour, .minute], from: time)

        var updated = timer
        updated.enabled = enabled
        updated.type = type
        updated.hour = components.hour ?? 0
        updated.minute = components.minute ?? 0
        updated.presetId = presetId
        updated.brightness = Int(brightness.rounded())

        // The schedule is written as a whole array via POST /api/config: the
        // per-timer endpoint needs an id, which a newly added timer does not
        // have until the device has stored it.
        var timers = model.sortedTimers
        if let index = timers.firstIndex(where: { $0.stableID == timer.stableID }) {
            updated.id = timers[index].id
            timers[index] = updated
        } else {
            updated.id = nil
            timers.append(updated)
        }

        Task {
            await model.saveTimers(timers)
            dismiss()
        }
    }
}
