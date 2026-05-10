import Foundation

public final class MuLawDecoder {
    public init() {}

    public func reset() {}

    public func decode(_ data: Data) -> Data {
        var output = Data()
        output.reserveCapacity(data.count * MemoryLayout<Int16>.size)

        for byte in data {
            var sample = Self.decodeSample(byte).littleEndian
            withUnsafeBytes(of: &sample) { output.append(contentsOf: $0) }
        }

        return output
    }

    private static func decodeSample(_ byte: UInt8) -> Int16 {
        let inverted = ~byte
        let sign = inverted & 0x80
        let exponent = Int((inverted >> 4) & 0x07)
        let mantissa = Int(inverted & 0x0f)
        var sample = ((mantissa << 3) + 0x84) << exponent
        sample -= 0x84

        if sign != 0 {
            sample = -sample
        }

        return Int16(clamping: sample)
    }
}
