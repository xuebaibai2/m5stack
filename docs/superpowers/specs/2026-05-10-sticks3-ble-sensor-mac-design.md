# StickS3 BLE Sensor App To Mac Menu Bar Design

## Context

This repository is a PlatformIO Arduino C++ project for M5Stack StickS3. The
launcher is in `src/main.cpp`. Per `docs/code-structure.md`, the second app is
`Sensor App` at app index `1`, and it currently renders through the placeholder
app path.

Official M5Stack documentation referenced before this design:

- StickS3 product page: confirms the ESP32-S3 based StickS3 hardware, built-in
  display, buttons, microphone/audio hardware, and Arduino/PlatformIO support.
- StickS3 Arduino quick start: confirms Arduino development flow.
- M5Unified hello world: confirms `M5.config()`, `M5.begin(cfg)`, `M5.update()`,
  and `M5.Display` usage patterns.
- M5GFX API reference: confirms display drawing API usage.

The ESP32-S3 target should use Bluetooth Low Energy for this feature. The Mac
side should use CoreBluetooth as a central and the StickS3 should act as a BLE
peripheral.

## Goals

- Button A inside Sensor App sends a test event to a connected Mac.
- The Mac app runs independently from a separate folder and can be moved later.
- The Mac app is a native SwiftUI macOS menu bar app.
- The Mac UI shows connection status, incoming logs, and basic device
  information when available.
- The protocol and app structure are generic enough to support future StickS3
  apps, message types, voice-trigger events, and later audio-oriented metadata.

## Non-Goals

- Streaming microphone audio in this iteration.
- Implementing pairing flows beyond normal BLE scanning and connecting.
- Persisting logs across app restarts unless it falls out cheaply from the
  chosen SwiftUI app structure.
- Shipping a signed or notarized macOS app bundle.

## Firmware Architecture

Add a dedicated Sensor App module:

- `src/sensor_app.h`
- `src/sensor_app.cpp`

Public functions:

- `sensorAppBegin()`: initialize BLE service/advertising during boot.
- `sensorAppStart()`: render Sensor App screen and current BLE status.
- `sensorAppUpdate()`: refresh status UI when connection/message state changes.
- `sensorAppSendButtonA()`: encode and send the Button A event.
- `sensorAppStop()`: leave app screen state without stopping BLE advertising.
- `sensorAppConnected()`: expose BLE connection state for UI.

Launcher integration in `src/main.cpp`:

- Include `sensor_app.h`.
- Call `sensorAppBegin()` in `setup()` after `M5.begin(cfg)`.
- Branch `drawApp(1)` to `sensorAppStart()`.
- In app input handling, short Button A for `runningApp == 1` calls
  `sensorAppSendButtonA()`.
- Long Button A or Button B continues returning to the launcher.
- `loop()` calls `sensorAppUpdate()` while Sensor App is running.

BLE behavior:

- StickS3 advertises a custom BLE service UUID.
- A notify characteristic sends message envelopes to subscribed centrals.
- A read characteristic, or readable value on the notify characteristic where
  supported by the library, exposes lightweight device/app metadata.
- Advertising name defaults to a generic value such as `StickS3 Link`, not
  `Sensor App`, so the Mac app remains app-agnostic.

## Message Protocol

Use a newline-free JSON envelope for all messages in v1. The first payload is
small and human-readable:

```json
{
  "v": 1,
  "id": "000001",
  "app": "sensor",
  "type": "button",
  "name": "ButtonA",
  "text": "ButtonA pressed from Sensor App",
  "ts_ms": 123456,
  "seq": 1
}
```

Required fields:

- `v`: protocol version.
- `id`: unique-ish message id for display/debugging.
- `app`: logical source app, such as `sensor`.
- `type`: message category, such as `button`, `voice_trigger`, or `audio_meta`.
- `name`: event name within the category.
- `text`: human-readable log text.
- `ts_ms`: device uptime from `millis()`.
- `seq`: monotonically increasing device-side sequence number.

Future extension rules:

- Consumers ignore unknown fields.
- Producers keep required fields stable for protocol version `1`.
- Larger future payloads, especially audio, should use a separate characteristic
  or chunking protocol instead of overloading the basic event characteristic.

## Mac App Architecture

Create a separate folder:

```text
mac/StickLinkMenuBar/
```

Use a Swift Package executable with a native SwiftUI app. The app uses:

- `MenuBarExtra` for status bar presence.
- `CoreBluetooth` for scanning, connecting, discovering service/characteristic,
  subscribing to notifications, and reading available device info.
- SwiftUI observable models for connection state, config, and log storage.

Suggested modules:

- `StickLinkMenuBarApp.swift`: app entry point and menu bar scene.
- `Bluetooth/StickBluetoothClient.swift`: CoreBluetooth central wrapper.
- `Protocol/StickMessage.swift`: Codable message envelope.
- `Configuration/StickLinkConfig.swift`: runtime config loading.
- `Models/LogStore.swift`: in-memory incoming log storage.
- `Views/StatusView.swift`: connection status and actions.
- `Views/LogsView.swift`: incoming message list.
- `Views/ConfigView.swift`: visible runtime config values.

Runtime config:

- Provide a JSON file in `mac/StickLinkMenuBar/config/sticklink.json`.
- Values include service UUID, message characteristic UUID, optional device name
  prefix, allowed apps, allowed message types, scan timeout, and max retained
  log count.
- Load config at app start and expose a reload action in the UI.
- Defaults are bundled in code so the app can still run if the config file is
  missing.

UI behavior:

- The menu bar item shows a connected/disconnected state.
- The main popover or menu content shows scanning/connecting/subscribed/error
  states.
- Incoming messages remain visible in a log list for the current run.
- Basic device information is shown when CoreBluetooth exposes it, including
  peripheral name, identifier, RSSI if recently scanned, service UUID, and
  message count.
- UI text and config labels stay generic: "Stick device", "app", "message type",
  and "event" instead of hardcoding Sensor App everywhere.

## Error Handling

Firmware:

- If no central is connected, Button A still updates the Sensor App screen and
  Serial log with a "not connected" status.
- If notify fails or no subscription exists, retain BLE advertising and show a
  recoverable status.
- Keep BLE setup independent of Weather App Wi-Fi state.

Mac app:

- Show when Bluetooth is powered off, unauthorized, unsupported, scanning,
  connecting, connected, subscribed, or disconnected.
- Keep received logs even if the device disconnects.
- Config load failures fall back to defaults and display the failure in the log.

## Testing

Firmware automated checks:

- Add host-testable protocol encoding logic where practical.
- Run `pio run` before completion.

Firmware manual checks:

- Firmware uploads successfully.
- Device boots to launcher.
- Sensor App opens from the second launcher slot.
- Mac app can discover and connect to the StickS3 BLE peripheral.
- Button A inside Sensor App sends the test message.
- Display shows sane BLE/send status.
- Long Button A or Button B returns to the launcher.
- Serial monitor has no repeated crash or reboot loop.

Mac app automated checks:

- Config loading uses defaults when config is missing.
- Config loading parses service/characteristic UUIDs and filters.
- Message decoder accepts valid v1 envelopes.
- Message decoder ignores unknown fields.
- Log store retains messages and enforces max count.
- Bluetooth client state model can be validated with a fake adapter or isolated
  reducer-style state tests.

Mac manual checks:

- App launches locally from `mac/StickLinkMenuBar`.
- App appears in the macOS menu bar.
- Scan/connect flow reaches subscribed state with the StickS3 nearby.
- Button A messages appear in the UI log.
- Runtime config can be edited and reloaded without code changes.

## Documentation

Add:

- `docs/bluetooth-protocol.md`: UUIDs, payload schema, extension rules, and
  limitations.
- `docs/sensor-app-bluetooth.md`: firmware behavior, build/upload steps, and
  manual StickS3 checks.
- `mac/StickLinkMenuBar/README.md`: local run instructions, config format,
  Bluetooth permission notes, pairing/connect instructions, and test commands.

## Assumptions And Limitations

- BLE is used instead of Classic Bluetooth because the target is ESP32-S3 and
  macOS has first-class CoreBluetooth support.
- BLE notifications are appropriate for short events and metadata, but not for
  raw low-latency audio streaming without a dedicated chunking design.
- macOS may prompt for Bluetooth permission on first launch.
- Automated tests can cover protocol/config/log behavior, but real Bluetooth
  discovery and notification delivery require manual hardware validation.
- Flashing Arduino firmware replaces any existing UIFlow2/MicroPython firmware.
