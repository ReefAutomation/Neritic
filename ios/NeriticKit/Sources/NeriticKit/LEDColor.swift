import Foundation

/// A single addressable LED, as delivered in a live frame from `/ws`.
///
/// The device sends `led.count` pixels of four bytes each, in R, G, B, W order.
public struct RGBW: Equatable, Sendable {
    public var r: UInt8
    public var g: UInt8
    public var b: UInt8
    public var w: UInt8

    public init(r: UInt8, g: UInt8, b: UInt8, w: UInt8) {
        self.r = r
        self.g = g
        self.b = b
        self.w = w
    }

    /// Colour to draw on screen, blending the white channel into RGB.
    ///
    /// Mirrors `rgbwToDisplayColor` in the web UI (`web/util.js`) so the app's
    /// LED bar matches the browser exactly.
    public var displayColor: RGB {
        // A pure white pixel (r == g == b with some white) is drawn as white
        // rather than being dimmed by the blend below.
        if r == g, g == b, w > 0 {
            return RGB(r: 255, g: 255, b: 255)
        }
        return RGB(
            r: UInt8(min(255, Int(r) + Int(w))),
            g: UInt8(min(255, Int(g) + Int(w))),
            b: UInt8(min(255, Int(b) + Int(w)))
        )
    }
}

/// An 8-bit RGB colour.
public struct RGB: Equatable, Sendable {
    public var r: UInt8
    public var g: UInt8
    public var b: UInt8

    public init(r: UInt8, g: UInt8, b: UInt8) {
        self.r = r
        self.g = g
        self.b = b
    }

    /// Normalised components in 0...1, convenient for SwiftUI.
    public var unitComponents: (r: Double, g: Double, b: Double) {
        (Double(r) / 255.0, Double(g) / 255.0, Double(b) / 255.0)
    }
}

/// A complete LED frame from the live binary WebSocket stream.
public struct LEDFrame: Equatable, Sendable {
    public var pixels: [RGBW]

    public init(pixels: [RGBW]) {
        self.pixels = pixels
    }

    /// Decodes a binary `/ws` frame: `count` pixels of four bytes (R, G, B, W).
    ///
    /// Trailing bytes that do not form a whole pixel are ignored rather than
    /// being reinterpreted, so a truncated frame renders as far as it can.
    public static func decode(_ data: Data) -> LEDFrame {
        let pixelCount = data.count / 4
        var pixels = [RGBW]()
        pixels.reserveCapacity(pixelCount)

        let bytes = [UInt8](data)
        for index in 0..<pixelCount {
            let base = index * 4
            pixels.append(
                RGBW(
                    r: bytes[base],
                    g: bytes[base + 1],
                    b: bytes[base + 2],
                    w: bytes[base + 3]
                )
            )
        }
        return LEDFrame(pixels: pixels)
    }
}

public enum LEDColor {
    /// Parses a device colour string into a drawable colour.
    ///
    /// Accepts `#RRGGBB` and the `#RRGGBBWW` form some effects use. Mirrors
    /// `rgbwHexToPreview` in `web/util.js`; an unparseable value renders black,
    /// matching the web UI rather than substituting a different colour.
    public static func parse(hex: String?) -> RGB {
        guard let hex, hex.hasPrefix("#") else { return RGB(r: 0, g: 0, b: 0) }

        switch hex.count {
        case 9:
            guard
                let r = component(hex, at: 1),
                let g = component(hex, at: 3),
                let b = component(hex, at: 5),
                let w = component(hex, at: 7)
            else { return RGB(r: 0, g: 0, b: 0) }
            return RGBW(r: r, g: g, b: b, w: w).displayColor
        case 7:
            guard
                let r = component(hex, at: 1),
                let g = component(hex, at: 3),
                let b = component(hex, at: 5)
            else { return RGB(r: 0, g: 0, b: 0) }
            return RGB(r: r, g: g, b: b)
        default:
            return RGB(r: 0, g: 0, b: 0)
        }
    }

    private static func component(_ hex: String, at offset: Int) -> UInt8? {
        let start = hex.index(hex.startIndex, offsetBy: offset)
        guard let end = hex.index(start, offsetBy: 2, limitedBy: hex.endIndex) else { return nil }
        return UInt8(hex[start..<end], radix: 16)
    }
}

public enum DurationFormatting {
    /// `0s`, `45s`, `12m`, `3h` — matches `formatTransitionTime` in `web/util.js`.
    public static func shortDuration(seconds: Int) -> String {
        if seconds <= 0 { return "0s" }
        if seconds < 60 { return "\(seconds)s" }
        if seconds < 3600 { return "\(Int((Double(seconds) / 60).rounded()))m" }
        return "\(Int((Double(seconds) / 3600).rounded()))h"
    }
}
