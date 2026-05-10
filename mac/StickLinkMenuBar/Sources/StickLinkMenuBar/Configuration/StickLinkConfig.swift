import Foundation

public struct StickLinkConfig: Codable, Equatable {
    public var serviceUUID: String
    public var messageCharacteristicUUID: String
    public var deviceInfoCharacteristicUUID: String
    public var audioCharacteristicUUID: String
    public var deviceNamePrefix: String
    public var allowedApps: [String]
    public var allowedMessageTypes: [String]
    public var scanTimeoutSeconds: TimeInterval
    public var maxRetainedLogs: Int
    public var audioSampleRate: Double
    public var transcriptionLocaleIdentifier: String
    public var pasteTranscriptsToFocusedApp: Bool
    public var saveRecordingsToDownloads: Bool

    public static let `default` = StickLinkConfig(
        serviceUUID: "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60",
        messageCharacteristicUUID: "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60",
        deviceInfoCharacteristicUUID: "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60",
        audioCharacteristicUUID: "6f7d9f13-2c3b-4e7a-9a1f-1b2c3d4e5f60",
        deviceNamePrefix: "StickS3",
        allowedApps: [],
        allowedMessageTypes: [],
        scanTimeoutSeconds: 10,
        maxRetainedLogs: 200,
        audioSampleRate: 16000,
        transcriptionLocaleIdentifier: "en-US",
        pasteTranscriptsToFocusedApp: true,
        saveRecordingsToDownloads: true
    )

    public static func load(data: Data) throws -> StickLinkConfig {
        let patch = try JSONDecoder().decode(PartialStickLinkConfig.self, from: data)
        var config = StickLinkConfig.default
        config.serviceUUID = patch.serviceUUID ?? config.serviceUUID
        config.messageCharacteristicUUID = patch.messageCharacteristicUUID ?? config.messageCharacteristicUUID
        config.deviceInfoCharacteristicUUID = patch.deviceInfoCharacteristicUUID ?? config.deviceInfoCharacteristicUUID
        config.audioCharacteristicUUID = patch.audioCharacteristicUUID ?? config.audioCharacteristicUUID
        config.deviceNamePrefix = patch.deviceNamePrefix ?? config.deviceNamePrefix
        config.allowedApps = patch.allowedApps ?? config.allowedApps
        config.allowedMessageTypes = patch.allowedMessageTypes ?? config.allowedMessageTypes
        config.scanTimeoutSeconds = patch.scanTimeoutSeconds ?? config.scanTimeoutSeconds
        config.maxRetainedLogs = patch.maxRetainedLogs ?? config.maxRetainedLogs
        config.audioSampleRate = patch.audioSampleRate ?? config.audioSampleRate
        config.transcriptionLocaleIdentifier = patch.transcriptionLocaleIdentifier ?? config.transcriptionLocaleIdentifier
        config.pasteTranscriptsToFocusedApp = patch.pasteTranscriptsToFocusedApp ?? config.pasteTranscriptsToFocusedApp
        config.saveRecordingsToDownloads = patch.saveRecordingsToDownloads ?? config.saveRecordingsToDownloads
        return config
    }

    public static func load(from url: URL) throws -> StickLinkConfig {
        try load(data: Data(contentsOf: url))
    }

    public static func defaultConfigURL(currentDirectory: String = FileManager.default.currentDirectoryPath) -> URL {
        URL(fileURLWithPath: currentDirectory)
            .appendingPathComponent("config")
            .appendingPathComponent("sticklink.json")
    }
}

private struct PartialStickLinkConfig: Codable {
    var serviceUUID: String?
    var messageCharacteristicUUID: String?
    var deviceInfoCharacteristicUUID: String?
    var audioCharacteristicUUID: String?
    var deviceNamePrefix: String?
    var allowedApps: [String]?
    var allowedMessageTypes: [String]?
    var scanTimeoutSeconds: TimeInterval?
    var maxRetainedLogs: Int?
    var audioSampleRate: Double?
    var transcriptionLocaleIdentifier: String?
    var pasteTranscriptsToFocusedApp: Bool?
    var saveRecordingsToDownloads: Bool?
}
