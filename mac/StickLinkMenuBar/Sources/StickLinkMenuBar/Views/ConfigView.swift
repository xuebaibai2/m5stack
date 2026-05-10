import SwiftUI

public struct ConfigView: View {
    let config: StickLinkConfig
    let configURL: URL
    let onReload: () -> Void

    public init(config: StickLinkConfig, configURL: URL, onReload: @escaping () -> Void) {
        self.config = config
        self.configURL = configURL
        self.onReload = onReload
    }

    public var body: some View {
        DisclosureGroup("Config") {
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 6) {
                GridRow {
                    Text("File").foregroundStyle(.secondary)
                    Text(configURL.path)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                GridRow {
                    Text("Name prefix").foregroundStyle(.secondary)
                    Text(config.deviceNamePrefix)
                }
                GridRow {
                    Text("Apps").foregroundStyle(.secondary)
                    Text(config.allowedApps.isEmpty ? "Any" : config.allowedApps.joined(separator: ", "))
                }
                GridRow {
                    Text("Types").foregroundStyle(.secondary)
                    Text(config.allowedMessageTypes.isEmpty ? "Any" : config.allowedMessageTypes.joined(separator: ", "))
                }
                GridRow {
                    Text("Log limit").foregroundStyle(.secondary)
                    Text("\(config.maxRetainedLogs)")
                }
            }
            .font(.caption)

            Button("Reload Config", action: onReload)
        }
    }
}
