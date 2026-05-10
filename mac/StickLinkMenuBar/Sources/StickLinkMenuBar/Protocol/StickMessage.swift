import Foundation

public struct StickMessage: Codable, Equatable, Identifiable {
    public let version: Int
    public let id: String
    public let app: String
    public let type: String
    public let name: String
    public let text: String
    public let timestampMilliseconds: UInt32
    public let sequence: UInt32

    enum CodingKeys: String, CodingKey {
        case version = "v"
        case id
        case app
        case type
        case name
        case text
        case timestampMilliseconds = "ts_ms"
        case sequence = "seq"
    }

    public init(
        version: Int,
        id: String,
        app: String,
        type: String,
        name: String,
        text: String,
        timestampMilliseconds: UInt32,
        sequence: UInt32
    ) {
        self.version = version
        self.id = id
        self.app = app
        self.type = type
        self.name = name
        self.text = text
        self.timestampMilliseconds = timestampMilliseconds
        self.sequence = sequence
    }
}
