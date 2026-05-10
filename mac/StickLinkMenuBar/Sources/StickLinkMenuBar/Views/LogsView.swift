import SwiftUI

public struct LogsView: View {
    @ObservedObject var logStore: LogStore

    public init(logStore: LogStore) {
        self.logStore = logStore
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Logs")
                .font(.headline)

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 6) {
                    ForEach(logStore.entries) { entry in
                        HStack(alignment: .top, spacing: 8) {
                            Text(entry.level.rawValue.uppercased())
                                .font(.caption2)
                                .foregroundStyle(color(for: entry.level))
                                .frame(width: 58, alignment: .leading)
                            Text(entry.message)
                                .font(.caption)
                                .textSelection(.enabled)
                            Spacer()
                        }
                        .padding(.vertical, 2)
                    }
                }
            }
            .frame(minHeight: 160, maxHeight: 220)
        }
    }

    private func color(for level: LogStore.Entry.Level) -> Color {
        switch level {
        case .info:
            return .secondary
        case .received:
            return .green
        case .warning:
            return .orange
        case .error:
            return .red
        }
    }
}
