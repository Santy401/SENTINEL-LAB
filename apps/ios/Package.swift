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
        .library(name: "SentinelLab", targets: ["SentinelLab"]),
        // Live Activity widget extension (xtool.yml `extensions:`).
        .library(name: "SentinelLabWidget", targets: ["SentinelLabWidget"])
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
            swiftSettings: [.swiftLanguageMode(.v5)],
            // Leave room in the Mach-O header for LC_CODE_SIGNATURE; without it
            // xtool's signer fails with "Can't find free space of LoadCommands".
            linkerSettings: [.unsafeFlags(["-Xlinker", "-headerpad_max_install_names"])]
        ),
        .target(
            name: "SentinelLabWidget",
            dependencies: [.product(name: "SentinelKit", package: "SentinelKit")],
            path: "SentinelLabWidget",
            exclude: ["Resources"],
            swiftSettings: [.swiftLanguageMode(.v5)],
            // Leave room in the Mach-O header for LC_CODE_SIGNATURE; without it
            // xtool's signer fails with "Can't find free space of LoadCommands".
            linkerSettings: [.unsafeFlags(["-Xlinker", "-headerpad_max_install_names"])]
        )
    ]
)
