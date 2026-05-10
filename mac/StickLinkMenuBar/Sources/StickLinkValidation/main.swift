import Foundation
import StickLinkCore

enum ValidationError: Error, CustomStringConvertible {
    case failed(String)

    var description: String {
        switch self {
        case .failed(let message):
            return message
        }
    }
}

func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() {
        throw ValidationError.failed(message)
    }
}

func validateMessageDecoding() throws {
    let data = """
    {
      "v": 1,
      "id": "000007",
      "app": "remote_mic",
      "type": "voice",
      "name": "start",
      "text": "Remote Mic recording started",
      "ts_ms": 123456,
      "seq": 7,
      "future": "ignored"
    }
    """.data(using: .utf8)!

    let message = try JSONDecoder().decode(StickMessage.self, from: data)
    try expect(message.version == 1, "message version decoded")
    try expect(message.id == "000007", "message id decoded")
    try expect(message.app == "remote_mic", "message app decoded")
    try expect(message.type == "voice", "message type decoded")
    try expect(message.name == "start", "message name decoded")
    try expect(message.text == "Remote Mic recording started", "message text decoded")
    try expect(message.timestampMilliseconds == 123456, "message timestamp decoded")
    try expect(message.sequence == 7, "message sequence decoded")
}

func validateConfigLoading() throws {
    let defaultConfig = StickLinkConfig.default
    try expect(defaultConfig.serviceUUID == "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default service UUID")
    try expect(defaultConfig.messageCharacteristicUUID == "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default message characteristic UUID")
    try expect(defaultConfig.deviceInfoCharacteristicUUID == "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default info characteristic UUID")
    try expect(defaultConfig.audioCharacteristicUUID == "6f7d9f13-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default audio characteristic UUID")
    try expect(defaultConfig.audioSampleRate == 8000, "default audio sample rate")

    let data = """
    {
      "deviceNamePrefix": "LabStick",
      "allowedApps": ["sensor", "voice"],
      "allowedMessageTypes": ["button"],
      "scanTimeoutSeconds": 12,
      "maxRetainedLogs": 3,
      "audioSampleRate": 16000,
      "transcriptionLocaleIdentifier": "en-AU",
      "pasteTranscriptsToFocusedApp": false,
      "saveRecordingsToDownloads": false
    }
    """.data(using: .utf8)!

    let loaded = try StickLinkConfig.load(data: data)
    try expect(loaded.serviceUUID == defaultConfig.serviceUUID, "partial config keeps default service UUID")
    try expect(loaded.deviceNamePrefix == "LabStick", "partial config overrides device prefix")
    try expect(loaded.allowedApps == ["sensor", "voice"], "partial config overrides apps")
    try expect(loaded.allowedMessageTypes == ["button"], "partial config overrides message types")
    try expect(loaded.scanTimeoutSeconds == 12, "partial config overrides scan timeout")
    try expect(loaded.maxRetainedLogs == 3, "partial config overrides log retention")
    try expect(loaded.audioSampleRate == 16000, "partial config overrides sample rate")
    try expect(loaded.transcriptionLocaleIdentifier == "en-AU", "partial config overrides locale")
    try expect(loaded.pasteTranscriptsToFocusedApp == false, "partial config overrides output behavior")
    try expect(loaded.saveRecordingsToDownloads == false, "partial config overrides recording save behavior")
}

func validateWavWriter() throws {
    let tempURL = FileManager.default.temporaryDirectory
        .appendingPathComponent("StickLinkValidation-\(UUID().uuidString).wav")
    defer { try? FileManager.default.removeItem(at: tempURL) }

    let pcm = Data([0x00, 0x00, 0xff, 0x7f])
    try WavFileWriter.writePCM16Mono(data: pcm, sampleRate: 8000, to: tempURL)
    let wav = try Data(contentsOf: tempURL)

    try expect(wav.count == 48, "wav size includes 44-byte header plus PCM")
    try expect(String(data: wav.prefix(4), encoding: .ascii) == "RIFF", "wav RIFF header")
    try expect(String(data: wav.dropFirst(8).prefix(4), encoding: .ascii) == "WAVE", "wav WAVE header")
    try expect(wav.suffix(4) == pcm, "wav preserves PCM payload")
}

func validateLogStore() throws {
    let store = LogStore(maxCount: 2)
    store.append(LogStore.info("first"))
    store.append(LogStore.info("second"))
    store.append(LogStore.info("third"))
    try expect(store.entries.map(\.message) == ["second", "third"], "log store retains newest entries")

    let message = StickMessage(
        version: 1,
        id: "000001",
        app: "remote_mic",
        type: "voice",
        name: "stop",
        text: "Remote Mic recording stopped",
        timestampMilliseconds: 42,
        sequence: 1
    )

    store.append(message)
    try expect(store.entries.last?.message == "remote_mic/voice stop: Remote Mic recording stopped", "log store formats Stick messages")
}

do {
    try validateMessageDecoding()
    try validateConfigLoading()
    try validateLogStore()
    try validateWavWriter()
    print("StickLinkValidation passed")
} catch {
    fputs("StickLinkValidation failed: \(error)\n", stderr)
    exit(1)
}
