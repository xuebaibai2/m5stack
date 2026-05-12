---
name: sticks3-weather-launcher-patterns
description: Use when working on M5Stack StickS3 launcher, weather app, Remote Mic BLE audio, or companion Mac menu bar code in this repo.
---

# StickS3 Weather Launcher Patterns

Use this skill when editing the StickS3 launcher, Weather App, Remote Mic BLE
messaging, or the companion Mac menu bar app.

## Config

- Keep secrets and site settings in `.env`.
- Generate `src/generated_config.h` from `.env` before build.
- Treat `WIFI_SSID` and `WIFI_PASSWORD` as required for upload.
- Keep weather location settings configurable, not hard-coded.

## Boot Flow

- Start Wi-Fi as early as possible after `M5.begin()`.
- Do not break the app if Wi-Fi is unavailable.
- Show visible Wi-Fi, BLE, and battery indicators through the shared status bar.

## App Behavior

- Launcher menu uses larger, readable option text.
- Launcher and Weather App headers use `src/status_bar.cpp` for Wi-Fi, BLE, and
  battery percentage.
- Button B cycles apps.
- Button A launches the selected app.
- In the weather app, short press Button A refreshes data.
- In Remote Mic, hold Button A to stream mic audio over BLE.
- Long press Button A or Button B returns to the menu.
- In Remote Mic, Button A long press is reserved for push-to-talk, so Button B
  long press is the menu return path.

## Weather Fetching

- Use Open-Meteo without an API key when only current weather is needed.
- Request only the fields required by the UI.
- Parse the response defensively and surface the actual failure reason.
- Keep the weather screen usable in offline or error states.

## Rendering

- Draw the full weather screen only when the state changes.
- Animate only the icon region after the first render.
- Keep text and static widgets outside the animated box.
- Redraw the smallest possible rectangle when animating moving content.

## Remote Mic BLE

- Keep Remote Mic firmware in `src/remote_mic_app.cpp` /
  `src/remote_mic_app.h`.
- Keep shared BLE protocol constants and JSON encoding in
  `src/stick_link_protocol.h`.
- StickS3 acts as a BLE peripheral named `StickS3 Link`.
- Use the custom service and characteristic UUIDs documented in
  `docs/bluetooth-protocol.md`.
- Send small JSON v1 control envelopes over the message characteristic.
- Send packed 12-bit unsigned PCM mono chunks over the audio characteristic; the Mac app
  decodes them back to little-endian 16-bit mono PCM for Speech and WAV files.
- Keep Remote Mic at 8 kHz / 100 samples per notification so BLE sends 150-byte
  chunks below the negotiated MTU.
- Capture the StickS3 mic at 16 kHz and downsample to the 8 kHz BLE stream;
  avoid configuring the hardware mic path directly at low rates when quality is
  poor.
- Configure M5Unified mic gain/filtering before capture; keep gain low enough
  that speech WAVs do not hit full scale.
- For Remote Mic speech quality, prefer the simplest possible capture path
  before adding filters or speech codecs that can sound robotic.
- Keep the protocol generic: use `app`, `type`, and `name` fields instead of
  hardcoding Mac behavior to one StickS3 app.
- Keep BLE audio optimized for short push-to-talk utterances.

## Mac Companion App

- Keep the native SwiftUI menu bar app under `mac/StickLinkMenuBar/`.
- Keep it movable: do not depend on files outside its folder at runtime.
- Load runtime config from `mac/StickLinkMenuBar/config/sticklink.json`.
- Use CoreBluetooth as the central, scan for the configured service UUID, then
  subscribe to the message and audio characteristics.
- Use macOS Speech for transcription and Accessibility/clipboard paste for
  output into the focused text editor.
- Validate Mac-side protocol/config/log behavior with
  `swift run StickLinkValidation`.

## M5Unified / M5GFX

- Call `M5.update()` in `loop()`.
- Use `M5.Display` for all UI drawing.
- Prefer small functions and separate modules for launcher, weather, and sensor
  logic.

## Validation

- Run `pio run` after firmware changes.
- Run `swift build` and `swift run StickLinkValidation` after Mac app changes.
- Real BLE discovery, connection, and notifications require manual StickS3 +
  macOS Bluetooth testing.
