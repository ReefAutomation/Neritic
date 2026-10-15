import SwiftUI
import NeriticKit

// MARK: - Firmware

struct FirmwareView: View {
    let model: AppModel

    @State private var isInstalling = false

    private var ota: OTAStatus? { model.otaStatus }

    var body: some View {
        Form {
            Section("Installed") {
                LabeledContent("Version", value: model.firmwareVersion ?? "—")
            }

            Section {
                Button("Check for updates") {
                    Task { await model.checkForUpdates() }
                }
                .disabled(model.isBusy)

                if let info = model.updateInfo {
                    if let error = info.error {
                        Text(error)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    } else {
                        LabeledContent("Latest release", value: latestVersion(info))
                    }
                }
            } header: {
                Text("Updates")
            } footer: {
                Text(
                    "The controller downloads the correct image for its own hardware and "
                    + "installs it itself. It restarts when the update completes."
                )
            }

            if let info = model.updateInfo, info.error == nil, !isInstalling, ota == nil {
                Section {
                    Button("Install update") {
                        isInstalling = true
                        Task { await model.startUpdate() }
                    }
                    .disabled(model.isBusy || !hasNewerVersion(info))
                } footer: {
                    if !hasNewerVersion(info) {
                        Text("The installed version is already the latest release.")
                    }
                }
            }

            if let ota {
                Section("Progress") {
                    OTAProgressView(status: ota)
                    if ota.isTerminal {
                        Button("Done") {
                            isInstalling = false
                            model.clearOtaStatus()
                        }
                    }
                }
            }
        }
        .navigationTitle("Firmware update")
    }

    private func latestVersion(_ info: UpdateInfo) -> String {
        // The device resolves the image for its own build environment during the
        // update, so the newest advertised release is what we can show here.
        info.latest?.compactMap(\.version).first ?? "—"
    }

    private func hasNewerVersion(_ info: UpdateInfo) -> Bool {
        guard let latest = info.latest?.compactMap(\.version).first else { return false }
        return latest != model.firmwareVersion
    }
}

private struct OTAProgressView: View {
    let status: OTAStatus

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            switch status.status {
            case "start":
                Label("Starting update", systemImage: "arrow.down.circle")
                ProgressView()
            case "progress":
                Label("Downloading and writing", systemImage: "arrow.down.circle")
                ProgressView(value: Double(status.progress ?? 0), total: 100)
                if let progress = status.progress {
                    Text("\(progress)%")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
            case "success":
                Label("Update installed", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                Text("The controller is restarting.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            case "error":
                Label("Update failed", systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange)
                if let message = status.message {
                    Text(message)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            default:
                Label(status.status, systemImage: "info.circle")
            }
        }
    }
}

// MARK: - Wi-Fi

struct WiFiSetupView: View {
    let model: AppModel

    @State private var networks: [String] = []
    @State private var selectedSSID: String?
    @State private var password = ""
    @State private var isScanning = false
    @State private var didScan = false
    @State private var confirming = false

    var body: some View {
        Form {
            Section("Connected network") {
                LabeledContent("Network", value: model.config?.network?.ssid?.isEmpty == false
                               ? (model.config?.network?.ssid ?? "—")
                               : "Not configured")
            }

            Section {
                Button {
                    Task { await scan() }
                } label: {
                    HStack {
                        Text("Scan for networks")
                        if isScanning { Spacer(); ProgressView() }
                    }
                }
                .disabled(isScanning)

                if didScan, networks.isEmpty, !isScanning {
                    Text("No networks found.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                ForEach(networks, id: \.self) { ssid in
                    Button {
                        selectedSSID = ssid
                    } label: {
                        HStack {
                            Text(ssid).foregroundStyle(.primary)
                            Spacer()
                            if selectedSSID == ssid {
                                Image(systemName: "checkmark").foregroundStyle(.tint)
                            }
                        }
                    }
                }
            } header: {
                Text("Available networks")
            } footer: {
                Text("The controller scans from where it is installed, so the list may differ from your phone's.")
            }

            if let ssid = selectedSSID, ssid != model.config?.network?.ssid {
                Section {
                    SecureField("Password", text: $password)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("Connect to \(ssid)") { confirming = true }
                        .disabled(password.isEmpty)
                } footer: {
                    Text("The controller saves these credentials and restarts to join the network.")
                }
            }
        }
        .navigationTitle("Wi-Fi")
        .confirmationDialog(
            "Restart the controller to join \(selectedSSID ?? "")?",
            isPresented: $confirming,
            titleVisibility: .visible
        ) {
            Button("Save and restart") {
                guard let ssid = selectedSSID else { return }
                Task { await model.setWiFi(ssid: ssid, password: password) }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(
                "If the credentials are wrong the controller will fall back to its own setup "
                + "access point, reachable at 192.168.4.1."
            )
        }
    }

    private func scan() async {
        isScanning = true
        networks = await model.scanWiFiNetworks()
        isScanning = false
        didScan = true
    }
}
