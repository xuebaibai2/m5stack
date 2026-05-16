import SwiftUI

public struct WeatherFirmwareConfigView: View {
    @ObservedObject var store: FirmwareConfigStore
    let onSendToStick: (FirmwareConfig) -> Void

    public init(store: FirmwareConfigStore, onSendToStick: @escaping (FirmwareConfig) -> Void) {
        self.store = store
        self.onSendToStick = onSendToStick
    }

    public var body: some View {
        Form {
            Section {
                TextField("Location name", text: $store.config.weatherLocationName)
                TextField("Latitude", text: $store.config.weatherLatitude)
                TextField("Longitude", text: $store.config.weatherLongitude)
                TextField("Timezone", text: $store.config.weatherTimezone)
            } header: {
                Text("Weather")
            }

            firmwareConfigActions(store: store, saveTitle: "Save & Send") {
                store.save()
                onSendToStick(store.config)
            }
        }
        .formStyle(.grouped)
    }
}

public struct DeviceFirmwareConfigView: View {
    @ObservedObject var store: FirmwareConfigStore
    @ObservedObject var client: StickBluetoothClient
    let onScan: () -> Void
    let onDisconnect: () -> Void

    public init(
        store: FirmwareConfigStore,
        client: StickBluetoothClient,
        onScan: @escaping () -> Void,
        onDisconnect: @escaping () -> Void
    ) {
        self.store = store
        self.client = client
        self.onScan = onScan
        self.onDisconnect = onDisconnect
    }

    public var body: some View {
        Form {
            Section {
                HStack {
                    Circle()
                        .fill(client.state.isConnected ? Color.green : Color.orange)
                        .frame(width: 10, height: 10)
                    Text(client.state.label)
                    Spacer()
                }

                HStack {
                    Button("Scan", action: onScan)
                        .keyboardShortcut("r")
                    Button("Disconnect", action: onDisconnect)
                        .disabled(!client.state.isConnected)
                    Spacer()
                }
            } header: {
                Text("BLE Connection")
            }

            Section {
                TextField("Wi-Fi SSID", text: $store.config.wifiSSID)
                SecureField("Wi-Fi Password", text: $store.config.wifiPassword)
            } header: {
                Text("Generated Config")
            }

            firmwareConfigActions(store: store, saveTitle: "Save") {
                store.save()
            }
        }
        .formStyle(.grouped)
    }
}

private func firmwareConfigActions(
    store: FirmwareConfigStore,
    saveTitle: String,
    onSave: @escaping () -> Void
) -> some View {
    Section {
        Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 4) {
            GridRow {
                Text("File").foregroundStyle(.secondary)
                Text(store.envURL.path)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            GridRow {
                Text("Status").foregroundStyle(.secondary)
                Text(store.statusMessage)
            }
        }
        .font(.caption)

        HStack {
            Button("Reload") {
                store.load()
            }
            Button(saveTitle) {
                onSave()
            }
            .keyboardShortcut(.defaultAction)
        }
    }
}
