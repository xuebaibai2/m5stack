import Foundation
import Combine

public final class LogStore: ObservableObject {
    public struct Entry: Identifiable, Equatable {
        public enum Level: String, CaseIterable, Equatable, Identifiable {
            case info
            case audio
            case received
            case warning
            case error

            public var id: String { rawValue }
        }

        public let id: UUID
        public let date: Date
        public let level: Level
        public let message: String

        public init(id: UUID = UUID(), date: Date = Date(), level: Level, message: String) {
            self.id = id
            self.date = date
            self.level = level
            self.message = message
        }
    }

    @Published public private(set) var entries: [Entry] = []
    public var maxCount: Int

    public init(maxCount: Int) {
        self.maxCount = max(1, maxCount)
    }

    public func append(_ entry: Entry) {
        entries.append(entry)
        if entries.count > maxCount {
            entries.removeFirst(entries.count - maxCount)
        }
    }

    public func append(_ level: Entry.Level, _ message: String) {
        append(Entry(level: level, message: message))
    }

    public func clear() {
        entries.removeAll()
    }

    public func append(_ message: StickMessage) {
        append(.received, "\(message.app)/\(message.type) \(message.name): \(message.text)")
    }

    public static func info(_ message: String) -> Entry {
        Entry(level: .info, message: message)
    }
}
