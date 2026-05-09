# AGENTS.md

## Purpose

This repository contains Arduino C++ code for the **M5Stack StickS3**.

When generating, modifying, or reviewing code in this repository, agents must treat the official M5Stack documentation as the primary source of truth.

## Target Device

- Device: **M5Stack StickS3**
- Platform: **ESP32-S3**
- Development style: **Arduino C++**
- Recommended core libraries:
  - `M5Unified`
  - `M5GFX`

## Official Documentation to Reference

Before creating or changing StickS3 Arduino code, refer to these official documents:

- StickS3 product page:  
  https://docs.m5stack.com/en/core/StickS3

- StickS3 Arduino Program Compilation & Upload:  
  https://docs.m5stack.com/en/arduino/m5sticks3/program

- M5Unified Quick Start:  
  https://docs.m5stack.com/en/arduino/m5unified/helloworld

- M5Unified PlatformIO User Guide:  
  https://docs.m5stack.com/en/arduino/m5unified/intro_vscode

- M5GFX API Reference:  
  https://docs.m5stack.com/en/arduino/m5gfx/m5gfx_functions

Agents should prefer examples and APIs from the official M5Stack docs over random blog posts or generated assumptions.

## Code Generation Rules

When creating StickS3 Arduino code:

1. Use Arduino C++.
2. Include `M5Unified.h` unless there is a clear reason not to.
3. Initialize the device with:

   ```cpp
   auto cfg = M5.config();
   M5.begin(cfg);
   ```

4. Call `M5.update()` inside `loop()` when using buttons, IMU, power state, or other M5 device state.
5. Use `M5.Display` / M5GFX APIs for drawing to the screen.
6. Avoid blocking long-running code in `loop()` unless necessary.
7. Prefer small functions for each feature, screen, or mode.
8. Keep hardware-specific assumptions explicit.
9. Do not assume UIFlow2/MicroPython files such as `boot.py`, `main.py`, or `/apps` are available after flashing Arduino firmware.
10. Remember that flashing Arduino firmware replaces the existing UIFlow2/MicroPython firmware.

## Recommended Minimal Example

```cpp
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.println("Hello StickS3");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Display.println("Button pressed");
  }
}
```

## Project Structure

Prefer this structure for PlatformIO-based projects:

```text
.
├── AGENTS.md
├── platformio.ini
├── src
│   └── main.cpp
└── test
    └── test_basic.cpp
```

Recommended `platformio.ini` starting point:

```ini
[env:m5stack-sticks3]
platform = espressif32
board = m5stack-sticks3
framework = arduino
monitor_speed = 115200
lib_deps =
  m5stack/M5Unified
  m5stack/M5GFX
```

If `m5stack-sticks3` is not available in the installed PlatformIO board definitions, use the closest official ESP32-S3 board configuration and document the reason clearly in the PR or commit message.

## Build and Upload Commands

Use PlatformIO CLI when no IDE is desired:

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbmodem101
pio device monitor -p /dev/cu.usbmodem101 -b 115200
```

The upload port may differ by machine. On macOS, check ports with:

```bash
ls /dev/cu.*
```

## Testing Requirements

Generated code should include or preserve tests where practical.

### Required checks before considering work complete

Run:

```bash
pio run
```

This verifies the code compiles for the configured StickS3/ESP32-S3 Arduino environment.

### Unit tests

When code includes logic that can be tested without physical hardware, put that logic in separate functions/classes and add PlatformIO tests under `test/`.

Examples of testable logic:

- menu state transitions
- button event mapping
- configuration parsing
- formatting display strings
- sensor value conversion
- BLE payload encoding/decoding
- IR command mapping

Avoid putting all logic directly inside `setup()` and `loop()` if it makes the code impossible to test.

### Hardware-dependent tests

For code that needs the actual StickS3 hardware, document a manual test checklist in the PR or commit message.

Manual checks should include relevant items such as:

- device boots successfully
- display renders expected screen
- Button A responds
- IMU values update if used
- speaker/mic works if used
- IR transmit/receive works if used
- Wi-Fi/BLE connects if used
- serial monitor shows expected logs
- no repeated crash/reboot loop

### Suggested manual test template

```text
Manual StickS3 test:
- [ ] Firmware uploads successfully
- [ ] Device boots to the expected screen
- [ ] Button A interaction works
- [ ] Display orientation and text are correct
- [ ] Serial monitor has no crash loop
- [ ] Feature-specific hardware behavior verified
```

## Review Checklist

Before finalizing generated code, agents should check:

- [ ] Official M5Stack StickS3 docs were referenced
- [ ] `M5.begin()` is called correctly
- [ ] `M5.update()` is called in `loop()` when needed
- [ ] Display code uses `M5.Display`
- [ ] Code compiles with `pio run`
- [ ] Testable logic is covered by tests or isolated for future testing
- [ ] Hardware-only behavior has a manual test checklist
- [ ] Any board/config assumptions are documented

## Notes for Agents

Do not claim that code has been tested on physical hardware unless it actually has been tested on a StickS3.

If only a compile check was run, say that clearly.

If official documentation and generated assumptions conflict, follow the official documentation.
