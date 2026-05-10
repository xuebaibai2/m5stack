import Foundation
import CoreBluetooth
import Combine

public enum BluetoothConnectionState: Equatable {
    case idle
    case unsupported
    case unauthorized
    case poweredOff
    case scanning
    case connecting(String)
    case connected(String)
    case subscribed(String)
    case disconnected
    case failed(String)

    public var label: String {
        switch self {
        case .idle:
            return "Idle"
        case .unsupported:
            return "Bluetooth unsupported"
        case .unauthorized:
            return "Bluetooth unauthorized"
        case .poweredOff:
            return "Bluetooth off"
        case .scanning:
            return "Scanning"
        case .connecting(let name):
            return "Connecting to \(name)"
        case .connected(let name):
            return "Connected to \(name)"
        case .subscribed(let name):
            return "Subscribed to \(name)"
        case .disconnected:
            return "Disconnected"
        case .failed(let reason):
            return "Error: \(reason)"
        }
    }

    public var isConnected: Bool {
        switch self {
        case .connected, .subscribed:
            return true
        default:
            return false
        }
    }
}

public struct StickDeviceInfo: Equatable {
    public var name: String = "Unknown"
    public var identifier: UUID?
    public var rssi: Int?
    public var serviceUUID: String = ""
    public var messageCount: Int = 0
    public var rawInfo: String = ""
}

public final class StickBluetoothClient: NSObject, ObservableObject {
    @Published public private(set) var state: BluetoothConnectionState = .idle
    @Published public private(set) var deviceInfo = StickDeviceInfo()

    private let logStore: LogStore
    private var config: StickLinkConfig
    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var messageCharacteristic: CBCharacteristic?
    private var deviceInfoCharacteristic: CBCharacteristic?
    private var scanTimer: Timer?

    public init(config: StickLinkConfig, logStore: LogStore) {
        self.config = config
        self.logStore = logStore
        super.init()
        self.central = CBCentralManager(delegate: self, queue: .main)
    }

    public func updateConfig(_ config: StickLinkConfig) {
        self.config = config
        logStore.maxCount = config.maxRetainedLogs
        logStore.append(.info, "Config reloaded")
    }

    public func startScan() {
        guard let central else {
            state = .failed("Bluetooth manager unavailable")
            return
        }

        switch central.state {
        case .poweredOn:
            let service = CBUUID(string: config.serviceUUID)
            state = .scanning
            logStore.append(.info, "Scanning for \(config.serviceUUID)")
            central.scanForPeripherals(withServices: [service], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
            scanTimer?.invalidate()
            scanTimer = Timer.scheduledTimer(withTimeInterval: config.scanTimeoutSeconds, repeats: false) { [weak self] _ in
                self?.stopScanDueToTimeout()
            }
        case .poweredOff:
            state = .poweredOff
            logStore.append(.warning, "Bluetooth is powered off")
        case .unauthorized:
            state = .unauthorized
            logStore.append(.error, "Bluetooth permission is not granted")
        case .unsupported:
            state = .unsupported
            logStore.append(.error, "Bluetooth is unsupported on this Mac")
        default:
            state = .failed("Bluetooth is not ready: \(central.state.rawValue)")
        }
    }

    public func disconnect() {
        scanTimer?.invalidate()
        central?.stopScan()
        if let peripheral {
            central?.cancelPeripheralConnection(peripheral)
        }
        messageCharacteristic = nil
        deviceInfoCharacteristic = nil
        state = .disconnected
        logStore.append(.info, "Disconnected")
    }

    private func stopScanDueToTimeout() {
        central?.stopScan()
        if case .scanning = state {
            state = .failed("Scan timed out")
            logStore.append(.warning, "Scan timed out")
        }
    }

    private func accepts(peripheral: CBPeripheral) -> Bool {
        guard !config.deviceNamePrefix.isEmpty else {
            return true
        }
        guard let name = peripheral.name else {
            return false
        }
        return name.hasPrefix(config.deviceNamePrefix)
    }

    private func accepts(message: StickMessage) -> Bool {
        let appAllowed = config.allowedApps.isEmpty || config.allowedApps.contains(message.app)
        let typeAllowed = config.allowedMessageTypes.isEmpty || config.allowedMessageTypes.contains(message.type)
        return appAllowed && typeAllowed
    }
}

extension StickBluetoothClient: CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if case .idle = state {
                logStore.append(.info, "Bluetooth ready")
            }
        case .poweredOff:
            state = .poweredOff
        case .unauthorized:
            state = .unauthorized
        case .unsupported:
            state = .unsupported
        default:
            break
        }
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard accepts(peripheral: peripheral) else {
            return
        }

        scanTimer?.invalidate()
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self

        let name = peripheral.name ?? "Stick device"
        deviceInfo.name = name
        deviceInfo.identifier = peripheral.identifier
        deviceInfo.rssi = RSSI.intValue
        deviceInfo.serviceUUID = config.serviceUUID
        state = .connecting(name)
        logStore.append(.info, "Discovered \(name)")
        central.connect(peripheral)
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        let name = peripheral.name ?? "Stick device"
        state = .connected(name)
        logStore.append(.info, "Connected to \(name)")
        peripheral.discoverServices([CBUUID(string: config.serviceUUID)])
    }

    public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        state = .failed(error?.localizedDescription ?? "Failed to connect")
        logStore.append(.error, "Connection failed")
    }

    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        state = .disconnected
        if let error {
            logStore.append(.warning, "Disconnected: \(error.localizedDescription)")
        } else {
            logStore.append(.info, "Device disconnected")
        }
    }
}

extension StickBluetoothClient: CBPeripheralDelegate {
    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            state = .failed(error.localizedDescription)
            logStore.append(.error, "Service discovery failed: \(error.localizedDescription)")
            return
        }

        let wanted = [
            CBUUID(string: config.messageCharacteristicUUID),
            CBUUID(string: config.deviceInfoCharacteristicUUID)
        ]

        peripheral.services?.forEach { service in
            peripheral.discoverCharacteristics(wanted, for: service)
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            state = .failed(error.localizedDescription)
            logStore.append(.error, "Characteristic discovery failed: \(error.localizedDescription)")
            return
        }

        service.characteristics?.forEach { characteristic in
            if characteristic.uuid == CBUUID(string: config.messageCharacteristicUUID) {
                messageCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            } else if characteristic.uuid == CBUUID(string: config.deviceInfoCharacteristicUUID) {
                deviceInfoCharacteristic = characteristic
                peripheral.readValue(for: characteristic)
            }
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            state = .failed(error.localizedDescription)
            logStore.append(.error, "Subscribe failed: \(error.localizedDescription)")
            return
        }

        if characteristic.uuid == CBUUID(string: config.messageCharacteristicUUID), characteristic.isNotifying {
            let name = peripheral.name ?? "Stick device"
            state = .subscribed(name)
            logStore.append(.info, "Subscribed to messages")
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            logStore.append(.error, "Read failed: \(error.localizedDescription)")
            return
        }

        guard let data = characteristic.value else {
            return
        }

        if characteristic.uuid == CBUUID(string: config.messageCharacteristicUUID) {
            do {
                let message = try JSONDecoder().decode(StickMessage.self, from: data)
                guard accepts(message: message) else {
                    logStore.append(.info, "Filtered \(message.app)/\(message.type)")
                    return
                }
                deviceInfo.messageCount += 1
                logStore.append(message)
            } catch {
                let raw = String(data: data, encoding: .utf8) ?? "\(data.count) bytes"
                logStore.append(.error, "Message decode failed: \(raw)")
            }
        } else if characteristic.uuid == CBUUID(string: config.deviceInfoCharacteristicUUID) {
            deviceInfo.rawInfo = String(data: data, encoding: .utf8) ?? "\(data.count) bytes"
        }
    }
}
