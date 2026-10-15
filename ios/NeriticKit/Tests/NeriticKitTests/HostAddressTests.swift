import Foundation
import Testing

@testable import NeriticKit

/// Regression coverage for a real discovery failure: Network.framework resolves
/// a Bonjour service to an interface-scoped address such as
/// `192.168.0.61%en0`, which is not a valid URL host, so connecting failed with
/// "That does not look like a valid address."
@Suite("Host address normalisation")
struct HostAddressTests {
    @Test("The interface scope is dropped from an IPv4 address")
    func stripsScopeFromIPv4() {
        #expect(HostAddress.normalized("192.168.0.61%en0") == "192.168.0.61")
        #expect(HostAddress.normalized("192.168.4.1%en1") == "192.168.4.1")
    }

    @Test("A plain IPv4 address is unchanged")
    func keepsPlainIPv4() {
        #expect(HostAddress.normalized("192.168.0.61") == "192.168.0.61")
    }

    @Test("A Bonjour name is unchanged")
    func keepsName() {
        #expect(HostAddress.normalized("Neritic.local") == "Neritic.local")
        #expect(HostAddress.normalized("neritic.local%en0") == "neritic.local")
    }

    @Test("IPv6 addresses are bracketed, and the scope moves inside as %25")
    func bracketsIPv6() {
        #expect(HostAddress.normalized("fe80::1") == "[fe80::1]")
        #expect(HostAddress.normalized("fe80::1%en0") == "[fe80::1%25en0]")
    }

    @Test("Whitespace and empty input are handled")
    func handlesEdgeCases() {
        #expect(HostAddress.normalized("  192.168.0.61%en0  ") == "192.168.0.61")
        #expect(HostAddress.normalized("") == "")
    }

    @Test("A normalised address produces a usable URL")
    func producesValidURL() throws {
        let client = try #require(NeriticClient(host: HostAddress.normalized("192.168.0.61%en0")))
        #expect(client.baseURL.absoluteString == "http://192.168.0.61")

        let mapped = try #require(NeriticClient(host: HostAddress.normalized("neritic.local%en0")))
        #expect(mapped.baseURL.absoluteString == "http://neritic.local")
    }

    @Test("An un-normalised scoped address is rejected by the client")
    func unnormalisedAddressIsRejected() {
        // Documents why normalisation is required upstream rather than optional.
        #expect(NeriticClient(host: "192.168.0.61%en0") == nil)
    }

    @Test("Display form is human-readable")
    func displayForm() {
        #expect(HostAddress.displayString("192.168.0.61%en0") == "192.168.0.61")
        #expect(HostAddress.displayString("fe80::1%en0") == "fe80::1%en0")
    }

    @Test("A discovered device is addressed by hostname, not the resolved IP")
    func discoveredDeviceUsesHostname() {
        let unresolved = DiscoveredDevice(name: "Neritic", host: nil, port: nil)
        #expect(unresolved.connectTarget == "Neritic.local")
        #expect(unresolved.displayHost == "Neritic.local")
        #expect(unresolved.displayAddress == nil)

        // Once the address resolves it is kept for information only: connecting
        // by hostname survives a DHCP lease change, a stored IP does not.
        let resolved = DiscoveredDevice(name: "Neritic", host: "192.168.0.61", port: 80)
        #expect(resolved.connectTarget == "Neritic.local")
        #expect(resolved.displayAddress == "192.168.0.61")

        // Port 80 is the default and is left out of both forms.
        #expect(resolved.displayHost == "Neritic.local")

        let nonStandardPort = DiscoveredDevice(name: "Neritic", host: "192.168.0.61", port: 8080)
        #expect(nonStandardPort.connectTarget == "Neritic.local:8080")
    }

    @Test("A service name that cannot be a hostname falls back to the address")
    func discoveredDeviceFallsBackToAddress() {
        let spaced = DiscoveredDevice(name: "My Tank", host: "192.168.0.61", port: 80)
        #expect(spaced.connectTarget == "192.168.0.61")
        // Nothing to add: the name was not usable as a hostname.
        #expect(spaced.displayAddress == nil)

        // Neither a usable name nor a resolved address: nothing to connect to.
        let unknown = DiscoveredDevice(name: "My Tank", host: nil, port: nil)
        #expect(unknown.connectTarget == nil)
        #expect(unknown.displayHost == "Resolving…")
    }

    @Test("Bonjour instance names only become hostnames when they are valid labels")
    func bonjourNameValidation() {
        #expect(HostAddress.bonjourName(instance: "Neritic") == "Neritic.local")
        #expect(HostAddress.bonjourName(instance: "reef-tank-2") == "reef-tank-2.local")
        #expect(HostAddress.bonjourName(instance: "  Neritic  ") == "Neritic.local")

        // Bonjour instance names are free-form, so these must not be turned into
        // hostnames that could never resolve.
        #expect(HostAddress.bonjourName(instance: "My Tank") == nil)
        #expect(HostAddress.bonjourName(instance: "Tank_1") == nil)
        #expect(HostAddress.bonjourName(instance: "café") == nil)
        #expect(HostAddress.bonjourName(instance: "-tank") == nil)
        #expect(HostAddress.bonjourName(instance: "tank-") == nil)
        #expect(HostAddress.bonjourName(instance: "") == nil)
        #expect(HostAddress.bonjourName(instance: String(repeating: "a", count: 64)) == nil)
    }
}
