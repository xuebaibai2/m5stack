# StickS3 BLE Sensor Mac Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build BLE event sending from the StickS3 Sensor App to a standalone native SwiftUI macOS menu bar app.

**Architecture:** The StickS3 acts as a BLE peripheral with a custom service, a notify message characteristic, and a readable device-info characteristic. The Mac app is a native Swift Package executable using SwiftUI `MenuBarExtra` and CoreBluetooth as the central. Both sides share a small JSON v1 envelope protocol documented in repo docs.

**Tech Stack:** Arduino C++ with M5Unified, M5GFX, ArduinoJson, ESP32 BLE Arduino APIs; Swift 5.9+, SwiftUI, CoreBluetooth, XCTest.

---

## File Map

- Create `src/stick_link_protocol.h`: protocol constants and host-testable JSON encoding.
- Create `src/sensor_app.h`: Sensor App public interface.
- Create `src/sensor_app.cpp`: Sensor App display, BLE peripheral setup, and Button A event send behavior.
- Modify `src/main.cpp`: launcher integration for Sensor App.
- Modify `platformio.ini`: add BLE library dependency if the installed framework does not expose required headers.
- Create `test/test_stick_link_protocol/test_main.cpp`: PlatformIO unit tests for protocol encoding.
- Create `docs/bluetooth-protocol.md`: shared UUIDs, JSON schema, and extension rules.
- Create `docs/sensor-app-bluetooth.md`: firmware build/upload/manual test guide.
- Create `mac/StickLinkMenuBar/Package.swift`: standalone Swift package.
- Create `mac/StickLinkMenuBar/config/sticklink.json`: runtime config.
- Create Swift source files under `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/`.
- Create Swift test files under `mac/StickLinkMenuBar/Tests/StickLinkMenuBarTests/`.
- Create `mac/StickLinkMenuBar/README.md`: run, test, permissions, and BLE connection instructions.

## Task 1: Shared Firmware Protocol

**Files:**
- Create: `src/stick_link_protocol.h`
- Create: `test/test_stick_link_protocol/test_main.cpp`

- [ ] **Step 1: Write failing protocol tests**

Create `test/test_stick_link_protocol/test_main.cpp` with tests that call `stickLinkEncodeButtonEvent(...)` and assert the JSON contains the v1 envelope fields.

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e m5stack-sticks3 -f test_stick_link_protocol`

Expected: FAIL because `src/stick_link_protocol.h` does not exist.

- [ ] **Step 3: Implement minimal protocol header**

Create `src/stick_link_protocol.h` with constants for service, message characteristic, device-info characteristic, default BLE name, and an inline encoder:

```cpp
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

constexpr const char* kStickLinkBleName = "StickS3 Link";
constexpr const char* kStickLinkServiceUuid =
    "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60";
constexpr const char* kStickLinkMessageCharacteristicUuid =
    "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60";
constexpr const char* kStickLinkDeviceInfoCharacteristicUuid =
    "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60";

inline String stickLinkEncodeEvent(const char* app, const char* type,
                                   const char* name, const char* text,
                                   uint32_t tsMs, uint32_t seq) {
  JsonDocument doc;
  doc["v"] = 1;
  char id[12];
  snprintf(id, sizeof(id), "%06lu", static_cast<unsigned long>(seq));
  doc["id"] = id;
  doc["app"] = app;
  doc["type"] = type;
  doc["name"] = name;
  doc["text"] = text;
  doc["ts_ms"] = tsMs;
  doc["seq"] = seq;

  String output;
  serializeJson(doc, output);
  return output;
}

inline String stickLinkEncodeButtonEvent(const char* app, const char* buttonName,
                                         const char* text, uint32_t tsMs,
                                         uint32_t seq) {
  return stickLinkEncodeEvent(app, "button", buttonName, text, tsMs, seq);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e m5stack-sticks3 -f test_stick_link_protocol`

Expected: PASS for protocol tests.

## Task 2: Sensor App Firmware BLE Peripheral

**Files:**
- Create: `src/sensor_app.h`
- Create: `src/sensor_app.cpp`
- Modify: `src/main.cpp`
- Modify: `platformio.ini` if BLE headers require an explicit library.

- [ ] **Step 1: Add Sensor App interface**

Create `src/sensor_app.h` with:

```cpp
#pragma once

void sensorAppBegin();
void sensorAppStart();
void sensorAppUpdate();
void sensorAppSendButtonA();
void sensorAppStop();
bool sensorAppConnected();
```

- [ ] **Step 2: Implement Sensor App BLE behavior**

Create `src/sensor_app.cpp` with BLE setup, connection callbacks, display rendering, and Button A notification send using `stickLinkEncodeButtonEvent("sensor", "ButtonA", "ButtonA pressed from Sensor App", millis(), seq)`.

- [ ] **Step 3: Wire launcher app index 1**

Modify `src/main.cpp` to include `sensor_app.h`, call `sensorAppBegin()` in `setup()`, start Sensor App from `drawApp(1)`, stop it from `returnToMenu()`, send Button A events from `handleAppInput()`, and update it in `loop()` while `runningApp == 1`.

- [ ] **Step 4: Compile firmware**

Run: `pio run`

Expected: build succeeds for the configured StickS3/ESP32-S3 Arduino environment.

## Task 3: Mac App Protocol, Config, And Log Tests

**Files:**
- Create: `mac/StickLinkMenuBar/Package.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Protocol/StickMessage.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Configuration/StickLinkConfig.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Models/LogStore.swift`
- Create: `mac/StickLinkMenuBar/Tests/StickLinkMenuBarTests/StickMessageTests.swift`
- Create: `mac/StickLinkMenuBar/Tests/StickLinkMenuBarTests/StickLinkConfigTests.swift`
- Create: `mac/StickLinkMenuBar/Tests/StickLinkMenuBarTests/LogStoreTests.swift`

- [ ] **Step 1: Create Swift package and failing tests**

Create package and test files that cover decoding a v1 message with unknown fields, default config loading, JSON config parsing, and max-count log retention.

- [ ] **Step 2: Run Swift tests to verify failure**

Run: `swift test` from `mac/StickLinkMenuBar`

Expected: FAIL until source files exist.

- [ ] **Step 3: Implement protocol, config, and log store**

Implement Codable `StickMessage`, Codable `StickLinkConfig` with default UUIDs matching `src/stick_link_protocol.h`, and observable `LogStore`.

- [ ] **Step 4: Run Swift tests to verify pass**

Run: `swift test` from `mac/StickLinkMenuBar`

Expected: PASS.

## Task 4: Native SwiftUI Menu Bar App

**Files:**
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/StickLinkMenuBarApp.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Bluetooth/StickBluetoothClient.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Views/StatusView.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Views/LogsView.swift`
- Create: `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Views/ConfigView.swift`
- Create: `mac/StickLinkMenuBar/config/sticklink.json`

- [ ] **Step 1: Implement CoreBluetooth client**

Create an `ObservableObject` client that scans for the configured service UUID, connects, discovers message and info characteristics, subscribes to notifications, decodes messages, and writes connection transitions to `LogStore`.

- [ ] **Step 2: Implement menu bar UI**

Create a SwiftUI `@main` app with `MenuBarExtra`, status controls, config summary, log list, and scan/disconnect/reload actions.

- [ ] **Step 3: Build Mac app**

Run: `swift build` from `mac/StickLinkMenuBar`

Expected: build succeeds.

## Task 5: Documentation And Final Validation

**Files:**
- Create: `docs/bluetooth-protocol.md`
- Create: `docs/sensor-app-bluetooth.md`
- Create: `mac/StickLinkMenuBar/README.md`

- [ ] **Step 1: Document protocol**

Write UUIDs, JSON schema, sample Button A payload, extension rules, and audio limitations in `docs/bluetooth-protocol.md`.

- [ ] **Step 2: Document firmware behavior**

Write Sensor App behavior, `pio run`, upload steps, and manual StickS3 hardware checklist in `docs/sensor-app-bluetooth.md`.

- [ ] **Step 3: Document Mac app**

Write local run commands, config format, Bluetooth permission notes, connect flow, tests, and manual validation in `mac/StickLinkMenuBar/README.md`.

- [ ] **Step 4: Run final validation**

Run:

```bash
pio run
```

Run from `mac/StickLinkMenuBar`:

```bash
swift test
swift build
```

Expected: all commands exit 0, or any failures are documented with exact cause.
