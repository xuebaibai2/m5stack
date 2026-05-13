import AVFoundation
import Foundation
import Speech

public protocol StickAudioReceiver: AnyObject {
    func handleControlMessage(_ message: StickMessage)
    func handleAudioChunk(_ data: Data)
    func updateConfig(_ config: StickLinkConfig)
}

public struct RemoteMicSessionTracker {
    public private(set) var currentSessionID: UInt64 = 0

    public init() {}

    @discardableResult
    public mutating func startSession() -> UInt64 {
        currentSessionID &+= 1
        return currentSessionID
    }

    public mutating func invalidateCurrentSession() {
        currentSessionID &+= 1
    }

    public func isCurrent(_ sessionID: UInt64) -> Bool {
        sessionID == currentSessionID
    }
}

public enum RemoteMicRecordingNamer {
    public static func filename(for date: Date = Date(), sessionID: UInt64) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return "StickLink-RemoteMic-\(formatter.string(from: date))-s\(sessionID).wav"
    }
}

public struct Pcm16LevelStats: Equatable {
    public let sampleCount: Int
    public let peak: Int
    public let rms: Int

    public init(data: Data) {
        var sampleCount = 0
        var peak = 0
        var sumSquares = 0.0

        data.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress else {
                return
            }

            let samples = baseAddress.assumingMemoryBound(to: Int16.self)
            let count = data.count / MemoryLayout<Int16>.size
            for index in 0..<count {
                let sample = Int(Int16(littleEndian: samples[index]))
                let magnitude = sample == Int(Int16.min) ? Int(Int16.max) : abs(sample)
                peak = max(peak, magnitude)
                sumSquares += Double(sample * sample)
            }
            sampleCount = count
        }

        self.sampleCount = sampleCount
        self.peak = peak
        self.rms = sampleCount > 0 ? Int((sumSquares / Double(sampleCount)).squareRoot()) : 0
    }
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
    private let audioDecoder = Pcm12Decoder()
    private var finalTranscript = ""
    private var receivedAudio = Data()
    private var receivedChunkCount = 0
    private var droppedChunkCount = 0
    private var finishRequested = false
    private var didFinalizeSession = false
    private var sessionTracker = RemoteMicSessionTracker()

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
        guard isRecording else {
            droppedChunkCount += 1
            if droppedChunkCount == 1 || droppedChunkCount.isMultiple(of: 25) {
                logStore.append(.warning, "Dropped audio chunk without active recording: \(data.count) bytes")
            }
            return
        }

        let pcmData = audioDecoder.decode(data)
        guard !pcmData.isEmpty else {
            logStore.append(.warning, "Dropped malformed audio chunk: \(data.count) bytes")
            return
        }

        receivedChunkCount += 1
        receivedAudio.append(pcmData)

        if let request {
            guard let buffer = pcmBuffer(from: pcmData) else {
                logStore.append(.warning, "Decoded audio chunk could not be buffered: \(pcmData.count) bytes")
                return
            }
            request.append(buffer)
        }
    }

    private func startSession() {
        stopSession(shouldPaste: false)
        let sessionID = sessionTracker.startSession()

        self.finalTranscript = ""
        self.latestTranscript = ""
        self.receivedAudio.removeAll(keepingCapacity: true)
        self.receivedChunkCount = 0
        self.droppedChunkCount = 0
        self.audioDecoder.reset()
        self.finishRequested = false
        self.didFinalizeSession = false
        self.isRecording = true
        self.recognizer = SFSpeechRecognizer(locale: Locale(identifier: config.transcriptionLocaleIdentifier))

        logStore.append(.info, "Remote Mic audio capture started")

        guard let recognizer else {
            logStore.append(.error, "Speech recognizer unavailable; saving audio only")
            return
        }

        guard recognizer.isAvailable else {
            logStore.append(.error, "Speech recognizer is not available; saving audio only")
            return
        }

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        self.request = request

        task = recognizer.recognitionTask(with: request) { [weak self] result, error in
            guard let self else {
                return
            }
            guard self.sessionTracker.isCurrent(sessionID) else {
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
                let message = "Speech recognition failed: \(error.localizedDescription)"
                if error.localizedDescription.localizedCaseInsensitiveContains("no speech") {
                    self.logStore.append(.warning, "\(message); WAV saved for inspection")
                } else {
                    self.logStore.append(.error, message)
                }
            }

            if self.finishRequested, result?.isFinal == true || error != nil {
                self.finalizeSession(shouldPaste: true, sessionID: sessionID)
            }
        }

        logStore.append(.info, "Remote Mic transcription started")
    }

    private func stopSession(shouldPaste: Bool = true) {
        guard isRecording || request != nil || task != nil else {
            return
        }

        let sessionID = sessionTracker.currentSessionID
        let chunkCount = receivedChunkCount
        let byteCount = receivedAudio.count
        finishRequested = shouldPaste
        if shouldPaste {
            saveRecordingIfNeeded()
            logStore.append(.audio, "Remote Mic audio ended: \(chunkCount) chunks, \(byteCount) bytes")
            finalizeSession(shouldPaste: shouldPaste, sessionID: sessionID)
        }
        resetSessionAfterStop()
    }

    private func finalizeSession(shouldPaste: Bool, sessionID: UInt64) {
        guard sessionTracker.isCurrent(sessionID) else {
            return
        }

        guard !didFinalizeSession else {
            return
        }

        didFinalizeSession = true
        task = nil

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

    private func resetSessionAfterStop() {
        request?.endAudio()
        task?.cancel()
        request = nil
        task = nil
        isRecording = false
        finishRequested = false
        receivedAudio.removeAll(keepingCapacity: true)
        receivedChunkCount = 0
        droppedChunkCount = 0
        audioDecoder.reset()
        sessionTracker.invalidateCurrentSession()
        logStore.append(.info, "Remote Mic session reset")
    }

    private func saveRecordingIfNeeded() {
        guard config.saveRecordingsToDownloads, !receivedAudio.isEmpty else {
            return
        }

        do {
            let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
                ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Downloads")
            let filename = RemoteMicRecordingNamer.filename(sessionID: sessionTracker.currentSessionID)
            let url = downloads.appendingPathComponent(filename)
            try WavFileWriter.writePCM16Mono(data: receivedAudio,
                                             sampleRate: Int(config.audioSampleRate),
                                             to: url)
            let stats = Pcm16LevelStats(data: receivedAudio)
            logStore.append(.info, "Saved recording: \(url.path)")
            logStore.append(.audio, "Recording level: peak \(stats.peak), rms \(stats.rms), samples \(stats.sampleCount)")
        } catch {
            logStore.append(.error, "Recording save failed: \(error.localizedDescription)")
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
