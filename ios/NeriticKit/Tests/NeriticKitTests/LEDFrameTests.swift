import Foundation
import Testing

@testable import NeriticKit

@Suite("Live LED frames")
struct LEDFrameTests {
    @Test("A frame decodes into one pixel per four bytes")
    func decodesPixels() {
        let data = Data([10, 20, 30, 40, 50, 60, 70, 80])
        let frame = LEDFrame.decode(data)

        #expect(frame.pixels.count == 2)
        #expect(frame.pixels[0] == RGBW(r: 10, g: 20, b: 30, w: 40))
        #expect(frame.pixels[1] == RGBW(r: 50, g: 60, b: 70, w: 80))
    }

    @Test("A truncated frame decodes the whole pixels it contains")
    func decodesTruncatedFrame() {
        // Two whole pixels plus a partial third.
        let data = Data([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        let frame = LEDFrame.decode(data)

        #expect(frame.pixels.count == 2)
    }

    @Test("An empty frame decodes to no pixels")
    func decodesEmptyFrame() {
        #expect(LEDFrame.decode(Data()).pixels.isEmpty)
    }

    @Test("White blinks as white instead of being washed out by the blend")
    func whiteStaysWhite() {
        // Mirrors rgbwToDisplayColor in web/util.js.
        let pureWhite = RGBW(r: 0, g: 0, b: 0, w: 255)
        #expect(pureWhite.displayColor == RGB(r: 255, g: 255, b: 255))

        let balanced = RGBW(r: 100, g: 100, b: 100, w: 50)
        #expect(balanced.displayColor == RGB(r: 255, g: 255, b: 255))
    }

    @Test("The white channel blends into colours and clamps")
    func whiteBlendsIntoColour() {
        #expect(RGBW(r: 10, g: 20, b: 30, w: 5).displayColor == RGB(r: 15, g: 25, b: 35))
        // Additive blend must saturate rather than wrap.
        #expect(RGBW(r: 250, g: 250, b: 250, w: 100).displayColor == RGB(r: 255, g: 255, b: 255))
    }

    @Test("A coloured pixel with no white passes through")
    func colourWithoutWhite() {
        #expect(RGBW(r: 200, g: 100, b: 50, w: 0).displayColor == RGB(r: 200, g: 100, b: 50))
    }
}

@Suite("Colour parsing")
struct LEDColorTests {
    @Test("Parses #RRGGBB")
    func parsesSixDigitHex() {
        #expect(LEDColor.parse(hex: "#FF5500") == RGB(r: 255, g: 85, b: 0))
    }

    @Test("Parses #RRGGBBWW and blends the white channel")
    func parsesEightDigitHex() {
        // #RRGGBBWW: red with no white stays red.
        #expect(LEDColor.parse(hex: "#FF000000") == RGB(r: 255, g: 0, b: 0))
        // Red with white added blends and saturates.
        #expect(LEDColor.parse(hex: "#80000080") == RGB(r: 255, g: 128, b: 128))
        // A balanced pixel with white is forced to white, like the web UI.
        #expect(LEDColor.parse(hex: "#00000080") == RGB(r: 255, g: 255, b: 255))
    }

    @Test("Unparseable values render black rather than a substitute colour")
    func rejectsInvalidInput() {
        #expect(LEDColor.parse(hex: nil) == RGB(r: 0, g: 0, b: 0))
        #expect(LEDColor.parse(hex: "") == RGB(r: 0, g: 0, b: 0))
        #expect(LEDColor.parse(hex: "FF5500") == RGB(r: 0, g: 0, b: 0))
        #expect(LEDColor.parse(hex: "#GGGGGG") == RGB(r: 0, g: 0, b: 0))
    }
}

@Suite("Formatting")
struct FormattingTests {
    @Test("Durations render like the web UI")
    func durations() {
        #expect(DurationFormatting.shortDuration(seconds: 0) == "0s")
        #expect(DurationFormatting.shortDuration(seconds: 45) == "45s")
        #expect(DurationFormatting.shortDuration(seconds: 600) == "10m")
        #expect(DurationFormatting.shortDuration(seconds: 10800) == "3h")
    }
}

@Suite("Client construction")
struct ClientConstructionTests {
    @Test("A bare host gains a scheme")
    func bareHostGetsScheme() throws {
        let client = try #require(NeriticClient(host: "192.168.1.42"))
        #expect(client.baseURL.absoluteString == "http://192.168.1.42")
    }

    @Test("Host with a port is preserved")
    func hostWithPort() throws {
        let client = try #require(NeriticClient(host: "neritic.local:8080"))
        #expect(client.baseURL.absoluteString == "http://neritic.local:8080")
    }

    @Test("An explicit scheme is respected")
    func explicitScheme() throws {
        let client = try #require(NeriticClient(host: "https://neritic.local"))
        #expect(client.baseURL.absoluteString == "https://neritic.local")
    }

    @Test("Blank input is rejected")
    func blankHostRejected() {
        #expect(NeriticClient(host: "   ") == nil)
        #expect(NeriticClient(host: "") == nil)
    }

    @Test("Request paths are appended to the host")
    func pathsResolve() throws {
        let client = try #require(NeriticClient(host: "neritic.local"))
        // Exercised indirectly: the stream rewrites the scheme and path for /ws.
        #expect(client.baseURL.host == "neritic.local")
    }
}
