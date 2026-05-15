import AppKit
import Combine
import SwiftUI
import StickLinkCore

@main
struct StickLinkMenuBarApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        Settings {
            EmptyView()
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var statusController: StatusItemController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApplication.shared.setActivationPolicy(.accessory)
        statusController = StatusItemController(model: StickLinkAppModel())
    }
}

final class StatusItemController: NSObject {
    private let model: StickLinkAppModel
    private let statusItem: NSStatusItem
    private let popover = NSPopover()
    private var stateCancellable: AnyCancellable?

    init(model: StickLinkAppModel) {
        self.model = model
        self.statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        super.init()

        popover.behavior = .applicationDefined
        popover.contentSize = NSSize(width: 420, height: 640)
        popover.contentViewController = NSHostingController(
            rootView: StickLinkRootView(
                model: model,
                onClose: { [weak self] in self?.closePopover() }
            )
            .frame(width: 420)
        )

        if let button = statusItem.button {
            button.target = self
            button.action = #selector(togglePopover)
        }

        stateCancellable = model.client.$state
            .receive(on: RunLoop.main)
            .sink { [weak self] _ in
                self?.updateStatusIcon()
            }
        updateStatusIcon()
    }

    @objc private func togglePopover() {
        if popover.isShown {
            closePopover()
        } else {
            showPopover()
        }
    }

    private func showPopover() {
        guard let button = statusItem.button else {
            return
        }
        popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        popover.contentViewController?.view.window?.makeKey()
    }

    private func closePopover() {
        popover.performClose(nil)
    }

    private func updateStatusIcon() {
        guard let button = statusItem.button else {
            return
        }

        let symbolName = model.client.state.isConnected
            ? "dot.radiowaves.left.and.right"
            : "antenna.radiowaves.left.and.right.slash"
        button.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: "Stick Link")
        button.imagePosition = .imageOnly
    }
}

final class StickLinkAppModel: ObservableObject {
    @Published private(set) var config: StickLinkConfig
    let configURL: URL
    let logStore: LogStore
    let client: StickBluetoothClient
    let transcriber: RemoteMicTranscriber
    let firmwareConfigStore: FirmwareConfigStore

    init(configURL: URL = StickLinkConfig.defaultConfigURL()) {
        self.configURL = configURL
        let loadedConfig: StickLinkConfig
        do {
            loadedConfig = try StickLinkConfig.load(from: configURL)
        } catch {
            loadedConfig = .default
        }

        self.config = loadedConfig
        self.logStore = LogStore(maxCount: loadedConfig.maxRetainedLogs)
        self.firmwareConfigStore = FirmwareConfigStore()
        let outputController = TextOutputController(logStore: logStore)
        self.transcriber = RemoteMicTranscriber(
            config: loadedConfig,
            logStore: logStore,
            outputController: outputController
        )
        self.client = StickBluetoothClient(config: loadedConfig, logStore: logStore)
        self.client.audioReceiver = transcriber

        if FileManager.default.fileExists(atPath: configURL.path) {
            logStore.append(.info, "Loaded config \(configURL.path)")
        } else {
            logStore.append(.warning, "Using default config; file not found at \(configURL.path)")
        }

        DispatchQueue.main.async { [client] in
            client.startAutoConnect()
        }
    }

    func reloadConfig() {
        do {
            let loaded = try StickLinkConfig.load(from: configURL)
            config = loaded
            client.updateConfig(loaded)
            transcriber.updateConfig(loaded)
        } catch {
            config = .default
            client.updateConfig(.default)
            transcriber.updateConfig(.default)
            logStore.append(.error, "Config load failed: \(error.localizedDescription)")
        }
    }
}

struct StickLinkRootView: View {
    @ObservedObject var model: StickLinkAppModel
    let onClose: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            TabView {
                StickLinkStatusTab(model: model)
                    .tabItem {
                        Label("Status", systemImage: "dot.radiowaves.left.and.right")
                    }

                WeatherFirmwareConfigView(
                    store: model.firmwareConfigStore,
                    onSendToStick: { model.client.sendWeatherConfig($0) }
                )
                    .tabItem {
                        Label("Weather", systemImage: "cloud.sun")
                    }

                DeviceFirmwareConfigView(store: model.firmwareConfigStore)
                    .tabItem {
                        Label("Device", systemImage: "switch.2")
                    }

                ConfigView(
                    config: model.config,
                    configURL: model.configURL,
                    onReload: { model.reloadConfig() }
                )
                .tabItem {
                    Label("Mac", systemImage: "desktopcomputer")
                }
            }
            .frame(minHeight: 560)

            HStack {
                Spacer()
                Button("Close", action: onClose)
                    .keyboardShortcut(.cancelAction)
                Button("Quit") {
                    NSApplication.shared.terminate(nil)
                }
                .keyboardShortcut("q")
            }
        }
        .padding(16)
    }
}

struct StickLinkStatusTab: View {
    @ObservedObject var model: StickLinkAppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            StatusView(
                client: model.client,
                onScan: { model.client.startScan() },
                onDisconnect: { model.client.disconnect() }
            )

            Divider()

            LogsView(logStore: model.logStore)

            TranscriptView(transcriber: model.transcriber)

            Spacer(minLength: 0)
        }
        .padding(.top, 8)
    }
}

struct TranscriptView: View {
    @ObservedObject var transcriber: RemoteMicTranscriber

    var body: some View {
        if !transcriber.latestTranscript.isEmpty || transcriber.isRecording {
            VStack(alignment: .leading, spacing: 6) {
                Text(transcriber.isRecording ? "Listening" : "Last Transcript")
                    .font(.headline)
                Text(transcriber.latestTranscript.isEmpty ? "Waiting for speech..." : transcriber.latestTranscript)
                    .font(.caption)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }
}
