import SwiftUI

public struct StatusView: View {
    @ObservedObject var client: StickBluetoothClient

    public init(client: StickBluetoothClient) {
        self.client = client
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
                GridRow {
                    Text("Audio chunks").foregroundStyle(.secondary)
                    Text("\(client.deviceInfo.audioChunkCount)")
                }
            }
            .font(.caption)
        }
    }
}
