import SwiftUI
import NeriticKit

@main
struct NeriticApp: App {
    @State private var model = AppModel()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView(model: model)
        }
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active:
                model.applicationWillEnterForeground()
            case .background:
                // Only on a real background transition: `.inactive` fires for
                // transient interruptions (Control Centre, a call banner) and
                // tearing the socket down for those would thrash the link.
                model.applicationDidEnterBackground()
            default:
                break
            }
        }
    }
}

struct RootView: View {
    let model: AppModel
    @State private var discovery = DeviceDiscovery()

    var body: some View {
        Group {
            if model.isConfigured {
                ConnectedView(model: model)
            } else {
                ConnectionView(model: model, discovery: discovery)
            }
        }
        .task { discovery.start() }
        .alert(
            "Something went wrong",
            isPresented: Binding(
                get: { model.lastError != nil },
                set: { if !$0 { model.clearError() } }
            ),
            actions: {
                Button("OK", role: .cancel) { model.clearError() }
            },
            message: {
                Text(model.lastError ?? "")
            }
        )
    }
}

// MARK: - Connection

struct ConnectionView: View {
    let model: AppModel
    let discovery: DeviceDiscovery

    @State private var manualHost = ""
    @State private var showManualEntry = false

    var body: some View {
        NavigationStack {
            List {
                if discovery.devices.isEmpty {
                    Section {
                        ContentUnavailableView {
                            Label("Searching", systemImage: "wifi")
                        } description: {
                            Text(
                                "Looking for controllers advertising themselves on this network. "
                                + "You can also enter an address directly."
                            )
                        }
                    }
                    .listRowBackground(Color.clear)
                } else {
                    Section("Found on this network") {
                        ForEach(discovery.devices) { device in
                            Button {
                                connect(device)
                            } label: {
                                DeviceRow(device: device)
                            }
                            .disabled(device.connectTarget == nil)
                        }
                    }
                }

                if let error = discovery.lastError {
                    Section {
                        Text("Automatic discovery is unavailable: \(error)")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }

                Section {
                    if showManualEntry {
                        TextField("192.168.1.42 or neritic.local", text: $manualHost)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .keyboardType(.URL)
                            .onSubmit(connectManually)
                        Button("Connect", action: connectManually)
                            .disabled(manualHost.trimmingCharacters(in: .whitespaces).isEmpty)
                    } else {
                        Button("Enter an address instead") {
                            showManualEntry = true
                        }
                    }
                } header: {
                    Text("Manual connection")
                } footer: {
                    Text(
                        "If the controller is in setup mode it creates a Wi-Fi network named "
                        + "after its hostname (Neritic by default) and answers on 192.168.4.1."
                    )
                }
            }
            .navigationTitle("Neritic")
            .refreshable { discovery.stop(); discovery.start() }
        }
    }

    private func connectManually() {
        model.connect(to: manualHost)
    }

    private func connect(_ device: DiscoveredDevice) {
        guard let target = device.connectTarget else { return }
        model.connect(to: target)
    }
}

private struct DeviceRow: View {
    let device: DiscoveredDevice

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(device.name)
                .font(.headline)
            HStack(spacing: 8) {
                // The hostname is the stable identifier; the address is shown
                // only as extra information while it is still known.
                Text(device.displayHost)
                if let address = device.displayAddress {
                    Text(address)
                }
                if let version = device.version {
                    Text(version)
                }
                if let environment = device.environment {
                    Text(environment)
                }
            }
            .font(.caption)
            .foregroundStyle(.secondary)
        }
    }
}

// MARK: - Connected shell

struct ConnectedView: View {
    let model: AppModel

    var body: some View {
        // Two tabs, mirroring the web UI (Lights = Home, Settings = Config).
        // The `Tab` builder is iOS 18+; the deployment target is iOS 17, so the
        // tabItem form is used deliberately.
        TabView {
            HomeView(model: model)
                .tabItem { Label("Lights", systemImage: "lightbulb") }
            SettingsView(model: model)
                .tabItem { Label("Settings", systemImage: "gearshape") }
        }
    }
}
