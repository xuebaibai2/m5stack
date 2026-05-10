# StickLinkMenuBar

Native SwiftUI macOS menu bar app for receiving StickS3 BLE events.

## Run Locally

From this folder:

```bash
swift run StickLinkMenuBar
```

The app appears in the macOS status/menu bar. Open it from the menu bar icon,
then click `Scan`.

For local development with automatic restart on Swift/config changes:

```bash
./dev.sh
```

This is a rebuild-and-relaunch loop, not true state-preserving hot reload.
Runtime config changes can also be applied with the in-app `Reload Config`
button without restarting.

## Validate

This local Command Line Tools install does not expose XCTest to SwiftPM test
targets, so the package includes a validation executable for automated checks:

```bash
swift run StickLinkValidation
```

It validates:

- protocol message decoding
- runtime config loading and default fallback behavior
- log storage retention and message formatting

Build the app:

```bash
swift build
```

## Runtime Config

Edit:

```text
config/sticklink.json
```

Default config:

```json
{
  "serviceUUID": "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60",
  "messageCharacteristicUUID": "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60",
  "deviceInfoCharacteristicUUID": "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60",
  "deviceNamePrefix": "StickS3",
  "allowedApps": [],
  "allowedMessageTypes": [],
  "scanTimeoutSeconds": 10,
  "maxRetainedLogs": 200
}
```

Empty `allowedApps` and `allowedMessageTypes` means accept all values. Use the
`Reload Config` button after editing.

## Connect To StickS3

1. Build and upload the firmware from the repository root with `pio run` and
   `pio run -t upload --upload-port <port>`.
2. Launch this Mac app with `swift run StickLinkMenuBar`.
3. Grant Bluetooth permission if macOS prompts.
4. Click `Scan`.
5. Wait for the state to become `Subscribed`.
6. Open Sensor App on the StickS3.
7. Press Button A.

Expected log:

```text
sensor/button ButtonA: ButtonA pressed from Sensor App
```

## Notes

- The app connects through CoreBluetooth; no manual macOS pairing is required.
- Bluetooth scans require macOS Bluetooth permission.
- The current log store is in memory and resets when the app exits.
- The protocol is generic: future StickS3 apps can send different `app` and
  `type` values without Mac app code changes.
