import AVFoundation
import Foundation
import Speech

public protocol StickAudioReceiver: AnyObject {
    func handleControlMessage(_ message: StickMessage)
    func handleAudioChunk(_ data: Data)
    func updateConfig(_ config: StickLinkConfig)
}

public final class RemoteMicTranscriber: ObservableObject, StickAudioReceiver {
    @Published public private(set) var isRecording = false
    @Published public private(set) var latestTranscript = ""

    private let logStore: LogStore
    private let outputController: TextOutputController
    private var config: StickLinkConfig
    private var recognizer: SFSpeechRecognizer?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var format: AVAudioFormat
    private var finalTranscript = ""

    public init(config: StickLinkConfig, logStore: LogStore, outputController: TextOutputController) {
        self.config = config
        self.logStore = logStore
        self.outputController = outputController
        self.format = RemoteMicTranscriber.makeFormat(sampleRate: config.audioSampleRate)
        self.recognizer = SFSpeechRecognizer(locale: Locale(identifier: config.transcriptionLocaleIdentifier))
        requestSpeechAuthorization()
    }

    public func updateConfig(_ config: StickLinkConfig) {
        self.config = config
        self.format = Self.makeFormat(sampleRate: config.audioSampleRate)
        self.recognizer = SFSpeechRecognizer(locale: Locale(identifier: config.transcriptionLocaleIdentifier))
    }

    public func handleControlMessage(_ message: StickMessage) {
        guard message.app == "remote_mic", message.type == "voice" else {
            return
        }

        if message.name == "start" {
            startSession()
        } else if message.name == "stop" {
            stopSession()
        }
    }

    public func handleAudioChunk(_ data: Data) {
        guard isRecording, let request else {
            return
        }

        guard let buffer = pcmBuffer(from: data) else {
            logStore.append(.warning, "Dropped malformed audio chunk: \(data.count) bytes")
            return
        }

        request.append(buffer)
    }

    private func startSession() {
        stopSession(shouldPaste: false)

        guard let recognizer else {
            logStore.append(.error, "Speech recognizer unavailable")
            return
        }

        guard recognizer.isAvailable else {
            logStore.append(.error, "Speech recognizer is not available")
            return
        }

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        self.request = request
        self.finalTranscript = ""
        self.latestTranscript = ""
        self.isRecording = true

        task = recognizer.recognitionTask(with: request) { [weak self] result, error in
            guard let self else {
                return
            }

            if let result {
                let text = result.bestTranscription.formattedString
                self.latestTranscript = text
                if result.isFinal {
                    self.finalTranscript = text
                }
            }

            if let error {
                self.logStore.append(.error, "Speech recognition failed: \(error.localizedDescription)")
                self.isRecording = false
            }
        }

        logStore.append(.info, "Remote Mic transcription started")
    }

    private func stopSession(shouldPaste: Bool = true) {
        guard isRecording || request != nil || task != nil else {
            return
        }

        request?.endAudio()
        task?.finish()
        request = nil
        task = nil
        isRecording = false

        let transcript = finalTranscript.isEmpty ? latestTranscript : finalTranscript
        if transcript.isEmpty {
            logStore.append(.warning, "Remote Mic stopped with no transcript")
        } else {
            logStore.append(.received, "Transcript: \(transcript)")
            if shouldPaste && config.pasteTranscriptsToFocusedApp {
                outputController.pasteIntoFocusedApp(transcript)
            }
        }
    }

    private func pcmBuffer(from data: Data) -> AVAudioPCMBuffer? {
        guard data.count >= MemoryLayout<Int16>.size,
              data.count % MemoryLayout<Int16>.size == 0 else {
            return nil
        }

        let sampleCount = data.count / MemoryLayout<Int16>.size
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(sampleCount)),
              let channel = buffer.int16ChannelData?[0] else {
            return nil
        }

        buffer.frameLength = AVAudioFrameCount(sampleCount)
        data.withUnsafeBytes { rawBuffer in
            if let baseAddress = rawBuffer.baseAddress {
                memcpy(channel, baseAddress, data.count)
            }
        }
        return buffer
    }

    private func requestSpeechAuthorization() {
        SFSpeechRecognizer.requestAuthorization { [weak self] status in
            DispatchQueue.main.async {
                switch status {
                case .authorized:
                    self?.logStore.append(.info, "Speech recognition authorized")
                case .denied:
                    self?.logStore.append(.error, "Speech recognition permission denied")
                case .restricted:
                    self?.logStore.append(.error, "Speech recognition restricted")
                case .notDetermined:
                    self?.logStore.append(.warning, "Speech recognition permission not determined")
                @unknown default:
                    self?.logStore.append(.warning, "Unknown Speech authorization state")
                }
            }
        }
    }

    private static func makeFormat(sampleRate: Double) -> AVAudioFormat {
        AVAudioFormat(commonFormat: .pcmFormatInt16,
                      sampleRate: sampleRate,
                      channels: 1,
                      interleaved: true)!
    }
}
