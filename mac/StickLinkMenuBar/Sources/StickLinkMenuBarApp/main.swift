import SwiftUI
import StickLinkCore

@main
struct StickLinkMenuBarApp: App {
    @StateObject private var appModel = StickLinkAppModel()

    var body: some Scene {
        MenuBarExtra {
            StickLinkRootView(model: appModel)
                .frame(width: 420)
        } label: {
            Label("Stick Link", systemImage: appModel.client.state.isConnected ? "dot.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash")
        }
        .menuBarExtraStyle(.window)
    }
}

final class StickLinkAppModel: ObservableObject {
    @Published private(set) var config: StickLinkConfig
    let configURL: URL
    let logStore: LogStore
    let client: StickBluetoothClient
    let transcriber: RemoteMicTranscriber

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

            Divider()

            ConfigView(
                config: model.config,
                configURL: model.configURL,
                onReload: { model.reloadConfig() }
            )

            HStack {
                Spacer()
                Button("Quit") {
                    NSApplication.shared.terminate(nil)
                }
                .keyboardShortcut("q")
            }
        }
        .padding(16)
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
