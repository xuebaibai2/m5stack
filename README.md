# StickS3 Launcher, Remote Mic, Weather, and CodeBuddy

This repository contains Arduino C++ firmware for an M5Stack StickS3 plus a
native macOS companion app. The firmware replaces the stock UIFlow firmware with
a small launcher that can run three apps:

- Weather App: connects to Wi-Fi and fetches current weather from Open-Meteo.
- Remote Mic: streams StickS3 microphone audio to the Mac over BLE for
  transcription and saved WAV recordings.
- CodeBuddy: exposes the upstream CodeBuddy BLE protocol as a third launcher
  app without removing Weather or Remote Mic.

The macOS app under `mac/StickLinkMenuBar` connects to the StickS3 over
CoreBluetooth for Remote Mic audio, weather configuration commands, logs, and
transcript output.

## Hardware And Tooling

- Device: M5Stack StickS3 / ESP32-S3.
- Firmware stack: PlatformIO, Arduino C++, M5Unified, M5GFX.
- Mac app stack: SwiftUI, CoreBluetooth, Speech, Accessibility.

Install PlatformIO and Swift command line tools before building.

## Configure Firmware

Create a local `.env` file at the repo root for Wi-Fi and default weather
location. `scripts/generate_config.py` converts this into
`src/generated_config.h` during `pio run`.

Example:

```env
WIFI_SSID=your_wifi_name
WIFI_PASSWORD=your_wifi_password
WEATHER_LOCATION_NAME=Sydney
WEATHER_LATITUDE=-33.8688
WEATHER_LONGITUDE=151.2093
WEATHER_TIMEZONE=Australia/Sydney
```

## Build And Upload Firmware

Compile:

```bash
pio run
```

Upload after putting the StickS3 into download mode and replacing the port if
needed:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem101
```

Open serial logs:

```bash
./monitor.sh
```

## Use The StickS3 Apps

From the launcher:

- Button B cycles Weather App, Remote Mic, and CodeBuddy.
- Button A launches the selected app.

Weather App:

- Button A refreshes weather.
- Button B returns to the launcher.

Remote Mic:

- Open the Mac menu bar app first, click `Scan`, and wait for `Subscribed`.
- Hold Button A to record and stream voice to the Mac.
- Release Button A to finish the recording/transcription.
- Long Button B returns to the launcher.

CodeBuddy:

- Open CodeBuddy on the StickS3 before running the CodeBuddy host.
- Run `./run-code-buddy.sh` for first-time pairing/host setup when needed.
- After setup, use `codex` normally from a new shell.
- Hold Button A inside CodeBuddy to open its menu and return to the launcher.

Remote Mic and CodeBuddy use different BLE advertisement modes. Opening
CodeBuddy advertises `Codex-StickS3` for the CodeBuddy host. Leaving CodeBuddy
or opening Remote Mic restores `StickS3 Link` for the Mac menu bar app.

## Mac Menu Bar App

Run:

```bash
cd mac/StickLinkMenuBar
swift run StickLinkMenuBar
```

The app scans for the StickS3, subscribes to message/audio characteristics,
saves Remote Mic WAV recordings to `~/Downloads`, and can paste transcripts into
the focused app after Accessibility permission is granted.

See `mac/StickLinkMenuBar/README.md` for detailed Mac app configuration and
permissions.

## Regression Check

Run the full local regression gate before committing future changes:

```bash
scripts/regression_check.sh
```

It runs:

- `pio run`
- static guardrails for BLE advertisement mode, Remote Mic repeat recording,
  Weather fetch behavior, and CodeBuddy runtime release
- `swift run StickLinkValidation`

Real BLE connection, audio transfer, weather Wi-Fi, and speech recognition still
need physical StickS3 and macOS validation. The automated regression check
protects the source-level behavior that previously broke those flows.

## Useful Docs

- `docs/remote-mic-bluetooth.md`: Remote Mic BLE behavior and manual checklist.
- `docs/bluetooth-protocol.md`: Stick Link and CodeBuddy BLE protocol notes.
- `docs/codebuddy-third-app.md`: CodeBuddy integration notes and test checklist.
- `docs/upload-instructions.md`: upload and serial-monitor troubleshooting.
