// swift-tools-version:5.9
// SentinelKit is Foundation-only so the protocol, reconnection and health
// logic can be compiled and tested on Linux as well as on Apple platforms.
import PackageDescription

let package = Package(
    name: "SentinelKit",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "SentinelKit", targets: ["SentinelKit"])
    ],
    targets: [
        .target(name: "SentinelKit"),
        .testTarget(
            name: "SentinelKitTests",
            dependencies: ["SentinelKit"],
            resources: [.copy("Fixtures")]
        )
    ]
)
