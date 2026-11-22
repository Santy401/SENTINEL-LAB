// swift-tools-version: 6.0
// SwiftPM adapter for building the iPhone app on Linux with xtool
// (scripts/linux-*.sh, docs/ios/LINUX.md). The Xcode project generated from
// project.yml keeps building the same sources on macOS; both must compile.
import PackageDescription

let package = Package(
    name: "SentinelLab",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        // xtool requires exactly one library product: the app itself.
        .library(name: "SentinelLab", targets: ["SentinelLab"])
    ],
    dependencies: [
        .package(path: "SentinelKit")
    ],
    targets: [
        .target(
            name: "SentinelLab",
            dependencies: [.product(name: "SentinelKit", package: "SentinelKit")],
            path: "SentinelLab",
            // Info.plist and the asset catalog are Xcode inputs. xtool uses
            // xtool-config/Info.plist and xtool-config/AppIcon.png instead (no actool on Linux).
            exclude: ["Resources"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
