import Foundation

public final class Pcm12Decoder {
    public init() {}

    public func reset() {}

    public func decode(_ data: Data) -> Data {
        guard data.count % 3 == 0 else {
            return Data()
        }

        var output = Data()
        output.reserveCapacity((data.count / 3) * 2 * MemoryLayout<Int16>.size)

        var index = data.startIndex
        while index < data.endIndex {
            let firstByte = UInt16(data[index])
            let middleByte = UInt16(data[data.index(after: index)])
            let thirdByte = UInt16(data[data.index(index, offsetBy: 2)])

            let first = firstByte | ((middleByte & 0x0f) << 8)
            let second = ((middleByte >> 4) & 0x0f) | (thirdByte << 4)
            appendSample(first, to: &output)
            appendSample(second, to: &output)

            index = data.index(index, offsetBy: 3)
        }

        return output
    }

    private func appendSample(_ unsignedSample: UInt16, to output: inout Data) {
        let clamped = min(unsignedSample, 0x0fff)
        var sample = Int16(Int(clamped) - 2048) << 4
        sample = sample.littleEndian
        withUnsafeBytes(of: &sample) { output.append(contentsOf: $0) }
    }
}
