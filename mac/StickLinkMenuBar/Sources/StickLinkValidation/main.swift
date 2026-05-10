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
      "app": "sensor",
      "type": "button",
      "name": "ButtonA",
      "text": "ButtonA pressed from Sensor App",
      "ts_ms": 123456,
      "seq": 7,
      "future": "ignored"
    }
    """.data(using: .utf8)!

    let message = try JSONDecoder().decode(StickMessage.self, from: data)
    try expect(message.version == 1, "message version decoded")
    try expect(message.id == "000007", "message id decoded")
    try expect(message.app == "sensor", "message app decoded")
    try expect(message.type == "button", "message type decoded")
    try expect(message.name == "ButtonA", "message name decoded")
    try expect(message.text == "ButtonA pressed from Sensor App", "message text decoded")
    try expect(message.timestampMilliseconds == 123456, "message timestamp decoded")
    try expect(message.sequence == 7, "message sequence decoded")
}

func validateConfigLoading() throws {
    let defaultConfig = StickLinkConfig.default
    try expect(defaultConfig.serviceUUID == "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default service UUID")
    try expect(defaultConfig.messageCharacteristicUUID == "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default message characteristic UUID")
    try expect(defaultConfig.deviceInfoCharacteristicUUID == "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60", "default info characteristic UUID")

    let data = """
    {
      "deviceNamePrefix": "LabStick",
      "allowedApps": ["sensor", "voice"],
      "allowedMessageTypes": ["button"],
      "scanTimeoutSeconds": 12,
      "maxRetainedLogs": 3
    }
    """.data(using: .utf8)!

    let loaded = try StickLinkConfig.load(data: data)
    try expect(loaded.serviceUUID == defaultConfig.serviceUUID, "partial config keeps default service UUID")
    try expect(loaded.deviceNamePrefix == "LabStick", "partial config overrides device prefix")
    try expect(loaded.allowedApps == ["sensor", "voice"], "partial config overrides apps")
    try expect(loaded.allowedMessageTypes == ["button"], "partial config overrides message types")
    try expect(loaded.scanTimeoutSeconds == 12, "partial config overrides scan timeout")
    try expect(loaded.maxRetainedLogs == 3, "partial config overrides log retention")
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
        app: "sensor",
        type: "button",
        name: "ButtonA",
        text: "ButtonA pressed from Sensor App",
        timestampMilliseconds: 42,
        sequence: 1
    )

    store.append(message)
    try expect(store.entries.last?.message == "sensor/button ButtonA: ButtonA pressed from Sensor App", "log store formats Stick messages")
}

do {
    try validateMessageDecoding()
    try validateConfigLoading()
    try validateLogStore()
    print("StickLinkValidation passed")
} catch {
    fputs("StickLinkValidation failed: \(error)\n", stderr)
    exit(1)
}
