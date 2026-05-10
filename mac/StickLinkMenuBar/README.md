# StickLinkMenuBar

Native SwiftUI macOS menu bar app for receiving StickS3 BLE events and Remote
Mic audio.

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
  "audioCharacteristicUUID": "6f7d9f13-2c3b-4e7a-9a1f-1b2c3d4e5f60",
  "deviceNamePrefix": "StickS3",
  "allowedApps": [],
  "allowedMessageTypes": [],
  "scanTimeoutSeconds": 10,
  "maxRetainedLogs": 200,
  "audioSampleRate": 8000,
  "transcriptionLocaleIdentifier": "en-US",
  "pasteTranscriptsToFocusedApp": true,
  "saveRecordingsToDownloads": true
}
```

Empty `allowedApps` and `allowedMessageTypes` means accept all values. Use the
`Reload Config` button after editing.

## Connect To StickS3

1. Build and upload the firmware from the repository root with `pio run` and
   `pio run -t upload --upload-port <port>`.
2. Launch this Mac app with `swift run StickLinkMenuBar`.
3. Grant Bluetooth, Speech Recognition, and Accessibility permissions if macOS
   prompts.
4. Click `Scan`.
5. Wait for the state to become `Subscribed`.
6. Open Remote Mic on the StickS3.
7. Click into any text editor.
8. Hold Button A, speak into the StickS3, then release Button A.

Expected log/output:

```text
Transcript: <recognized speech>
```

If `pasteTranscriptsToFocusedApp` is `true`, the transcript is copied to the
clipboard and pasted into the focused editor with Cmd+V.

When `saveRecordingsToDownloads` is `true`, each Remote Mic utterance is saved
as a WAV file in:

```text
~/Downloads/StickLink-RemoteMic-YYYYMMDD-HHMMSS.wav
```

Use that file to debug transcription quality. If the WAV is noisy, clipped,
silent, or choppy, the issue is in the StickS3 mic/BLE audio path. If the WAV
sounds correct but the transcript is wrong, tune the speech locale or sample
rate settings.

## Notes

- The app connects through CoreBluetooth; no manual macOS pairing is required.
- Bluetooth scans require macOS Bluetooth permission.
- Speech transcription requires macOS Speech Recognition permission.
- Pasting into another app requires Accessibility permission.
- The current log store is in memory and resets when the app exits.
- The protocol is generic: future StickS3 apps can send different `app` and
  `type` values without Mac app code changes.
