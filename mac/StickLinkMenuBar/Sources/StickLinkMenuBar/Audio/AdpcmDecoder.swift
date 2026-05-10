import Foundation

public final class AdpcmDecoder {
    private var predictor = 0
    private var stepIndex = 0

    private static let indexTable = [
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    ]

    private static let stepTable = [
        7, 8, 9, 10, 11, 12, 13, 14, 16,
        17, 19, 21, 23, 25, 28, 31, 34, 37,
        41, 45, 50, 55, 60, 66, 73, 80, 88,
        97, 107, 118, 130, 143, 157, 173, 190, 209,
        230, 253, 279, 307, 337, 371, 408, 449, 494,
        544, 598, 658, 724, 796, 876, 963, 1060, 1166,
        1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
        3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
        16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    ]

    public init() {}

    public func reset() {
        predictor = 0
        stepIndex = 0
    }

    public func decode(_ data: Data) -> Data {
        var output = Data()
        output.reserveCapacity(data.count * MemoryLayout<Int16>.size * 2)

        for byte in data {
            appendDecodedSample(nibble: byte & 0x0f, to: &output)
            appendDecodedSample(nibble: byte >> 4, to: &output)
        }

        return output
    }

    private func appendDecodedSample(nibble: UInt8, to output: inout Data) {
        var step = Self.stepTable[stepIndex]
        var delta = step >> 3

        if nibble & 4 != 0 {
            delta += step
        }
        step >>= 1
        if nibble & 2 != 0 {
            delta += step
        }
        step >>= 1
        if nibble & 1 != 0 {
            delta += step
        }

        if nibble & 8 != 0 {
            predictor -= delta
        } else {
            predictor += delta
        }

        predictor = min(Int(Int16.max), max(Int(Int16.min), predictor))
        stepIndex += Self.indexTable[Int(nibble & 0x0f)]
        stepIndex = min(Self.stepTable.count - 1, max(0, stepIndex))

        var sample = Int16(predictor).littleEndian
        withUnsafeBytes(of: &sample) { output.append(contentsOf: $0) }
    }
}
