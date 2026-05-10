import Foundation

public enum WavFileWriter {
    public static func writePCM16Mono(data: Data, sampleRate: Int, to url: URL) throws {
        var output = Data()
        let byteRate = UInt32(sampleRate * 2)
        let blockAlign = UInt16(2)
        let bitsPerSample = UInt16(16)
        let dataSize = UInt32(data.count)
        let riffSize = UInt32(36 + data.count)

        output.appendASCII("RIFF")
        output.appendLittleEndian(riffSize)
        output.appendASCII("WAVE")
        output.appendASCII("fmt ")
        output.appendLittleEndian(UInt32(16))
        output.appendLittleEndian(UInt16(1))
        output.appendLittleEndian(UInt16(1))
        output.appendLittleEndian(UInt32(sampleRate))
        output.appendLittleEndian(byteRate)
        output.appendLittleEndian(blockAlign)
        output.appendLittleEndian(bitsPerSample)
        output.appendASCII("data")
        output.appendLittleEndian(dataSize)
        output.append(data)

        try output.write(to: url, options: .atomic)
    }
}

private extension Data {
    mutating func appendASCII(_ string: String) {
        append(string.data(using: .ascii)!)
    }

    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        var littleEndian = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndian) { append(contentsOf: $0) }
    }
}
