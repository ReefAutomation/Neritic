import Foundation

public enum HostAddress {
    /// Turns a resolved endpoint description into a host usable in a URL.
    ///
    /// Network.framework renders an interface-scoped address as
    /// `192.168.0.61%en0` when a Bonjour service resolves. The scope is a
    /// link-layer detail, not part of the address, and a URL containing it is
    /// invalid. For IPv4 and named hosts the scope is dropped; for IPv6 it is
    /// kept but moved inside the brackets as `%25`, which is how a scope is
    /// written in a URL.
    ///
    /// Examples:
    /// * `192.168.0.61%en0` → `192.168.0.61`
    /// * `neritic.local`    → `neritic.local`
    /// * `fe80::1%en0`      → `[fe80::1%25en0]`
    /// * `fe80::1`          → `[fe80::1]`
    public static func normalized(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return trimmed }

        let parts = trimmed.split(
            separator: "%",
            maxSplits: 1,
            omittingEmptySubsequences: false
        )
        let address = String(parts[0])
        let scope = parts.count > 1 ? String(parts[1]) : ""

        guard address.contains(":") else { return address }

        return scope.isEmpty ? "[\(address)]" : "[\(address)%25\(scope)]"
    }

    /// Builds the mDNS hostname for a Bonjour service instance, or nil when the
    /// instance name cannot serve as a DNS label.
    ///
    /// Bonjour instance names are free-form UTF-8, so they are not necessarily
    /// valid hostnames. Only ASCII letters, digits and interior hyphens are
    /// accepted; anything else returns nil so the caller falls through to the
    /// resolved address instead of trying to resolve a name that cannot work.
    public static func bonjourName(instance: String) -> String? {
        let trimmed = instance.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed.count <= 63 else { return nil }

        let allowed = CharacterSet(
            charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"
        )
        guard trimmed.unicodeScalars.allSatisfy(allowed.contains),
              !trimmed.hasPrefix("-"),
              !trimmed.hasSuffix("-")
        else { return nil }

        return "\(trimmed).local"
    }

    /// The address as a person would recognise it, without URL bracketing but
    /// keeping the scope when it is meaningful (IPv6 link-local).
    public static func displayString(_ raw: String) -> String {
        let normalized = normalized(raw)
        if normalized.hasPrefix("[") && normalized.hasSuffix("]") {
            return String(normalized.dropFirst().dropLast())
                .replacingOccurrences(of: "%25", with: "%")
        }
        return normalized
    }
}
