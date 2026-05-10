import Foundation

public final class PcmU8Decoder {
    public init() {}

    public func reset() {}

    public func decode(_ data: Data) -> Data {
        var output = Data()
        output.reserveCapacity(data.count * MemoryLayout<Int16>.size)

        for byte in data {
            var sample = Int16(Int(byte) - 128) << 8
            sample = sample.littleEndian
            withUnsafeBytes(of: &sample) { output.append(contentsOf: $0) }
        }

        return output
    }
}
