import SwiftUI

public struct StatusView: View {
    @ObservedObject var client: StickBluetoothClient
    let onScan: () -> Void
    let onDisconnect: () -> Void

    public init(client: StickBluetoothClient, onScan: @escaping () -> Void, onDisconnect: @escaping () -> Void) {
        self.client = client
        self.onScan = onScan
        self.onDisconnect = onDisconnect
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Circle()
                    .fill(client.state.isConnected ? Color.green : Color.orange)
                    .frame(width: 10, height: 10)
                Text(client.state.label)
                    .font(.headline)
                Spacer()
            }

            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 6) {
                GridRow {
                    Text("Device").foregroundStyle(.secondary)
                    Text(client.deviceInfo.name)
                }
                GridRow {
                    Text("Service").foregroundStyle(.secondary)
                    Text(client.deviceInfo.serviceUUID.isEmpty ? "-" : client.deviceInfo.serviceUUID)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                GridRow {
                    Text("RSSI").foregroundStyle(.secondary)
                    Text(client.deviceInfo.rssi.map(String.init) ?? "-")
                }
                GridRow {
                    Text("Messages").foregroundStyle(.secondary)
                    Text("\(client.deviceInfo.messageCount)")
                }
            }
            .font(.caption)

            HStack {
                Button("Scan", action: onScan)
                    .keyboardShortcut("r")
                Button("Disconnect", action: onDisconnect)
                    .disabled(!client.state.isConnected)
                Spacer()
            }
        }
    }
}
