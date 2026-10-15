import SwiftUI
import NeriticKit

struct SettingsView: View {
    let model: AppModel

    var body: some View {
        NavigationStack {
            List {
                Section("Device") {
                    LabeledContent("Address", value: model.host ?? "—")
                    LabeledContent("Firmware", value: model.firmwareVersion ?? "—")
                    LabeledContent("Effect", value: model.effectName(for: model.deviceState.effect))
                }

                Section("Lighting") {
                    NavigationLink("Presets") { PresetsManagerView(model: model) }
                    NavigationLink("Strip and relay") { LEDSettingsView(model: model) }
                    NavigationLink("Safety limits") { SafetySettingsView(model: model) }
                    NavigationLink("Transitions") { TransitionSettingsView(model: model) }
                }

                Section("Scheduling") {
                    NavigationLink("Schedule") { ScheduleView(model: model) }
                    NavigationLink("Location and time") { LocationTimeSettingsView(model: model) }
                }

                Section("Network") {
                    NavigationLink("Wi-Fi") { WiFiSetupView(model: model) }
                }

                Section("Maintenance") {
                    NavigationLink("Firmware update") { FirmwareView(model: model) }
                    NavigationLink("Device actions") { DeviceActionsView(model: model) }
                }

                Section {
                    Button("Change controller") {
                        model.forgetDevice()
                    }
                } footer: {
                    Text("This forgets the saved address. The controller itself is unchanged.")
                }
            }
            .navigationTitle("Settings")
            .refreshable { await model.refreshAll() }
        }
    }
}

// MARK: - Strip and relay

struct LEDSettingsView: View {
    let model: AppModel

    @State private var pin = 0
    @State private var count = 0
    @State private var type = ""
    @State private var colorOrder = "GRB"
    @State private var relayPin = 0
    @State private var relayActiveHigh = true

    private let orders = ["RGB", "RBG", "GRB", "GBR", "BRG", "BGR", "RGBW", "GRBW"]

    var body: some View {
        Form {
            Section("Strip") {
                Stepper("Data pin: \(pin)", value: $pin, in: 0...48)
                Stepper("LED count: \(count)", value: $count, in: 1...512)
                TextField("Chipset", text: $type)
                    .textInputAutocapitalization(.characters)
                Picker("Colour order", selection: $colorOrder) {
                    ForEach(orders, id: \.self) { Text($0).tag($0) }
                }
            }

            Section {
                Stepper("Relay pin: \(relayPin)", value: $relayPin, in: 0...48)
                Toggle("Relay active high", isOn: $relayActiveHigh)
            } header: {
                Text("Relay")
            } footer: {
                Text("The relay cuts power to the strip. Active high means the relay switches on when the pin is driven high.")
            }

            Section {
                Button("Save") { save() }
                    .disabled(model.config?.led == nil)
            } footer: {
                Text("Changing the strip wiring or LED count takes effect after the device restarts.")
            }
        }
        .navigationTitle("Strip and relay")
        .onAppear(perform: load)
    }

    private func load() {
        guard let led = model.config?.led else { return }
        pin = led.pin ?? pin
        count = led.count ?? count
        type = led.type ?? type
        colorOrder = led.colorOrder ?? colorOrder
        relayPin = led.relayPin ?? relayPin
        relayActiveHigh = led.relayActiveHigh ?? relayActiveHigh
    }

    private func save() {
        Task {
            await model.updateConfig(ConfigUpdate(led: LEDConfig(
                pin: pin,
                count: count,
                type: type,
                colorOrder: colorOrder,
                relayPin: relayPin,
                relayActiveHigh: relayActiveHigh
            )))
        }
    }
}

// MARK: - Safety

struct SafetySettingsView: View {
    let model: AppModel

    @State private var maxBrightness = 100.0
    @State private var minTransitionTime = 0.0

    var body: some View {
        Form {
            Section {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("Maximum brightness")
                        Spacer()
                        Text("\(Int(maxBrightness.rounded()))%")
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                    Slider(value: $maxBrightness, in: 1...100)
                }
            } footer: {
                Text("A ceiling the device enforces on every brightness request, including presets and schedules.")
            }

            Section {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("Minimum transition")
                        Spacer()
                        Text(DurationFormatting.shortDuration(seconds: Int(minTransitionTime.rounded())))
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                    Slider(value: $minTransitionTime, in: 0...30)
                }
            } footer: {
                Text("Fast transitions stress the LEDs and power supply; this is the shortest fade the device will allow.")
            }

            Section {
                Button("Save") { save() }
            }
        }
        .navigationTitle("Safety limits")
        .onAppear(perform: load)
    }

    private func load() {
        guard let safety = model.config?.safety else { return }
        if let value = safety.maxBrightness { maxBrightness = Double(value) }
        if let value = safety.minTransitionTime { minTransitionTime = Double(value / 1000) }
    }

    private func save() {
        Task {
            await model.updateConfig(ConfigUpdate(safety: SafetyConfig(
                maxBrightness: Int(maxBrightness.rounded()),
                minTransitionTime: Int(minTransitionTime.rounded()) * 1000
            )))
        }
    }
}

// MARK: - Transitions

struct TransitionSettingsView: View {
    let model: AppModel

    @State private var powerOn = 0.0
    @State private var schedule = 0.0
    @State private var manual = 0.0
    @State private var effect = 0.0

    var body: some View {
        Form {
            Section {
                durationSlider("Power on", value: $powerOn)
                durationSlider("Scheduled changes", value: $schedule)
                durationSlider("Manual changes", value: $manual)
                durationSlider("Effect changes", value: $effect)
            } footer: {
                Text("How long the device fades between states, in each situation.")
            }

            Section {
                Button("Save") { save() }
            }
        }
        .navigationTitle("Transitions")
        .onAppear(perform: load)
    }

    private func durationSlider(_ title: String, value: Binding<Double>) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(DurationFormatting.shortDuration(seconds: Int(value.wrappedValue.rounded())))
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            Slider(value: value, in: 0...3600, step: 5)
        }
    }

    private func load() {
        guard let times = model.config?.transitionTimes else { return }
        powerOn = Double(times.powerOn ?? 0)
        schedule = Double(times.schedule ?? 0)
        manual = Double(times.manual ?? 0)
        effect = Double(times.effect ?? 0)
    }

    private func save() {
        Task {
            await model.updateConfig(ConfigUpdate(transitionTimes: TransitionTimesConfig(
                powerOn: Int(powerOn.rounded()),
                schedule: Int(schedule.rounded()),
                manual: Int(manual.rounded()),
                effect: Int(effect.rounded())
            )))
        }
    }
}

// MARK: - Location and time

struct LocationTimeSettingsView: View {
    let model: AppModel

    @State private var timezone = ""
    @State private var ntpServer = ""
    @State private var latitude = ""
    @State private var longitude = ""

    var body: some View {
        Form {
            Section {
                Picker("Time zone", selection: $timezone) {
                    ForEach(model.timezones) { option in
                        Text(option.label).tag(option.name)
                    }
                }
            } footer: {
                Text(
                    "Daylight saving is applied automatically from the zone's rules. "
                    + "Offsets appear once the device has synchronised its clock."
                )
            }

            Section("Clock source") {
                TextField("NTP server", text: $ntpServer)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
            }

            Section {
                TextField("Latitude", text: $latitude)
                    .keyboardType(.numbersAndPunctuation)
                TextField("Longitude", text: $longitude)
                    .keyboardType(.numbersAndPunctuation)
            } header: {
                Text("Coordinates")
            } footer: {
                Text("Used to calculate sunrise and sunset for scheduled changes.")
            }

            Section {
                Button("Save", action: save)
            }
        }
        .navigationTitle("Location and time")
        .onAppear(perform: load)
    }

    private func load() {
        guard let time = model.config?.time else { return }
        timezone = time.timezone ?? timezone
        ntpServer = time.ntpServer ?? ntpServer
        latitude = time.latitude.map { String($0) } ?? ""
        longitude = time.longitude.map { String($0) } ?? ""
        if timezone.isEmpty, let first = model.timezones.first {
            timezone = first.name
        }
    }

    private func save() {
        Task {
            await model.updateConfig(ConfigUpdate(time: TimeConfig(
                ntpServer: ntpServer.isEmpty ? nil : ntpServer,
                timezone: timezone.isEmpty ? nil : timezone,
                latitude: Double(latitude),
                longitude: Double(longitude)
            )))
        }
    }
}

// MARK: - Device actions

struct DeviceActionsView: View {
    let model: AppModel

    @State private var confirmingRestart = false
    @State private var confirmingReset = false
    @State private var restartOutcome: RestartOutcome = .idle

    private enum RestartOutcome: Equatable {
        case idle
        case sending
        case accepted
        case refused(String)
    }

    var body: some View {
        List {
            Section {
                Button("Restart controller") { confirmingRestart = true }
                    .disabled(restartOutcome == .sending)

                switch restartOutcome {
                case .idle:
                    EmptyView()
                case .sending:
                    HStack(spacing: 10) {
                        ProgressView()
                        Text("Sending restart…")
                    }
                case .accepted:
                    Label(
                        "Restart sent. The controller will be back in a few seconds.",
                        systemImage: "checkmark.circle.fill"
                    )
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                case let .refused(message):
                    Label(message, systemImage: "exclamationmark.triangle.fill")
                        .font(.footnote)
                        .foregroundStyle(.orange)
                }

                // Shows the controller dropping and returning, which is the only
                // visible sign that a restart actually happened.
                ConnectionRow(state: model.connectionState, host: model.host)
            } header: {
                Text("Restart")
            } footer: {
                Text("The lights turn off for a few seconds. The status above follows the controller going away and coming back.")
            }

            Section {
                Button("Erase settings and restart", role: .destructive) {
                    confirmingReset = true
                }
            } footer: {
                Text("Restores factory defaults, including Wi-Fi credentials and presets. You will need to set the controller up again.")
            }
        }
        .navigationTitle("Device actions")
        .confirmationDialog(
            "Restart the controller?",
            isPresented: $confirmingRestart,
            titleVisibility: .visible
        ) {
            Button("Restart") {
                Task { await restart() }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The lights turn off for a few seconds while it restarts.")
        }
        .confirmationDialog(
            "Erase all settings?",
            isPresented: $confirmingReset,
            titleVisibility: .visible
        ) {
            Button("Erase everything", role: .destructive) {
                Task { await model.factoryReset() }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("This cannot be undone and the controller will leave your network.")
        }
    }

    private func restart() async {
        restartOutcome = .sending
        let accepted = await model.reboot()
        restartOutcome = accepted
            ? .accepted
            : .refused(model.lastError ?? "Could not restart the controller.")
    }
}
