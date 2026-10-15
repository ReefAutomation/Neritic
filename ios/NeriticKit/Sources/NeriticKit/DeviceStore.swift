import Foundation

/// Remembers which controller the app is talking to.
///
/// The host is stored as a plain string (either an address the user typed or a
/// Bonjour name discovered on the network) — the device has no account or
/// pairing concept to store.
public struct DeviceStore: Sendable {
    // UserDefaults is documented as thread-safe, but the SDK does not declare
    // the conformance, so this is opted out of strict checking explicitly.
    private nonisolated(unsafe) let defaults: UserDefaults
    private static let hostKey = "neritic.device.host"

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    /// The last host the app successfully used, if any.
    public var savedHost: String? {
        get {
            let value = defaults.string(forKey: Self.hostKey)
            guard let value, !value.isEmpty else { return nil }
            return value
        }
        nonmutating set {
            if let newValue, !newValue.isEmpty {
                defaults.set(newValue, forKey: Self.hostKey)
            } else {
                defaults.removeObject(forKey: Self.hostKey)
            }
        }
    }

    public func client(for host: String) -> NeriticClient? {
        NeriticClient(host: host)
    }

    public func forget() {
        savedHost = nil
    }
}
