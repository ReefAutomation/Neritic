import Foundation
import Network
import Observation

/// A controller found on the local network.
public struct DiscoveredDevice: Identifiable, Equatable, Sendable {
    /// Bonjour service instance name, e.g. "Neritic".
    public var name: String
    /// Resolved host once known, e.g. "neritic.local" or "192.168.1.42".
    public var host: String?
    public var port: Int?
    public var version: String?
    public var model: String?
    public var environment: String?

    public var id: String { name }

    /// Value to hand to ``NeriticClient(host:)``, or nil while the device cannot
    /// be addressed yet.
    ///
    /// The Bonjour name is preferred over the resolved address. The firmware
    /// advertises its hostname as the service name (`startMdns` in
    /// src/network.cpp sets `mdns_hostname_set` and `mdns_instance_name_set` to
    /// the same value), so `<name>.local` keeps working after a DHCP lease
    /// change, where a stored IP would quietly go stale. It also makes the device
    /// connectable the moment it is discovered, without waiting for the address
    /// to resolve.
    public var connectTarget: String? {
        guard let base = HostAddress.bonjourName(instance: name) ?? resolvedHost else {
            return nil
        }
        guard let port, port != 80 else { return base }
        return "\(base):\(port)"
    }

    /// Resolved address, normalised into a usable URL host.
    public var resolvedHost: String? {
        guard let host, !host.isEmpty else { return nil }
        return HostAddress.normalized(host)
    }

    /// What the user sees in the discovery list.
    public var displayHost: String {
        connectTarget ?? "Resolving…"
    }

    /// The device's address, shown alongside the hostname when the two differ.
    public var displayAddress: String? {
        guard HostAddress.bonjourName(instance: name) != nil, let resolvedHost else {
            return nil
        }
        return HostAddress.displayString(resolvedHost)
    }
}

/// Browses for the `_neritic._tcp` service advertised by the firmware.
///
/// The advertisement is additive: the firmware already set a DHCP hostname, but
/// nothing answered mDNS until the `_neritic._tcp` service was added in
/// `src/network.cpp`, so this browse only succeeds against firmware that
/// includes it. Manual host entry remains a first-class way to connect.
@MainActor
@Observable
public final class DeviceDiscovery {

    public private(set) var devices: [DiscoveredDevice] = []
    public private(set) var isBrowsing = false
    public private(set) var lastError: String?

    private var browser: NWBrowser?
    /// Resolution connections, keyed by service name. Kept alive until the host
    /// resolves (or the browse restarts).
    private var resolvers: [String: NWConnection] = [:]

    private static let serviceType = "_neritic._tcp"

    public init() {}

    public func start() {
        guard browser == nil else { return }

        let parameters = NWParameters.tcp
        parameters.includePeerToPeer = true

        let browser = NWBrowser(
            for: .bonjour(type: Self.serviceType, domain: "local."),
            using: parameters
        )

        browser.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                guard let self else { return }
                switch state {
                case .ready:
                    self.isBrowsing = true
                    self.lastError = nil
                case let .failed(error):
                    self.isBrowsing = false
                    self.lastError = error.localizedDescription
                case .cancelled:
                    self.isBrowsing = false
                default:
                    break
                }
            }
        }

        browser.browseResultsChangedHandler = { [weak self] results, _ in
            Task { @MainActor in
                self?.apply(results)
            }
        }

        browser.start(queue: .main)
        self.browser = browser
        isBrowsing = true
    }

    public func stop() {
        browser?.cancel()
        browser = nil
        for resolver in resolvers.values {
            resolver.cancel()
        }
        resolvers.removeAll()
        isBrowsing = false
    }

    // MARK: - Results

    private func apply(_ results: Set<NWBrowser.Result>) {
        var found: [DiscoveredDevice] = []

        for result in results {
            guard case let .service(name, _, _, _) = result.endpoint else { continue }

            var device = DiscoveredDevice(
                name: name,
                host: devices.first(where: { $0.name == name })?.host,
                port: devices.first(where: { $0.name == name })?.port
            )

            if case let .bonjour(txt) = result.metadata {
                device.version = txt["version"]
                device.model = txt["model"]
                device.environment = txt["env"]
            }

            found.append(device)
            if device.host == nil {
                resolve(serviceName: name, endpoint: result.endpoint)
            }
        }

        devices = found.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    /// Opens a short-lived connection purely to learn the service's address.
    private func resolve(serviceName: String, endpoint: NWEndpoint) {
        guard resolvers[serviceName] == nil else { return }

        let connection = NWConnection(to: endpoint, using: .tcp)
        resolvers[serviceName] = connection

        connection.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                guard let self else { return }
                switch state {
                case .ready:
                    if case let .hostPort(host, port) = connection.currentPath?.remoteEndpoint {
                        // The endpoint arrives interface-scoped ("…%en0"); that
                        // scope is not valid in a URL, so it is normalised here.
                        self.applyResolvedHost(
                            HostAddress.normalized(String(describing: host)),
                            port: Int(port.rawValue),
                            for: serviceName
                        )
                    }
                    self.finishResolving(serviceName)
                case .failed, .cancelled:
                    self.finishResolving(serviceName)
                default:
                    break
                }
            }
        }

        connection.start(queue: .main)
    }

    private func applyResolvedHost(_ host: String, port: Int?, for serviceName: String) {
        guard let index = devices.firstIndex(where: { $0.name == serviceName }) else { return }
        devices[index].host = host
        devices[index].port = port
    }

    private func finishResolving(_ serviceName: String) {
        resolvers[serviceName]?.cancel()
        resolvers[serviceName] = nil
    }
}
