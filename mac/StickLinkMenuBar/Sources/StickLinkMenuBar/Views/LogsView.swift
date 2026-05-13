import SwiftUI

public struct LogsView: View {
    @ObservedObject var logStore: LogStore
    @State private var visibleLevels = Set(LogStore.Entry.Level.allCases)

    public init(logStore: LogStore) {
        self.logStore = logStore
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Logs")
                    .font(.headline)
                Spacer()
                Button("Clear") {
                    logStore.clear()
                }
                .disabled(logStore.entries.isEmpty)
            }

            HStack(spacing: 10) {
                ForEach(LogStore.Entry.Level.allCases) { level in
                    Toggle(level.rawValue.capitalized, isOn: Binding(
                        get: { visibleLevels.contains(level) },
                        set: { isVisible in
                            if isVisible {
                                visibleLevels.insert(level)
                            } else {
                                visibleLevels.remove(level)
                            }
                        }
                    ))
                    .toggleStyle(.checkbox)
                    .font(.caption)
                }
            }

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 6) {
                    ForEach(filteredEntries) { entry in
                        HStack(alignment: .top, spacing: 8) {
                            Text(entry.level.rawValue.uppercased())
                                .font(.caption2)
                                .foregroundStyle(color(for: entry.level))
                                .frame(width: 64, alignment: .leading)
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

    private var filteredEntries: [LogStore.Entry] {
        logStore.entries.filter { visibleLevels.contains($0.level) }
    }

    private func color(for level: LogStore.Entry.Level) -> Color {
        switch level {
        case .info:
            return .secondary
        case .audio:
            return .blue
        case .received:
            return .green
        case .warning:
            return .orange
        case .error:
            return .red
        }
    }
}
