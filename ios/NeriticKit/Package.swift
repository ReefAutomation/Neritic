// swift-tools-version: 6.0
import PackageDescription

// NeriticKit holds everything that talks to a Neritic controller: the REST
// client, the typed models, the live LED WebSocket stream and local discovery.
// It is a standalone package so it can be unit tested from the command line
// (`swift test` in ios/NeriticKit) without building the app.
let package = Package(
    name: "NeriticKit",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(name: "NeriticKit", targets: ["NeriticKit"])
    ],
    targets: [
        .target(name: "NeriticKit"),
        .testTarget(name: "NeriticKitTests", dependencies: ["NeriticKit"]),
    ]
)
