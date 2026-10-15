import Foundation
import Testing

@testable import NeriticKit

/// Stubs transport behaviour so the client's error mapping can be exercised
/// without a device.
///
/// The case is chosen from the request's **host**, which means the stub holds no
/// shared mutable state and the tests stay parallel-safe.
final class StubURLProtocol: URLProtocol {
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        guard let url = request.url else {
            client?.urlProtocol(self, didFailWithError: URLError(.badURL))
            return
        }

        switch url.host {
        case "reboot-ok.local":
            let body = Data(#"{"success":true,"message":"OK"}"#.utf8)
            let response = HTTPURLResponse(
                url: url,
                statusCode: 200,
                httpVersion: "HTTP/1.1",
                headerFields: ["Content-Type": "application/json"]
            )!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: body)
            client?.urlProtocolDidFinishLoading(self)

        case "reboot-lost.local":
            // What the device does when it restarts mid-response.
            client?.urlProtocol(self, didFailWithError: URLError(.networkConnectionLost))

        case "reboot-offline.local":
            client?.urlProtocol(self, didFailWithError: URLError(.cannotConnectToHost))

        default:
            client?.urlProtocol(self, didFailWithError: URLError(.badURL))
        }
    }

    override func stopLoading() {}
}

/// The controller answers a restart command and then restarts about 200 ms later,
/// so the response frequently never arrives in full. That dropped connection has
/// to be distinguishable from a controller that was never reachable, otherwise a
/// successful restart looks like a failure and a real failure looks like a
/// restart.
@Suite("Restart transport")
struct RestartTransportTests {

    private func makeClient(host: String) throws -> NeriticClient {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [StubURLProtocol.self]
        let url = try #require(URL(string: "http://\(host)"))
        return NeriticClient(baseURL: url, session: URLSession(configuration: configuration))
    }

    @Test("A normal reply is decoded")
    func acceptsSuccessReply() async throws {
        let response = try await makeClient(host: "reboot-ok.local").reboot()

        #expect(response.success == true)
        #expect(response.message == "OK")
    }

    @Test("A connection dropped mid-request is reported distinctly")
    func reportsConnectionLost() async throws {
        let client = try makeClient(host: "reboot-lost.local")

        await #expect(throws: NeriticError.connectionLost) {
            try await client.reboot()
        }
        #expect(NeriticError.connectionLost.isRetryable)
    }

    @Test("An unreachable controller is not mistaken for a dropped connection")
    func reportsUnreachable() async throws {
        let client = try makeClient(host: "reboot-offline.local")

        do {
            _ = try await client.reboot()
            Issue.record("Expected the call to fail")
        } catch let error as NeriticError {
            guard case .unreachable = error else {
                Issue.record("Expected .unreachable, got \(error)")
                return
            }
        }
    }
}
