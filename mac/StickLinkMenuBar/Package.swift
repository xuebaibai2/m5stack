// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "StickLinkMenuBar",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "StickLinkCore", targets: ["StickLinkCore"]),
        .executable(name: "StickLinkMenuBar", targets: ["StickLinkMenuBarApp"]),
        .executable(name: "StickLinkValidation", targets: ["StickLinkValidation"])
    ],
    targets: [
        .target(name: "StickLinkCore", path: "Sources/StickLinkMenuBar"),
        .executableTarget(
            name: "StickLinkMenuBarApp",
            dependencies: ["StickLinkCore"],
            path: "Sources/StickLinkMenuBarApp"
        ),
        .executableTarget(
            name: "StickLinkValidation",
            dependencies: ["StickLinkCore"],
            path: "Sources/StickLinkValidation"
        )
    ]
)
